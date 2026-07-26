#include "smalls_fixtures.hpp"

#include "../tools/smalls-lsp/server.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

using json = nlohmann::json;

std::string frame_message(const json& message)
{
    std::string body = message.dump();
    return "Content-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
}

std::vector<json> parse_messages(const std::string& framed)
{
    std::vector<json> result;
    size_t offset = 0;
    while (offset < framed.size()) {
        size_t header_end = framed.find("\r\n\r\n", offset);
        if (header_end == std::string::npos) {
            break;
        }

        constexpr std::string_view prefix = "Content-Length: ";
        size_t length_start = offset + prefix.size();
        size_t content_length = std::stoul(framed.substr(length_start, header_end - length_start));
        size_t body_start = header_end + 4;
        result.push_back(json::parse(framed.substr(body_start, content_length)));
        offset = body_start + content_length;
    }
    return result;
}

const json* response_with_id(const std::vector<json>& messages, int id)
{
    for (const auto& message : messages) {
        if (message.value("id", -1) == id) {
            return &message;
        }
    }
    return nullptr;
}

const json* diagnostics_with_version(const std::vector<json>& messages,
    std::string_view uri, int version)
{
    for (const auto& message : messages) {
        if (message.value("method", "") == "textDocument/publishDiagnostics"
            && message["params"].value("uri", "") == uri
            && message["params"].value("version", -1) == version) {
            return &message;
        }
    }
    return nullptr;
}

struct DecodedSemanticToken {
    int line = 0;
    int start = 0;
    int length = 0;
    int type = 0;
};

std::vector<DecodedSemanticToken> decode_semantic_tokens(const json& response)
{
    std::vector<DecodedSemanticToken> result;
    const json& data = response["result"]["data"];
    int line = 0;
    int start = 0;
    for (size_t i = 0; i + 4 < data.size(); i += 5) {
        int delta_line = data[i].get<int>();
        line += delta_line;
        start = delta_line == 0 ? start + data[i + 1].get<int>() : data[i + 1].get<int>();
        result.push_back(
            {line, start, data[i + 2].get<int>(), data[i + 3].get<int>()});
    }
    return result;
}

std::optional<DecodedSemanticToken> find_semantic_token(
    const json& response, int line, int type)
{
    for (const auto& token : decode_semantic_tokens(response)) {
        if (token.line == line && token.type == type) {
            return token;
        }
    }
    return std::nullopt;
}

} // namespace

TEST_F(SmallsLSP, ProtocolAdvertisesAndReturnsInlayHints)
{
    constexpr std::string_view uri = "file:///tmp/smalls_lsp_protocol.smalls";
    constexpr std::string_view source = R"(fn add(x: int, y: int): int {
    return x + y;
}

