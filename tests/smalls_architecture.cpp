#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct SourceLine {
    fs::path path;
    size_t line = 0;
    size_t scope_depth = 0;
    std::string text;
};

int scope_delta(std::string_view line)
{
    int result = 0;
    char quote = '\0';
    bool escaped = false;
    for (size_t index = 0; index < line.size(); ++index) {
        const char ch = line[index];
        if (quote != '\0') {
            if (escaped) {
                escaped = false;
            } else if (ch == '\\') {
                escaped = true;
            } else if (ch == quote) {
                quote = '\0';
            }
            continue;
        }
        if (ch == '/' && index + 1 < line.size() && line[index + 1] == '/') {
            break;
        }
        if (ch == '\'' || ch == '"') {
            quote = ch;
        } else if (ch == '{') {
            ++result;
        } else if (ch == '}') {
            --result;
        }
    }
    return result;
}

std::vector<SourceLine> smalls_source_lines(const fs::path& root)
{
    std::vector<SourceLine> result;
    for (const auto& entry : fs::recursive_directory_iterator{root}) {
        if (!entry.is_regular_file() || entry.path().extension() != ".smalls") {
            continue;
        }

        std::ifstream input{entry.path()};
        std::string text;
        size_t line = 0;
        int depth = 0;
        while (std::getline(input, text)) {
            result.push_back(SourceLine{
                entry.path(), ++line, static_cast<size_t>(std::max(depth, 0)),
                std::move(text)});
            depth += scope_delta(result.back().text);
        }
    }
    return result;
}

std::string locations(const std::vector<SourceLine>& lines)
{
    std::ostringstream result;
    for (const auto& line : lines) {
        result << line.path.string() << ':' << line.line << ": " << line.text << '\n';
    }
    return result.str();
}

bool starts_with_import(std::string_view line, std::string_view package)
{
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
        line.remove_prefix(1);
    }
    const std::string import = "import " + std::string{package};
    const std::string from = "from " + std::string{package};
    return line.starts_with(import) || line.starts_with(from);
}

bool starts_with_code(std::string_view line, std::string_view code)
{
    while (!line.empty() && (line.front() == ' ' || line.front() == '\t')) {
        line.remove_prefix(1);
    }
    return line.starts_with(code);
}

bool declares_native(std::string_view line)
{
    const size_t begin = line.find("[[");
    const size_t end = line.find("]]", begin);
    if (begin == std::string_view::npos || end == std::string_view::npos) {
        return false;
    }
    const auto attributes = line.substr(begin + 2, end - begin - 2);
    size_t cursor = 0;
    while (cursor < attributes.size()) {
        const size_t comma = attributes.find(',', cursor);
        auto attribute = attributes.substr(cursor,
            comma == std::string_view::npos
                ? attributes.size() - cursor
                : comma - cursor);
        while (!attribute.empty() && attribute.front() == ' ') {
            attribute.remove_prefix(1);
        }
        while (!attribute.empty() && attribute.back() == ' ') {
            attribute.remove_suffix(1);
        }
        if (attribute == "native") {
            return true;
        }
        if (comma == std::string_view::npos) {
            break;
        }
        cursor = comma + 1;
    }
    return false;
}

} // namespace

TEST(SmallsArchitecture, NativeDeclarationsAreCoreOwned)
{
    const fs::path root = fs::path{ROLLNW_TEST_SOURCE_DIR};
    std::vector<SourceLine> violations;
    for (const auto& line : smalls_source_lines(root / "lib/nw/smalls/scripts")) {
        if (declares_native(line.text)
            && line.path.parent_path().filename() != "core"
            && line.path.parent_path().parent_path().filename() != "core") {
            violations.push_back(line);
        }
    }
    for (const auto& line : smalls_source_lines(root / "tools/ui/scripts")) {
        if (declares_native(line.text)) {
            violations.push_back(line);
        }
    }
    EXPECT_TRUE(violations.empty()) << locations(violations);
}

TEST(SmallsArchitecture, ToolsetScriptsDoNotOwnEditorStateOrConfig)
{
    const fs::path root = fs::path{ROLLNW_TEST_SOURCE_DIR}
        / "tools/ui/scripts/toolset";
    const fs::path presentation_adapter = root / "ui.smalls";
    std::vector<SourceLine> violations;
    for (const auto& line : smalls_source_lines(root)) {
        const bool mutable_module_state = line.path != presentation_adapter
            && line.scope_depth == 0
            && starts_with_code(line.text, "var ");
        const bool config_authority = line.text.find("load_config!")
            != std::string::npos;
        if (mutable_module_state || config_authority) {
            violations.push_back(line);
        }
    }
    EXPECT_TRUE(violations.empty()) << locations(violations);
}

TEST(SmallsArchitecture, PropsetsAreProfileOwned)
{
    const fs::path root = fs::path{ROLLNW_TEST_SOURCE_DIR} / "lib/nw/smalls/scripts";
    const fs::path owner = root / "nwn1/propsets.smalls";
    std::vector<SourceLine> violations;
    for (const auto& line : smalls_source_lines(root)) {
        if (line.scope_depth == 0
            && starts_with_code(line.text, "[[propset(")
            && line.path != owner) {
            violations.push_back(line);
        }
    }
    EXPECT_TRUE(violations.empty()) << locations(violations);
}

TEST(SmallsArchitecture, PackageDependenciesPointInward)
{
    const fs::path scripts = fs::path{ROLLNW_TEST_SOURCE_DIR} / "lib/nw/smalls/scripts";
    std::vector<SourceLine> violations;
    for (const auto& line : smalls_source_lines(scripts / "core")) {
        if (starts_with_import(line.text, "nwn1")
            || starts_with_import(line.text, "toolset")) {
            violations.push_back(line);
        }
    }
    for (const auto& line : smalls_source_lines(scripts / "nwn1")) {
        if (starts_with_import(line.text, "toolset")) {
            violations.push_back(line);
        }
    }
    EXPECT_TRUE(violations.empty()) << locations(violations);
}