fn main() {
    var result = add(10, 20);
})";

    std::stringstream input;
    input << frame_message({{"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "initialize"},
        {"params", {{"capabilities", json::object()}}}});
    input << frame_message({{"jsonrpc", "2.0"},
        {"method", "textDocument/didOpen"},
        {"params", {{"textDocument", {{"uri", uri}, {"languageId", "smalls"}, {"version", 2}, {"text", source}}}}}});
    input << frame_message({{"jsonrpc", "2.0"},
        {"id", 2},
        {"method", "textDocument/inlayHint"},
        {"params", {{"textDocument", {{"uri", uri}}}, {"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 6}, {"character", 1}}}}}}}});

    std::stringstream output;
    run_smalls_lsp(input, output);
    auto messages = parse_messages(output.str());

    const json* initialize = response_with_id(messages, 1);
    ASSERT_NE(initialize, nullptr);
    EXPECT_TRUE((*initialize)["result"]["capabilities"]["inlayHintProvider"]);
    EXPECT_EQ((*initialize)["result"]["capabilities"]["textDocumentSync"]["change"], 1);
    EXPECT_EQ((*initialize)["result"]["capabilities"]["positionEncoding"], "utf-16");

    const json* hints = response_with_id(messages, 2);
    ASSERT_NE(hints, nullptr);
    ASSERT_TRUE((*hints)["result"].is_array());
    ASSERT_EQ((*hints)["result"].size(), 2);
    EXPECT_EQ((*hints)["result"][0]["label"], "x:");
    EXPECT_EQ((*hints)["result"][0]["kind"], 2);
    EXPECT_EQ((*hints)["result"][0]["position"]["line"], 5);
    EXPECT_EQ((*hints)["result"][1]["label"], "y:");
}

TEST_F(SmallsLSP, ProtocolRejectsStaleChangesAndUnknownRequests)
{
    constexpr std::string_view uri = "file:///tmp/smalls_lsp_stale.smalls";
    constexpr std::string_view source = R"(fn consume(value: int) {
}

fn main() {
    consume(42);
})";
    constexpr std::string_view updated_source = R"(fn consume(renamed: int) {
}

fn main() {
    consume(42);
})";

    std::stringstream input;
    input << frame_message({{"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "initialize"},
        {"params", {{"capabilities", json::object()}}}});
    input << frame_message({{"jsonrpc", "2.0"},
        {"method", "textDocument/didOpen"},
        {"params", {{"textDocument", {{"uri", uri}, {"languageId", "smalls"}, {"version", 5}, {"text", source}}}}}});
    input << frame_message({{"jsonrpc", "2.0"},
        {"method", "textDocument/didChange"},
        {"params", {{"textDocument", {{"uri", uri}, {"version", 4}}}, {"contentChanges", json::array({{{"text", "not valid smalls"}}})}}}});
    input << frame_message({{"jsonrpc", "2.0"},
        {"method", "textDocument/didChange"},
        {"params", {{"textDocument", {{"uri", uri}, {"version", 6}}}, {"contentChanges", json::array({{{"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 0}, {"character", 0}}}}}, {"text", "not valid smalls"}}})}}}});
    input << frame_message({{"jsonrpc", "2.0"},
        {"method", "textDocument/didChange"},
        {"params", {{"textDocument", {{"uri", uri}, {"version", 6}}}, {"contentChanges", json::array({{{"text", updated_source}}})}}}});
    input << frame_message({{"jsonrpc", "2.0"},
        {"id", 8},
        {"method", "textDocument/inlayHint"},
        {"params", {{"textDocument", {{"uri", uri}}}, {"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 5}, {"character", 1}}}}}}}});
    input << frame_message({{"jsonrpc", "2.0"},
        {"id", 9},
        {"method", "smalls/notImplemented"},
        {"params", json::object()}});

    std::stringstream output;
    run_smalls_lsp(input, output);
    auto messages = parse_messages(output.str());

    const json* hints = response_with_id(messages, 8);
    ASSERT_NE(hints, nullptr);
    ASSERT_EQ((*hints)["result"].size(), 1);
    EXPECT_EQ((*hints)["result"][0]["label"], "renamed:");

    const json* unknown = response_with_id(messages, 9);
    ASSERT_NE(unknown, nullptr);
    EXPECT_EQ((*unknown)["error"]["code"], -32601);
}

TEST_F(SmallsLSP, ProtocolRequiresInitialization)
{
    std::stringstream input;
    input << frame_message({{"jsonrpc", "2.0"},
        {"id", 7},
        {"method", "textDocument/hover"},
        {"params", json::object()}});

    std::stringstream output;
    run_smalls_lsp(input, output);
    auto messages = parse_messages(output.str());

    const json* response = response_with_id(messages, 7);
    ASSERT_NE(response, nullptr);
    EXPECT_EQ((*response)["error"]["code"], -32002);
}

TEST_F(SmallsLSP, ProtocolRejectsMalformedFramesWithoutThrowing)
{
    std::stringstream malformed{"Content-Length: nope\r\n\r\n{}"};
    std::stringstream malformed_output;
    EXPECT_NO_THROW(run_smalls_lsp(malformed, malformed_output));
    EXPECT_TRUE(malformed_output.str().empty());

    std::stringstream truncated{"Content-Length: 20\r\n\r\n{}"};
    std::stringstream truncated_output;
    EXPECT_NO_THROW(run_smalls_lsp(truncated, truncated_output));
    EXPECT_TRUE(truncated_output.str().empty());
}

TEST_F(SmallsLSP, ProtocolNegotiatesAndConvertsPositionEncoding)
{
    constexpr std::string_view uri = "file:///tmp/smalls_lsp_unicode.smalls";
    constexpr std::string_view source = R"(fn consume(value: int) {
}

fn main() {
    var label = "é😀"; consume(42);
})";
    constexpr std::string_view unicode_line = R"(    var label = "é😀"; consume(42);)";
    const int byte_column = static_cast<int>(unicode_line.find("42"));
    const int utf16_column = byte_column - 3;
    const int consume_byte_column = static_cast<int>(unicode_line.find("consume"));
    const int consume_utf16_column = consume_byte_column - 3;

    auto run_server = [&](json capabilities, int hover_character) {
        std::stringstream input;
        input << frame_message({{"jsonrpc", "2.0"},
            {"id", 1},
            {"method", "initialize"},
            {"params", {{"capabilities", std::move(capabilities)}}}});
        input << frame_message({{"jsonrpc", "2.0"},
            {"method", "textDocument/didOpen"},
            {"params", {{"textDocument", {{"uri", uri}, {"languageId", "smalls"}, {"version", 1}, {"text", source}}}}}});
        input << frame_message({{"jsonrpc", "2.0"},
            {"id", 2},
            {"method", "textDocument/inlayHint"},
            {"params", {{"textDocument", {{"uri", uri}}}, {"range", {{"start", {{"line", 0}, {"character", 0}}}, {"end", {{"line", 5}, {"character", 1}}}}}}}});
        input << frame_message({{"jsonrpc", "2.0"},
            {"id", 3},
            {"method", "textDocument/hover"},
            {"params", {{"textDocument", {{"uri", uri}}}, {"position", {{"line", 4}, {"character", hover_character}}}}}});
        input << frame_message({{"jsonrpc", "2.0"},
            {"id", 4},
            {"method", "textDocument/semanticTokens/full"},
            {"params", {{"textDocument", {{"uri", uri}}}}}});

        std::stringstream output;
        run_smalls_lsp(input, output);
        return parse_messages(output.str());
    };

    auto utf16_messages = run_server(json::object(), consume_utf16_column);
    const json* utf16_initialize = response_with_id(utf16_messages, 1);
    ASSERT_NE(utf16_initialize, nullptr);
    EXPECT_EQ((*utf16_initialize)["result"]["capabilities"]["positionEncoding"], "utf-16");
    const json* utf16_hints = response_with_id(utf16_messages, 2);
    ASSERT_NE(utf16_hints, nullptr);
    ASSERT_EQ((*utf16_hints)["result"].size(), 1);
    EXPECT_EQ((*utf16_hints)["result"][0]["position"]["character"], utf16_column);
    const json* utf16_hover = response_with_id(utf16_messages, 3);
    ASSERT_NE(utf16_hover, nullptr);
    EXPECT_FALSE((*utf16_hover)["result"].is_null());
    const json* utf16_semantic = response_with_id(utf16_messages, 4);
    ASSERT_NE(utf16_semantic, nullptr);
    auto utf16_string = find_semantic_token(*utf16_semantic, 4, 7);
    ASSERT_TRUE(utf16_string);
    EXPECT_EQ(utf16_string->length, 5);

    auto utf8_messages = run_server(
        {{"general", {{"positionEncodings", json::array({"utf-8", "utf-16"})}}}},
        consume_byte_column);
    const json* utf8_initialize = response_with_id(utf8_messages, 1);
    ASSERT_NE(utf8_initialize, nullptr);
    EXPECT_EQ((*utf8_initialize)["result"]["capabilities"]["positionEncoding"], "utf-8");
    const json* utf8_hints = response_with_id(utf8_messages, 2);
    ASSERT_NE(utf8_hints, nullptr);
    ASSERT_EQ((*utf8_hints)["result"].size(), 1);
    EXPECT_EQ((*utf8_hints)["result"][0]["position"]["character"], byte_column);
    const json* utf8_hover = response_with_id(utf8_messages, 3);
    ASSERT_NE(utf8_hover, nullptr);
    EXPECT_FALSE((*utf8_hover)["result"].is_null());
    const json* utf8_semantic = response_with_id(utf8_messages, 4);
    ASSERT_NE(utf8_semantic, nullptr);
    auto utf8_string = find_semantic_token(*utf8_semantic, 4, 7);
    ASSERT_TRUE(utf8_string);
    EXPECT_EQ(utf8_string->length, 8);
}

TEST_F(SmallsLSP, ProtocolCompletesInferredMembersAndRefreshesCoreDiagnostics)
{
    auto core_path = std::filesystem::weakly_canonical(
        std::filesystem::path{__FILE__}.parent_path().parent_path()
        / "lib/nw/smalls/scripts/core");
    nw::kernel::runtime().add_module_path(core_path);
    std::string uri = "file://" + (core_path / "lsp_inferred_completion.smalls").generic_string();
    constexpr std::string_view incomplete_source = R"([[propset(Door)]]
type DoorState {
    test: int;
    linked_to_flags: int;
};

fn test(d: Door) {
    var ds = get_propset!(DoorState)(d);
    ds.
})";
    constexpr std::string_view complete_source = R"([[propset(Door)]]
type DoorState {
    test: int;
    linked_to_flags: int;
};

fn test(d: Door) {
    var ds = get_propset!(DoorState)(d);
    ds.linked_to_flags;
})";

    std::stringstream input;
    input << frame_message({{"jsonrpc", "2.0"},
        {"id", 1},
        {"method", "initialize"},
        {"params", {{"capabilities", json::object()}}}});
    input << frame_message({{"jsonrpc", "2.0"},
        {"method", "textDocument/didOpen"},
        {"params", {{"textDocument", {{"uri", uri}, {"languageId", "smalls"}, {"version", 1}, {"text", incomplete_source}}}}}});
    input << frame_message({{"jsonrpc", "2.0"},
        {"id", 2},
        {"method", "textDocument/completion"},
        {"params", {{"textDocument", {{"uri", uri}}}, {"position", {{"line", 8}, {"character", 7}}}, {"context", {{"triggerKind", 2}, {"triggerCharacter", "."}}}}}});
    input << frame_message({{"jsonrpc", "2.0"},
        {"method", "textDocument/didChange"},
        {"params", {{"textDocument", {{"uri", uri}, {"version", 2}}}, {"contentChanges", json::array({{{"text", complete_source}}})}}}});

    std::stringstream output;
    run_smalls_lsp(input, output);
    auto messages = parse_messages(output.str());

    const json* completion = response_with_id(messages, 2);
    ASSERT_NE(completion, nullptr);
    ASSERT_TRUE((*completion)["result"].is_array());
    bool has_test = false;
    bool has_linked_to_flags = false;
    for (const auto& item : (*completion)["result"]) {
        has_test |= item.value("label", "") == "test";
        has_linked_to_flags |= item.value("label", "") == "linked_to_flags";
    }
    EXPECT_TRUE(has_test);
    EXPECT_TRUE(has_linked_to_flags);

    const json* initial_diagnostics = diagnostics_with_version(messages, uri, 1);
    ASSERT_NE(initial_diagnostics, nullptr);
    ASSERT_FALSE((*initial_diagnostics)["params"]["diagnostics"].empty());
    EXPECT_EQ((*initial_diagnostics)["params"]["diagnostics"][0]["message"],
        "expected identifier after '.'");

    const json* refreshed_diagnostics = diagnostics_with_version(messages, uri, 2);
    ASSERT_NE(refreshed_diagnostics, nullptr);
    EXPECT_TRUE((*refreshed_diagnostics)["params"]["diagnostics"].empty());
}
