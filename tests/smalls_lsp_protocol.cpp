#include "smalls_fixtures.hpp"

#include "../tools/smalls-lsp/lsp_uri.hpp"
#include "../tools/smalls-lsp/server.hpp"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
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

/// An inlay hint label is `string | InlayHintLabelPart[]`. Clients accept
/// either, so tests read through this rather than assuming one shape.
/// Parameter hints only. Type hints are a separate category and their presence
/// must not perturb a test about parameter positions.
std::vector<json> parameter_hints(const json& response)
{
    std::vector<json> result;
    for (const auto& hint : response["result"]) {
        if (hint.value("kind", 2) == 2) {
            result.push_back(hint);
        }
    }
    return result;
}

std::string inlay_label(const json& hint)
{
    const json& label = hint["label"];
    if (label.is_string()) {
        return label.get<std::string>();
    }
    std::string result;
    for (const auto& part : label) {
        result += part.value("value", "");
    }
    return result;
}

/// Indices into the legend the server advertises, mirroring
/// `smalls_lsp::TokenType`.
constexpr int token_type_variable = 7;
[[maybe_unused]] constexpr int token_type_function = 9;
constexpr int token_type_string = 10;
[[maybe_unused]] constexpr int token_type_number = 11;

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
    EXPECT_TRUE((*initialize)["result"]["capabilities"]["inlayHintProvider"]["resolveProvider"]);
    EXPECT_EQ((*initialize)["result"]["capabilities"]["textDocumentSync"]["change"], 2);
    EXPECT_TRUE((*initialize)["result"]["capabilities"]["documentSymbolProvider"]);
    EXPECT_TRUE((*initialize)["result"]["capabilities"]["foldingRangeProvider"]);
    EXPECT_TRUE((*initialize)["result"]["capabilities"]["selectionRangeProvider"]);
    EXPECT_EQ((*initialize)["result"]["capabilities"]["positionEncoding"], "utf-16");

    const json* hints = response_with_id(messages, 2);
    ASSERT_NE(hints, nullptr);
    ASSERT_TRUE((*hints)["result"].is_array());
    auto parameters = parameter_hints(*hints);
    ASSERT_EQ(parameters.size(), 2);
    EXPECT_EQ(inlay_label(parameters[0]), "x:");
    EXPECT_EQ(parameters[0]["kind"], 2);
    EXPECT_EQ(parameters[0]["position"]["line"], 5);
    EXPECT_EQ(inlay_label(parameters[1]), "y:");

    // The inferred type of `result` is a type hint, reported separately.
    bool has_type_hint = false;
    for (const auto& hint : (*hints)["result"]) {
        has_type_hint |= hint.value("kind", 0) == 1;
    }
    EXPECT_TRUE(has_type_hint);
}

TEST_F(SmallsLSP, ProtocolRejectsStaleChangesAndUnknownRequests)
{
    constexpr std::string_view uri = "file:///tmp/smalls_lsp_stale.smalls";
    constexpr std::string_view source = R"(fn consume(value: int) {
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
    // A range outside the document is rejected without modifying the buffer.
    input << frame_message({{"jsonrpc", "2.0"},
        {"method", "textDocument/didChange"},
        {"params", {{"textDocument", {{"uri", uri}, {"version", 6}}}, {"contentChanges", json::array({{{"range", {{"start", {{"line", 99}, {"character", 0}}}, {"end", {{"line", 99}, {"character", 0}}}}}, {"text", "junk"}}})}}}});
    // Incremental rename of the parameter, which the inlay hint below reports.
    input << frame_message({{"jsonrpc", "2.0"},
        {"method", "textDocument/didChange"},
        {"params", {{"textDocument", {{"uri", uri}, {"version", 6}}}, {"contentChanges", json::array({{{"range", {{"start", {{"line", 0}, {"character", 11}}}, {"end", {{"line", 0}, {"character", 16}}}}}, {"text", "renamed"}}})}}}});
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
    auto parameters = parameter_hints(*hints);
    ASSERT_EQ(parameters.size(), 1);
    EXPECT_EQ(inlay_label(parameters[0]), "renamed:");

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
    auto utf16_parameters = parameter_hints(*utf16_hints);
    ASSERT_EQ(utf16_parameters.size(), 1);
    EXPECT_EQ(utf16_parameters[0]["position"]["character"], utf16_column);
    const json* utf16_hover = response_with_id(utf16_messages, 3);
    ASSERT_NE(utf16_hover, nullptr);
    EXPECT_FALSE((*utf16_hover)["result"].is_null());
    const json* utf16_semantic = response_with_id(utf16_messages, 4);
    ASSERT_NE(utf16_semantic, nullptr);
    auto utf16_string = find_semantic_token(*utf16_semantic, 4, token_type_string);
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
    auto utf8_parameters = parameter_hints(*utf8_hints);
    ASSERT_EQ(utf8_parameters.size(), 1);
    EXPECT_EQ(utf8_parameters[0]["position"]["character"], byte_column);
    const json* utf8_hover = response_with_id(utf8_messages, 3);
    ASSERT_NE(utf8_hover, nullptr);
    EXPECT_FALSE((*utf8_hover)["result"].is_null());
    const json* utf8_semantic = response_with_id(utf8_messages, 4);
    ASSERT_NE(utf8_semantic, nullptr);
    auto utf8_string = find_semantic_token(*utf8_semantic, 4, token_type_string);
    ASSERT_TRUE(utf8_string);
    EXPECT_EQ(utf8_string->length, 8);
}

TEST_F(SmallsLSP, ProtocolCompletesInferredMembersAndRefreshesCoreDiagnostics)
{
    auto core_path = std::filesystem::weakly_canonical(
        std::filesystem::path{__FILE__}.parent_path().parent_path()
        / "lib/nw/smalls/scripts/core");
    nw::kernel::runtime().add_module_path(core_path);
    std::string uri = smalls_lsp::native_path_to_uri(
        (core_path / "lsp_inferred_completion.smalls").string());
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
    ASSERT_TRUE((*completion)["result"].is_object());
    EXPECT_TRUE((*completion)["result"]["isIncomplete"]);
    bool has_test = false;
    bool has_linked_to_flags = false;
    for (const auto& item : (*completion)["result"]["items"]) {
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

namespace {

/// Runs a server over one framed conversation and returns every message it
/// wrote, so a test can assert on responses and notifications together.
std::vector<json> run_conversation(const std::vector<json>& messages)
{
    std::stringstream input;
    for (const auto& message : messages) {
        input << frame_message(message);
    }
    std::stringstream output;
    run_smalls_lsp(input, output);
    return parse_messages(output.str());
}

/// Capabilities of a client that can read CodeAction literals. Without this a
/// conformant server must fall back to the legacy Command form.
json code_action_capabilities()
{
    return {{"textDocument",
        {{"codeAction",
            {{"codeActionLiteralSupport",
                {{"codeActionKind", {{"valueSet", json::array({"quickfix"})}}}}}}}}}};
}

json initialize_message(json capabilities = json::object())
{
    return {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"},
        {"params", {{"capabilities", std::move(capabilities)}}}};
}

json did_open_message(std::string_view uri, std::string_view source, int version = 1)
{
    return {{"jsonrpc", "2.0"}, {"method", "textDocument/didOpen"},
        {"params", {{"textDocument", {{"uri", uri}, {"languageId", "smalls"},
                        {"version", version}, {"text", source}}}}}};
}

json make_range(int start_line, int start_character, int end_line, int end_character)
{
    return {{"start", {{"line", start_line}, {"character", start_character}}},
        {"end", {{"line", end_line}, {"character", end_character}}}};
}

json document_request(int id, std::string_view method, std::string_view uri)
{
    return {{"jsonrpc", "2.0"}, {"id", id}, {"method", method},
        {"params", {{"textDocument", {{"uri", uri}}}}}};
}

/// Every symbol's selectionRange must sit inside its range, or VS Code drops it.
void expect_selection_within_range(const json& symbols)
{
    for (const auto& symbol : symbols) {
        const json& range = symbol["range"];
        const json& selection = symbol["selectionRange"];
        EXPECT_LE(range["start"]["line"].get<int>(), selection["start"]["line"].get<int>())
            << symbol["name"];
        EXPECT_GE(range["end"]["line"].get<int>(), selection["end"]["line"].get<int>())
            << symbol["name"];
        if (symbol.contains("children")) {
            expect_selection_within_range(symbol["children"]);
        }
    }
}

const json* find_symbol(const json& symbols, std::string_view name)
{
    for (const auto& symbol : symbols) {
        if (symbol.value("name", "") == name) {
            return &symbol;
        }
        if (symbol.contains("children")) {
            if (const json* nested = find_symbol(symbol["children"], name)) {
                return nested;
            }
        }
    }
    return nullptr;
}

} // namespace

// A request carries an id and gets exactly one response; a notification carries
// none and must get none. Indexing `req["id"]` unconditionally read past the end
// of the object under NDEBUG.
TEST_F(SmallsLSP, ProtocolIgnoresNotificationShapedRequests)
{
    constexpr std::string_view uri = "file:///tmp/smalls_lsp_notification.smalls";
    constexpr std::string_view source = "fn main() {\n    var x = 1;\n}\n";

    std::vector<json> conversation{initialize_message(), did_open_message(uri, source)};
    for (std::string_view method : {"textDocument/hover", "textDocument/definition",
             "textDocument/completion", "textDocument/signatureHelp",
             "textDocument/documentSymbol", "textDocument/foldingRange",
             "textDocument/semanticTokens/full"}) {
        conversation.push_back({{"jsonrpc", "2.0"}, {"method", method},
            {"params", {{"textDocument", {{"uri", uri}}},
                {"position", {{"line", 1}, {"character", 8}}}}}});
    }
    conversation.push_back({{"jsonrpc", "2.0"}, {"method", "textDocument/inlayHint"},
        {"params", {{"textDocument", {{"uri", uri}}},
            {"range", {{"start", {{"line", 0}, {"character", 0}}},
                {"end", {{"line", 2}, {"character", 1}}}}}}}});
    // A real request afterwards proves the server survived the notifications.
    conversation.push_back(document_request(42, "textDocument/documentSymbol", uri));

    auto messages = run_conversation(conversation);

    for (const auto& message : messages) {
        if (message.contains("id")) {
            EXPECT_FALSE(message["id"].is_null()) << message.dump();
            int id = message["id"].get<int>();
            EXPECT_TRUE(id == 1 || id == 42) << "unexpected response: " << message.dump();
        }
    }
    ASSERT_NE(response_with_id(messages, 42), nullptr);
}

TEST_F(SmallsLSP, ProtocolAnswersCancelledRequestWithoutWorking)
{
    constexpr std::string_view uri = "file:///tmp/smalls_lsp_cancel.smalls";
    constexpr std::string_view source = "fn main() {\n    var x = 1;\n}\n";

    auto messages = run_conversation({initialize_message(), did_open_message(uri, source),
        {{"jsonrpc", "2.0"}, {"method", "$/cancelRequest"}, {"params", {{"id", 7}}}},
        document_request(7, "textDocument/documentSymbol", uri),
        document_request(8, "textDocument/documentSymbol", uri)});

    const json* cancelled = response_with_id(messages, 7);
    ASSERT_NE(cancelled, nullptr);
    EXPECT_EQ((*cancelled)["error"]["code"], -32800);

    // The cancellation applies once and does not leak to later requests.
    const json* later = response_with_id(messages, 8);
    ASSERT_NE(later, nullptr);
    EXPECT_TRUE(later->contains("result"));
}

// SemanticTokenVisitor derived from NullVisitor, whose empty base visits ended
// the walk at any unhandled node. Every construct below sat behind one.
TEST_F(SmallsLSP, ProtocolTokenizesNodesThatOnceTruncatedTheWalk)
{
    constexpr std::string_view uri = "file:///tmp/smalls_lsp_traversal.smalls";
    constexpr std::string_view source = R"(type Shape = Circle(int) | Square(int);

fn classify(shape: Shape, scale: int, flags: int[4]): int {
    var total = 0;
    switch (shape) {
        case Circle(radius): {
            total = radius;
        }
        case Square(side): {
            total = side;
        }
    }
    if (total > 10 && scale < 100) {
        total = flags[0];
    }
    return total + scale;
}
)";

    auto messages = run_conversation({initialize_message(), did_open_message(uri, source),
        document_request(2, "textDocument/semanticTokens/full", uri)});

    const json* response = response_with_id(messages, 2);
    ASSERT_NE(response, nullptr);
    auto tokens = decode_semantic_tokens(*response);

    auto has_token_on_line = [&](int line) {
        return std::any_of(tokens.begin(), tokens.end(),
            [&](const DecodedSemanticToken& token) { return token.line == line; });
    };

    EXPECT_TRUE(has_token_on_line(6)) << "switch case body (SwitchStatement)";
    EXPECT_TRUE(has_token_on_line(9)) << "second case body (LabelStatement)";
    EXPECT_TRUE(has_token_on_line(12)) << "comparison and logical operands";
    EXPECT_TRUE(has_token_on_line(13)) << "index expression";
    EXPECT_TRUE(has_token_on_line(15)) << "return operand (JumpStatement)";

    // `case Circle(radius):` names a variant rather than a variable.
    auto circle_pattern = find_semantic_token(*response, 5, 4);
    EXPECT_TRUE(circle_pattern);

    // Tokens must be sorted and non-overlapping.
    for (size_t i = 1; i < tokens.size(); ++i) {
        const auto& previous = tokens[i - 1];
        const auto& current = tokens[i];
        ASSERT_TRUE(current.line > previous.line
            || (current.line == previous.line
                && current.start >= previous.start + previous.length))
            << "overlap at token " << i;
    }
}

TEST_F(SmallsLSP, ProtocolReportsHierarchicalDocumentSymbols)
{
    constexpr std::string_view uri = "file:///tmp/smalls_lsp_symbols.smalls";
    constexpr std::string_view source = R"(type Point {
    x: int;
    y: int;
};

type Shape = Circle(int) | Square(int);

fn area(p: Point): int {
    var body_local = 0;
    return p.x;
}
)";

    auto messages = run_conversation({initialize_message(), did_open_message(uri, source),
        document_request(2, "textDocument/documentSymbol", uri)});

    const json* response = response_with_id(messages, 2);
    ASSERT_NE(response, nullptr);
    const json& symbols = (*response)["result"];
    ASSERT_TRUE(symbols.is_array());

    const json* point = find_symbol(symbols, "Point");
    ASSERT_NE(point, nullptr);
    EXPECT_EQ((*point)["kind"], 23); // Struct
    ASSERT_TRUE(point->contains("children"));
    EXPECT_EQ((*point)["children"].size(), 2);
    EXPECT_EQ((*point)["children"][0]["kind"], 8); // Field

    const json* shape = find_symbol(symbols, "Shape");
    ASSERT_NE(shape, nullptr);
    EXPECT_EQ((*shape)["kind"], 10); // Enum
    ASSERT_TRUE(shape->contains("children"));
    EXPECT_EQ((*shape)["children"].size(), 2);
    EXPECT_EQ((*shape)["children"][0]["kind"], 22); // EnumMember

    const json* area = find_symbol(symbols, "area");
    ASSERT_NE(area, nullptr);
    EXPECT_EQ((*area)["kind"], 12); // Function

    // The outline describes declared shape; function bodies contribute nothing.
    EXPECT_EQ(find_symbol(symbols, "body_local"), nullptr);
    expect_selection_within_range(symbols);
}

TEST_F(SmallsLSP, ProtocolReportsFoldingAndSelectionRanges)
{
    constexpr std::string_view uri = "file:///tmp/smalls_lsp_ranges.smalls";
    constexpr std::string_view source = R"(// A comment that
// spans two lines.
fn main(): int {
    var total = 1;
    return total;
}
)";

    auto messages = run_conversation({initialize_message(), did_open_message(uri, source),
        document_request(2, "textDocument/foldingRange", uri),
        {{"jsonrpc", "2.0"}, {"id", 3}, {"method", "textDocument/selectionRange"},
            {"params", {{"textDocument", {{"uri", uri}}},
                {"positions", json::array({{{"line", 4}, {"character", 12}}})}}}}});

    const json* folding = response_with_id(messages, 2);
    ASSERT_NE(folding, nullptr);
    const json& ranges = (*folding)["result"];
    ASSERT_TRUE(ranges.is_array());

    bool has_comment_fold = false;
    bool has_body_fold = false;
    for (const auto& range : ranges) {
        // No region may collapse to a single line.
        EXPECT_LT(range["startLine"].get<int>(), range["endLine"].get<int>());
        if (range.value("kind", "") == "comment") {
            has_comment_fold = true;
        }
        if (range["startLine"] == 2 && !range.contains("kind")) {
            has_body_fold = true;
        }
    }
    EXPECT_TRUE(has_comment_fold);
    // The body folds up to the line before the closing brace, which stays visible.
    EXPECT_TRUE(has_body_fold);

    const json* selection = response_with_id(messages, 3);
    ASSERT_NE(selection, nullptr);
    ASSERT_TRUE((*selection)["result"].is_array());
    ASSERT_EQ((*selection)["result"].size(), 1);

    // The chain must strictly grow outward or expand-selection appears stuck.
    const json* node = &(*selection)["result"][0];
    ASSERT_FALSE(node->is_null());
    int previous_span = -1;
    while (node && !node->is_null()) {
        const json& range = (*node)["range"];
        int span = (range["end"]["line"].get<int>() - range["start"]["line"].get<int>()) * 1000
            + range["end"]["character"].get<int>() - range["start"]["character"].get<int>();
        EXPECT_GT(span, previous_span);
        previous_span = span;
        node = node->contains("parent") ? &(*node)["parent"] : nullptr;
    }
}

TEST_F(SmallsLSP, ProtocolAppliesIncrementalEditBatches)
{
    constexpr std::string_view uri = "file:///tmp/smalls_lsp_incremental.smalls";
    // Spelled as UTF-8 bytes, not a universal-character-name: MSVC converts a
    // UCN to the compiling machine's code page, which cannot represent U+1F600.
    constexpr std::string_view source = "fn main() {\n    var a = \"é\xF0\x9F\x98\x80\";\n    var b = 0;\n}\n";

    // Two edits in one batch, the first on a line containing multi-byte text.
    json changes = json::array(
        {{{"range", {{"start", {{"line", 1}, {"character", 8}}},
                        {"end", {{"line", 1}, {"character", 9}}}}},
             {"text", "alpha"}},
            {{"range", {{"start", {{"line", 2}, {"character", 8}}},
                 {"end", {{"line", 2}, {"character", 9}}}}},
                {"text", "beta"}}});

    auto messages = run_conversation({initialize_message(), did_open_message(uri, source),
        {{"jsonrpc", "2.0"}, {"method", "textDocument/didChange"},
            {"params", {{"textDocument", {{"uri", uri}, {"version", 2}}},
                {"contentChanges", changes}}}},
        document_request(2, "textDocument/semanticTokens/full", uri)});

    const json* response = response_with_id(messages, 2);
    ASSERT_NE(response, nullptr);
    auto tokens = decode_semantic_tokens(*response);

    auto variable_length_on_line = [&](int line) {
        for (const auto& token : tokens) {
            if (token.line == line && token.type == token_type_variable) {
                return token.length;
            }
        }
        return -1;
    };
    EXPECT_EQ(variable_length_on_line(1), 5); // alpha
    EXPECT_EQ(variable_length_on_line(2), 4); // beta
}

TEST_F(SmallsLSP, ProtocolReturnsDefinitionLinksWhenClientSupportsThem)
{
    constexpr std::string_view uri = "file:///tmp/smalls_lsp_links.smalls";
    constexpr std::string_view source = R"(fn helper(): int {
    return 1;
}

fn main(): int {
    return helper();
}
)";

    json capabilities{{"textDocument", {{"definition", {{"linkSupport", true}}}}}};
    auto messages = run_conversation({initialize_message(capabilities),
        did_open_message(uri, source),
        {{"jsonrpc", "2.0"}, {"id", 2}, {"method", "textDocument/definition"},
            {"params", {{"textDocument", {{"uri", uri}}},
                {"position", {{"line", 5}, {"character", 12}}}}}}});

    const json* response = response_with_id(messages, 2);
    ASSERT_NE(response, nullptr);
    const json& result = (*response)["result"];
    ASSERT_TRUE(result.is_array());
    ASSERT_EQ(result.size(), 1);
    EXPECT_TRUE(result[0].contains("targetUri"));
    EXPECT_TRUE(result[0].contains("targetSelectionRange"));
    EXPECT_TRUE(result[0].contains("originSelectionRange"));
    EXPECT_EQ(result[0]["targetRange"]["start"]["line"], 0);
}

TEST_F(SmallsLSP, ProtocolResolvesCompletionDetailLazily)
{
    constexpr std::string_view uri = "file:///tmp/smalls_lsp_resolve.smalls";
    constexpr std::string_view source = R"(fn documented_helper(value: int): int {
    return value;
}

fn main(): int {
    return doc
}
)";

    auto messages = run_conversation({initialize_message(), did_open_message(uri, source),
        {{"jsonrpc", "2.0"}, {"id", 2}, {"method", "textDocument/completion"},
            {"params", {{"textDocument", {{"uri", uri}}},
                {"position", {{"line", 5}, {"character", 14}}}}}}});

    const json* completion = response_with_id(messages, 2);
    ASSERT_NE(completion, nullptr);
    ASSERT_TRUE((*completion)["result"].is_object());

    const json* helper = nullptr;
    for (const auto& item : (*completion)["result"]["items"]) {
        if (item.value("label", "") == "documented_helper") {
            helper = &item;
            break;
        }
    }
    ASSERT_NE(helper, nullptr);

    // Ordering, replace range, and snippet insertion are decided up front.
    EXPECT_TRUE(helper->contains("sortText"));
    EXPECT_TRUE(helper->contains("textEdit"));
    EXPECT_EQ((*helper)["insertTextFormat"], 2);
    EXPECT_NE((*helper)["textEdit"]["newText"].get<std::string>().find("${1:value}"),
        std::string::npos);
    // Documentation is the expensive field and is deferred to resolve.
    EXPECT_FALSE(helper->contains("documentation"));
    EXPECT_TRUE(helper->contains("data"));
}

TEST_F(SmallsLSP, ProtocolReportsSymbolsAndRangesForPartiallyParsedFiles)
{
    constexpr std::string_view uri = "file:///tmp/smalls_lsp_partial.smalls";
    // The second declaration is broken; the first must still be reported.
    constexpr std::string_view source = R"(fn intact(): int {
    return 1;
}

fn broken(: {
)";

    auto messages = run_conversation({initialize_message(), did_open_message(uri, source),
        document_request(2, "textDocument/documentSymbol", uri),
        {{"jsonrpc", "2.0"}, {"id", 3}, {"method", "textDocument/selectionRange"},
            {"params", {{"textDocument", {{"uri", uri}}},
                {"positions", json::array({{{"line", 4}, {"character", 10}}})}}}}});

    const json* symbols = response_with_id(messages, 2);
    ASSERT_NE(symbols, nullptr);
    ASSERT_TRUE((*symbols)["result"].is_array());
    EXPECT_NE(find_symbol((*symbols)["result"], "intact"), nullptr)
        << "a parse failure later in the file must not erase earlier symbols";

    // A position with no node behind it answers empty rather than erroring.
    const json* selection = response_with_id(messages, 3);
    ASSERT_NE(selection, nullptr);
    EXPECT_FALSE(selection->contains("error"));

    // The syntax error itself is reported rather than suppressed.
    const json* diagnostics = diagnostics_with_version(messages, uri, 1);
    ASSERT_NE(diagnostics, nullptr);
    EXPECT_FALSE((*diagnostics)["params"]["diagnostics"].empty());
}

TEST_F(SmallsLSP, ProtocolFoldsTheImportHeader)
{
    constexpr std::string_view uri = "file:///tmp/smalls_lsp_imports.smalls";
    constexpr std::string_view source = R"(import core.math as math;
import core.string as text;

fn main(): int {
    return 1;
}
)";

    auto messages = run_conversation({initialize_message(), did_open_message(uri, source),
        document_request(2, "textDocument/foldingRange", uri)});

    const json* folding = response_with_id(messages, 2);
    ASSERT_NE(folding, nullptr);

    bool has_import_fold = false;
    for (const auto& range : (*folding)["result"]) {
        if (range.value("kind", "") == "imports") {
            has_import_fold = true;
            EXPECT_EQ(range["startLine"], 0);
            EXPECT_EQ(range["endLine"], 1);
        }
    }
    EXPECT_TRUE(has_import_fold);
}

// `linkSupport` is a per-client capability, so the shape must follow what the
// client actually advertised rather than a fixed choice.
TEST_F(SmallsLSP, ProtocolFallsBackToPlainLocationWithoutLinkSupport)
{
    constexpr std::string_view uri = "file:///tmp/smalls_lsp_plainloc.smalls";
    constexpr std::string_view source = R"(fn helper(): int {
    return 1;
}

fn main(): int {
    return helper();
}
)";

    auto messages = run_conversation({initialize_message(), did_open_message(uri, source),
        {{"jsonrpc", "2.0"}, {"id", 2}, {"method", "textDocument/definition"},
            {"params", {{"textDocument", {{"uri", uri}}},
                {"position", {{"line", 5}, {"character", 12}}}}}},
        {{"jsonrpc", "2.0"}, {"id", 3}, {"method", "textDocument/declaration"},
            {"params", {{"textDocument", {{"uri", uri}}},
                {"position", {{"line", 5}, {"character", 12}}}}}}});

    const json* definition = response_with_id(messages, 2);
    ASSERT_NE(definition, nullptr);
    const json& result = (*definition)["result"];
    ASSERT_TRUE(result.is_object()) << "expected a Location, not a LocationLink";
    EXPECT_EQ(result["uri"].get<std::string_view>(), uri);
    EXPECT_TRUE(result.contains("range"));
    EXPECT_FALSE(result.contains("targetUri"));

    const json* declaration = response_with_id(messages, 3);
    ASSERT_NE(declaration, nullptr);
    EXPECT_TRUE(declaration->contains("result"));
}

TEST_F(SmallsLSP, ProtocolReturnsHoverRangeAndTypeDefinition)
{
    constexpr std::string_view uri = "file:///tmp/smalls_lsp_hoverrange.smalls";
    constexpr std::string_view source = R"(type Point {
    x: int;
};

fn main(p: Point): int {
    return p.x;
}
)";

    auto messages = run_conversation({initialize_message(), did_open_message(uri, source),
        {{"jsonrpc", "2.0"}, {"id", 2}, {"method", "textDocument/hover"},
            {"params", {{"textDocument", {{"uri", uri}}},
                {"position", {{"line", 4}, {"character", 12}}}}}},
        {{"jsonrpc", "2.0"}, {"id", 3}, {"method", "textDocument/typeDefinition"},
            {"params", {{"textDocument", {{"uri", uri}}},
                {"position", {{"line", 5}, {"character", 12}}}}}}});

    // Without a range the editor cannot underline the hovered symbol.
    const json* hover = response_with_id(messages, 2);
    ASSERT_NE(hover, nullptr);
    ASSERT_FALSE((*hover)["result"].is_null());
    ASSERT_TRUE((*hover)["result"].contains("range"));
    const json& range = (*hover)["result"]["range"];
    EXPECT_EQ(range["start"]["line"], 4);
    EXPECT_EQ(range["end"]["line"], 4);
    EXPECT_LT(range["start"]["character"].get<int>(), range["end"]["character"].get<int>());

    // typeDefinition is advertised, so it must answer rather than error.
    const json* type_definition = response_with_id(messages, 3);
    ASSERT_NE(type_definition, nullptr);
    EXPECT_TRUE(type_definition->contains("result"));
    EXPECT_FALSE(type_definition->contains("error"));
}

// The lexer scans a raw string with `no_eol` false, so `r"..."` may cross a
// line boundary. The protocol has no multi-line token, so it must be split.
TEST_F(SmallsLSP, ProtocolSplitsMultiLineTokensPerLine)
{
    constexpr std::string_view uri = "file:///tmp/smalls_lsp_rawstring.smalls";
    constexpr std::string_view source = "fn main(): int {\n"
                                        "    var s = r\"line one\n"
                                        "line two\n"
                                        "line three\";\n"
                                        "    return 1;\n"
                                        "}\n";

    auto messages = run_conversation({initialize_message(), did_open_message(uri, source),
        document_request(2, "textDocument/semanticTokens/full", uri)});

    const json* response = response_with_id(messages, 2);
    ASSERT_NE(response, nullptr);
    auto tokens = decode_semantic_tokens(*response);

    std::vector<int> string_lines;
    for (const auto& token : tokens) {
        if (token.type == token_type_string) {
            string_lines.push_back(token.line);
        }
    }
    ASSERT_EQ(string_lines.size(), 3) << "one string token per spanned line";
    EXPECT_EQ(string_lines[0], 1);
    EXPECT_EQ(string_lines[1], 2);
    EXPECT_EQ(string_lines[2], 3);

    for (size_t i = 1; i < tokens.size(); ++i) {
        ASSERT_TRUE(tokens[i].line > tokens[i - 1].line
            || tokens[i].start >= tokens[i - 1].start + tokens[i - 1].length);
    }
}

// The client's own watcher covers the workspace only, so the server registers
// watchers for every active module path.
TEST_F(SmallsLSP, ProtocolRegistersWatchersForModulePaths)
{
    auto core_path = std::filesystem::weakly_canonical(
        std::filesystem::path{__FILE__}.parent_path().parent_path()
        / "lib/nw/smalls/scripts/core");
    nw::kernel::runtime().add_module_path(core_path);

    json capabilities{
        {"workspace", {{"didChangeWatchedFiles", {{"dynamicRegistration", true}}}}}};
    auto messages = run_conversation({initialize_message(capabilities),
        {{"jsonrpc", "2.0"}, {"method", "initialized"}, {"params", json::object()}}});

    const json* registration = nullptr;
    for (const auto& message : messages) {
        if (message.value("method", "") == "client/registerCapability") {
            registration = &message;
            break;
        }
    }
    ASSERT_NE(registration, nullptr);
    const json& entry = (*registration)["params"]["registrations"][0];
    EXPECT_EQ(entry["method"], "workspace/didChangeWatchedFiles");
    EXPECT_FALSE(entry["registerOptions"]["watchers"].empty());
    // A server-initiated request must carry an id so the client can answer it.
    EXPECT_TRUE(registration->contains("id"));
}

// A client that does not support dynamic registration must not be sent one.
TEST_F(SmallsLSP, ProtocolSkipsWatcherRegistrationWithoutDynamicSupport)
{
    auto messages = run_conversation({initialize_message(),
        {{"jsonrpc", "2.0"}, {"method", "initialized"}, {"params", json::object()}}});

    for (const auto& message : messages) {
        EXPECT_NE(message.value("method", ""), "client/registerCapability");
    }
}

// The compiler has no opinion on unused imports, so the server derives them.
// The fix keys on the diagnostic code, never on the message text.
TEST_F(SmallsLSP, ProtocolReportsAndFixesUnusedImports)
{
    auto scripts = std::filesystem::weakly_canonical(
        std::filesystem::path{__FILE__}.parent_path().parent_path() / "lib/nw/smalls/scripts");
    nw::kernel::runtime().add_module_path(scripts / "core");
    nw::kernel::runtime().add_module_path(scripts / "nwn1");

    std::string uri = smalls_lsp::native_path_to_uri(
        (scripts / "nwn1" / "lsp_unused_import_probe.smalls").string());
    constexpr std::string_view source = R"(import core.array as Array;

fn probe(a: int): int {
    return a;
}
)";

    auto messages = run_conversation({initialize_message(), did_open_message(uri, source)});

    const json* published = nullptr;
    for (const auto& message : messages) {
        if (message.value("method", "") == "textDocument/publishDiagnostics"
            && message["params"].value("uri", "") == uri) {
            published = &message;
        }
    }
    ASSERT_NE(published, nullptr);

    const json* unused = nullptr;
    for (const auto& diagnostic : (*published)["params"]["diagnostics"]) {
        if (diagnostic.value("code", "") == "unused-import") {
            unused = &diagnostic;
        }
    }
    ASSERT_NE(unused, nullptr);
    EXPECT_EQ((*unused)["severity"], 4);
    // Tag 1 is Unnecessary, which renders the import faded.
    ASSERT_TRUE(unused->contains("tags"));
    EXPECT_EQ((*unused)["tags"][0], 1);
    EXPECT_EQ((*unused)["range"]["start"]["line"], 0);

    auto action_messages = run_conversation({initialize_message(code_action_capabilities()),
        did_open_message(uri, source),
        {{"jsonrpc", "2.0"}, {"id", 2}, {"method", "textDocument/codeAction"},
            {"params", {{"textDocument", {{"uri", uri}}},
                {"range", (*unused)["range"]},
                {"context", {{"diagnostics", json::array({*unused})},
                    {"only", json::array({"quickfix"})}}}}}}});

    const json* actions = response_with_id(action_messages, 2);
    ASSERT_NE(actions, nullptr);
    ASSERT_TRUE((*actions)["result"].is_array());
    ASSERT_EQ((*actions)["result"].size(), 1);
    const json& action = (*actions)["result"][0];
    EXPECT_EQ(action["kind"], "quickfix");
    EXPECT_TRUE(action["isPreferred"]);
    // Deleting the whole line rather than the declaration avoids a blank line.
    const json& edit = action["edit"]["changes"][uri][0];
    EXPECT_EQ(edit["range"]["start"]["line"], 0);
    EXPECT_EQ(edit["range"]["end"]["line"], 1);
    EXPECT_EQ(edit["newText"], "");
}

// A client asking only for refactors must not be handed quick fixes.
TEST_F(SmallsLSP, ProtocolHonorsCodeActionOnlyFilter)
{
    constexpr std::string_view uri = "file:///tmp/smalls_lsp_onlyfilter.smalls";
    constexpr std::string_view source = "fn main(): int {\n    return 1;\n}\n";

    auto messages = run_conversation({initialize_message(), did_open_message(uri, source),
        {{"jsonrpc", "2.0"}, {"id", 2}, {"method", "textDocument/codeAction"},
            {"params", {{"textDocument", {{"uri", uri}}},
                {"range", make_range(0, 0, 0, 0)},
                {"context", {{"diagnostics", json::array()},
                    {"only", json::array({"refactor.extract"})}}}}}}});

    const json* actions = response_with_id(messages, 2);
    ASSERT_NE(actions, nullptr);
    ASSERT_TRUE((*actions)["result"].is_array());
    EXPECT_TRUE((*actions)["result"].empty());
}

namespace {

/// Counts messages the server sent for one method and URI.
int count_notifications(const std::vector<json>& messages, std::string_view method,
    std::string_view uri = {})
{
    int total = 0;
    for (const auto& message : messages) {
        if (message.value("method", "") != method) {
            continue;
        }
        if (!uri.empty() && message["params"].value("uri", "") != uri) {
            continue;
        }
        ++total;
    }
    return total;
}

json did_change_message(std::string_view uri, int version, int line, int character,
    std::string_view text)
{
    return {{"jsonrpc", "2.0"}, {"method", "textDocument/didChange"},
        {"params", {{"textDocument", {{"uri", uri}, {"version", version}}},
            {"contentChanges", json::array({{{"range", make_range(line, character, line, character)},
                {"text", text}}})}}}};
}

} // namespace

// A burst of edits must cost one analysis pass, not one per keystroke. The
// intermediate versions are superseded before they are ever analyzed.
TEST_F(SmallsLSP, ProtocolCoalescesBurstsOfEdits)
{
    constexpr std::string_view uri = "file:///tmp/smalls_lsp_coalesce.smalls";
    constexpr std::string_view source = "fn main(): int {\n    return 1;\n}\n";

    std::vector<json> conversation{initialize_message(), did_open_message(uri, source)};
    for (int i = 0; i < 6; ++i) {
        conversation.push_back(did_change_message(uri, 2 + i, 1, 4, "// x\n"));
    }

    auto messages = run_conversation(conversation);

    EXPECT_EQ(count_notifications(messages, "textDocument/publishDiagnostics", uri), 1);
    EXPECT_EQ(count_notifications(messages, "workspace/semanticTokens/refresh"), 1);

    // The one pass reported the version the client ended on.
    for (const auto& message : messages) {
        if (message.value("method", "") == "textDocument/publishDiagnostics") {
            EXPECT_EQ(message["params"]["version"], 7);
        }
    }
}

// A request has to observe analyzed state, so it drains the burst first.
TEST_F(SmallsLSP, ProtocolFlushesPendingAnalysisBeforeServicingRequests)
{
    constexpr std::string_view uri = "file:///tmp/smalls_lsp_flush.smalls";
    constexpr std::string_view source = "fn main(): int {\n    return 1;\n}\n";

    auto messages = run_conversation({initialize_message(), did_open_message(uri, source),
        did_change_message(uri, 2, 1, 4, "// a\n"),
        document_request(2, "textDocument/documentSymbol", uri),
        did_change_message(uri, 3, 1, 4, "// b\n"),
        did_change_message(uri, 4, 1, 4, "// c\n")});

    // One pass before the request, one for the trailing burst.
    EXPECT_EQ(count_notifications(messages, "textDocument/publishDiagnostics", uri), 2);

    const json* symbols = response_with_id(messages, 2);
    ASSERT_NE(symbols, nullptr);
    EXPECT_TRUE((*symbols)["result"].is_array());
}

// Closing a document mid-burst must not resurrect it during the flush.
TEST_F(SmallsLSP, ProtocolDropsQueuedAnalysisForClosedDocuments)
{
    constexpr std::string_view uri = "file:///tmp/smalls_lsp_closed.smalls";
    constexpr std::string_view source = "fn main(): int {\n    return 1;\n}\n";

    auto messages = run_conversation({initialize_message(), did_open_message(uri, source),
        did_change_message(uri, 2, 1, 4, "// a\n"),
        {{"jsonrpc", "2.0"}, {"method", "textDocument/didClose"},
            {"params", {{"textDocument", {{"uri", uri}}}}}}});

    // didClose publishes exactly one empty set to clear the editor.
    ASSERT_EQ(count_notifications(messages, "textDocument/publishDiagnostics", uri), 1);
    for (const auto& message : messages) {
        if (message.value("method", "") == "textDocument/publishDiagnostics") {
            EXPECT_TRUE(message["params"]["diagnostics"].empty());
        }
    }
}

// Codes are what tooling keys on, so every diagnostic carries one: a named
// rule where a consumer needs to recognize it, its category otherwise.
TEST_F(SmallsLSP, ProtocolPublishesDiagnosticCodesAndRelatedInformation)
{
    constexpr std::string_view uri = "file:///tmp/smalls_lsp_codes.smalls";
    constexpr std::string_view source = R"(fn probe(): int {
    var dup = 1;
    var dup = 2;
    return unknown_name;
}
)";

    auto messages = run_conversation({initialize_message(), did_open_message(uri, source)});

    const json* published = nullptr;
    for (const auto& message : messages) {
        if (message.value("method", "") == "textDocument/publishDiagnostics"
            && message["params"].value("uri", "") == uri) {
            published = &message;
        }
    }
    ASSERT_NE(published, nullptr);
    const json& diagnostics = (*published)["params"]["diagnostics"];
    ASSERT_FALSE(diagnostics.empty());

    const json* duplicate = nullptr;
    const json* unresolved = nullptr;
    for (const auto& diagnostic : diagnostics) {
        // No diagnostic may reach the client without a code.
        ASSERT_TRUE(diagnostic.contains("code")) << diagnostic.dump();
        EXPECT_FALSE(diagnostic["code"].get<std::string>().empty());
        if (diagnostic["code"] == "duplicate-declaration") {
            duplicate = &diagnostic;
        }
        if (diagnostic["code"] == "unresolved-identifier") {
            unresolved = &diagnostic;
        }
    }
    ASSERT_NE(duplicate, nullptr);
    ASSERT_NE(unresolved, nullptr);

    // "declared twice" is useless without saying where the first one is.
    ASSERT_TRUE(duplicate->contains("relatedInformation"));
    const json& related = (*duplicate)["relatedInformation"][0];
    EXPECT_EQ(related["location"]["uri"].get<std::string_view>(), uri);
    EXPECT_EQ(related["location"]["range"]["start"]["line"], 1);
    EXPECT_FALSE(related["message"].get<std::string>().empty());
}

TEST_F(SmallsLSP, ProtocolAnswersPullDiagnosticsAndReusesResults)
{
    constexpr std::string_view uri = "file:///tmp/smalls_lsp_pull.smalls";
    constexpr std::string_view source = "fn probe(): int {\n    return missing;\n}\n";

    auto messages = run_conversation({initialize_message(), did_open_message(uri, source),
        document_request(2, "textDocument/diagnostic", uri),
        {{"jsonrpc", "2.0"}, {"id", 3}, {"method", "textDocument/diagnostic"},
            {"params", {{"textDocument", {{"uri", uri}}}, {"previousResultId", "1"}}}}});

    const json* initialize = response_with_id(messages, 1);
    ASSERT_NE(initialize, nullptr);
    const json& provider = (*initialize)["result"]["capabilities"]["diagnosticProvider"];
    EXPECT_EQ(provider["identifier"], "smalls");
    EXPECT_TRUE(provider["interFileDependencies"]);
    // Not advertised: a corpus pass is dominated by config data misread as
    // source. See issues/smalls-lsp-propset-config-awareness.md.
    EXPECT_FALSE(provider["workspaceDiagnostics"]);

    const json* full = response_with_id(messages, 2);
    ASSERT_NE(full, nullptr);
    EXPECT_EQ((*full)["result"]["kind"], "full");
    EXPECT_EQ((*full)["result"]["resultId"], "1");
    EXPECT_FALSE((*full)["result"]["items"].empty());

    // A client already holding this version gets no payload back.
    const json* unchanged = response_with_id(messages, 3);
    ASSERT_NE(unchanged, nullptr);
    EXPECT_EQ((*unchanged)["result"]["kind"], "unchanged");
    EXPECT_EQ((*unchanged)["result"]["resultId"], "1");
    EXPECT_FALSE((*unchanged)["result"].contains("items"));
}

// A file reachable from several module paths takes its name from the most
// specific one. Preferring the shallowest root named core.array as
// lib.nw.smalls.scripts.core.array, which matches no native module registration
// and made every [[native]] declaration in the stdlib report as unregistered.
TEST_F(SmallsLSP, ProtocolNamesModulesFromTheMostSpecificRoot)
{
    auto repo = std::filesystem::weakly_canonical(
        std::filesystem::path{__FILE__}.parent_path().parent_path());
    auto scripts = repo / "lib/nw/smalls/scripts";

    auto& runtime = nw::kernel::runtime();
    // Order matters: the shallow root is registered first, as a workspace
    // folder would be.
    runtime.add_module_path(repo);
    runtime.add_module_path(scripts / "core");
    runtime.add_module_path(scripts / "nwn1");

    auto target = scripts / "core" / "array.smalls";
    std::string uri = smalls_lsp::native_path_to_uri(target.string());
    std::ifstream stream{target};
    std::string source{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    ASSERT_FALSE(source.empty());

    auto messages = run_conversation({initialize_message(), did_open_message(uri, source)});

    const json* published = nullptr;
    for (const auto& message : messages) {
        if (message.value("method", "") == "textDocument/publishDiagnostics"
            && message["params"].value("uri", "") == uri) {
            published = &message;
        }
    }
    ASSERT_NE(published, nullptr);

    for (const auto& diagnostic : (*published)["params"]["diagnostics"]) {
        EXPECT_EQ(diagnostic["message"].get<std::string>().find("[[native]]"),
            std::string::npos)
            << "resolved under the wrong root: " << diagnostic["message"];
        EXPECT_EQ(diagnostic["message"].get<std::string>().find("no C++ native module"),
            std::string::npos)
            << "resolved under the wrong root: " << diagnostic["message"];
    }
}

namespace {

/// A scratch package under the scripts tree, so module resolution behaves as it
/// does for real workspace files.
///
/// Files must exist before the path is registered: a module path is indexed
/// when it is added, so a file written afterwards is invisible to the runtime
/// until the registry is rebuilt.
struct ScratchPackage {
    ScratchPackage(std::string_view name,
        std::initializer_list<std::pair<std::string_view, std::string_view>> files)
    {
        auto scripts = std::filesystem::weakly_canonical(
            std::filesystem::path{__FILE__}.parent_path().parent_path()
            / "lib/nw/smalls/scripts");
        root = scripts / name;
        std::filesystem::create_directories(root);
        write("package.json", R"({"name":"probe","version":"1.0.0"})");
        for (const auto& [file_name, contents] : files) {
            write(file_name, contents);
        }

        nw::kernel::runtime().add_module_path(scripts / "core");
        nw::kernel::runtime().add_module_path(root);
    }

    ~ScratchPackage()
    {
        std::error_code ec;
        std::filesystem::remove_all(root, ec);
    }

    std::string write(std::string_view name, std::string_view contents) const
    {
        auto path = root / name;
        std::ofstream out{path};
        out << contents;
        return smalls_lsp::native_path_to_uri(path.string());
    }

    std::string uri_for(std::string_view name) const
    {
        return smalls_lsp::native_path_to_uri((root / name).string());
    }

    std::filesystem::path root;
};

const json* find_action(const json& actions, std::string_view prefix)
{
    for (const auto& action : actions) {
        if (action.value("title", "").rfind(prefix, 0) == 0) {
            return &action;
        }
    }
    return nullptr;
}

} // namespace

TEST_F(SmallsLSP, ProtocolOffersSuggestionAndImportFixes)
{
    constexpr std::string_view source = "fn probe(): int {\n    return unique_helper_fn();\n}\n";
    ScratchPackage package{"lsp_ca_probe",
        {{"helpers.smalls", "fn unique_helper_fn(): int {\n    return 7;\n}\n"},
            {"main.smalls", source}}};
    std::string uri = package.uri_for("main.smalls");

    // The helper module must be loadable from disk, or the export index has
    // nothing to find.
    ASSERT_NE(nw::kernel::runtime().get_module("lsp_ca_probe.helpers"), nullptr);

    json capabilities = code_action_capabilities();
    auto messages = run_conversation({initialize_message(capabilities),
        did_open_message(uri, source)});

    const json* unresolved = nullptr;
    for (const auto& message : messages) {
        if (message.value("method", "") != "textDocument/publishDiagnostics"
            || message["params"].value("uri", "") != uri) {
            continue;
        }
        for (const auto& diagnostic : message["params"]["diagnostics"]) {
            if (diagnostic.value("code", "") == "unresolved-identifier") {
                unresolved = &diagnostic;
            }
        }
    }
    ASSERT_NE(unresolved, nullptr);

    auto action_messages = run_conversation({initialize_message(capabilities),
        did_open_message(uri, source),
        {{"jsonrpc", "2.0"}, {"id", 2}, {"method", "textDocument/codeAction"},
            {"params", {{"textDocument", {{"uri", uri}}}, {"range", (*unresolved)["range"]},
                {"context", {{"diagnostics", json::array({*unresolved})},
                    {"only", json::array({"quickfix"})}}}}}}});

    const json* response = response_with_id(action_messages, 2);
    ASSERT_NE(response, nullptr);
    const json* import_action = find_action((*response)["result"], "Import");
    ASSERT_NE(import_action, nullptr) << (*response)["result"].dump();
    EXPECT_EQ((*import_action)["kind"], "quickfix");

    const json& edit = (*import_action)["edit"]["changes"][uri][0];
    EXPECT_EQ(edit["range"]["start"]["line"], 0) << "no import block, so insert at the top";
    EXPECT_NE(edit["newText"].get<std::string>().find("unique_helper_fn"), std::string::npos);
}

// The whole point of keying on codes is that wording can change freely. A fix
// that matched on message text would stop firing here.
TEST_F(SmallsLSP, ProtocolCodeActionsIgnoreDiagnosticMessageText)
{
    constexpr std::string_view source = "import core.array as Array;\n\nfn probe(): int {\n    return 1;\n}\n";
    ScratchPackage package{"lsp_ca_wording", {{"main.smalls", source}}};
    std::string uri = package.uri_for("main.smalls");

    auto messages = run_conversation({initialize_message(), did_open_message(uri, source)});

    json unused;
    for (const auto& message : messages) {
        if (message.value("method", "") != "textDocument/publishDiagnostics"
            || message["params"].value("uri", "") != uri) {
            continue;
        }
        for (const auto& diagnostic : message["params"]["diagnostics"]) {
            if (diagnostic.value("code", "") == "unused-import") {
                unused = diagnostic;
            }
        }
    }
    ASSERT_FALSE(unused.is_null());

    // Rewrite the message to something the server never produces.
    unused["message"] = "totally different wording that no fix should match on";

    auto action_messages = run_conversation({initialize_message(code_action_capabilities()),
        did_open_message(uri, source),
        {{"jsonrpc", "2.0"}, {"id", 2}, {"method", "textDocument/codeAction"},
            {"params", {{"textDocument", {{"uri", uri}}}, {"range", unused["range"]},
                {"context", {{"diagnostics", json::array({unused})},
                    {"only", json::array({"quickfix"})}}}}}}});

    const json* response = response_with_id(action_messages, 2);
    ASSERT_NE(response, nullptr);
    EXPECT_NE(find_action((*response)["result"], "Remove unused import"), nullptr)
        << "the fix must key on the code, not the message";
}

TEST_F(SmallsLSP, ProtocolFillsMissingStructFields)
{
    constexpr std::string_view source = R"(type Point {
    x: int;
    y: int;
    label: string;
};

fn probe(): int {
    var p = Point{ x = 1 };
    return p.x;
}
)";
    ScratchPackage package{"lsp_ca_fields", {{"main.smalls", source}}};
    std::string uri = package.uri_for("main.smalls");

    // An empty range, exactly as an editor sends the cursor position.
    auto messages = run_conversation({initialize_message(code_action_capabilities()),
        did_open_message(uri, source),
        {{"jsonrpc", "2.0"}, {"id", 2}, {"method", "textDocument/codeAction"},
            {"params", {{"textDocument", {{"uri", uri}}}, {"range", make_range(7, 20, 7, 20)},
                {"context", {{"diagnostics", json::array()},
                    {"only", json::array({"quickfix"})}}}}}}});

    const json* response = response_with_id(messages, 2);
    ASSERT_NE(response, nullptr);
    const json* action = find_action((*response)["result"], "Add missing fields");
    ASSERT_NE(action, nullptr) << (*response)["result"].dump();

    const json& edit = (*action)["edit"]["changes"][uri][0];
    auto inserted = edit["newText"].get<std::string>();
    // The separator lands next to the value, not after the padding before `}`.
    EXPECT_EQ(inserted, ", y = 0, label = \"\"");
    EXPECT_EQ(edit["range"]["start"], edit["range"]["end"]) << "an insertion, not a replacement";
}

// A client that cannot read a CodeAction only understands the legacy form.
TEST_F(SmallsLSP, ProtocolFallsBackToCommandsWithoutLiteralSupport)
{
    constexpr std::string_view source = "import core.array as Array;\n\nfn probe(): int {\n    return 1;\n}\n";
    ScratchPackage package{"lsp_ca_command", {{"main.smalls", source}}};
    std::string uri = package.uri_for("main.smalls");

    auto messages = run_conversation({initialize_message(), did_open_message(uri, source)});
    json unused;
    for (const auto& message : messages) {
        if (message.value("method", "") != "textDocument/publishDiagnostics"
            || message["params"].value("uri", "") != uri) {
            continue;
        }
        for (const auto& diagnostic : message["params"]["diagnostics"]) {
            if (diagnostic.value("code", "") == "unused-import") {
                unused = diagnostic;
            }
        }
    }
    ASSERT_FALSE(unused.is_null());

    auto action_messages = run_conversation({initialize_message(),
        did_open_message(uri, source),
        {{"jsonrpc", "2.0"}, {"id", 2}, {"method", "textDocument/codeAction"},
            {"params", {{"textDocument", {{"uri", uri}}}, {"range", unused["range"]},
                {"context", {{"diagnostics", json::array({unused})},
                    {"only", json::array({"quickfix"})}}}}}}});

    const json* response = response_with_id(action_messages, 2);
    ASSERT_NE(response, nullptr);
    ASSERT_FALSE((*response)["result"].empty());
    const json& command = (*response)["result"][0];
    EXPECT_TRUE(command.contains("command"));
    EXPECT_FALSE(command.contains("kind")) << "a Command carries no CodeAction fields";
}

TEST_F(SmallsLSP, ProtocolReportsInlayHintKindsAndResolvesTooltips)
{
    constexpr std::string_view source = R"(fn add(count: int, label: string): int {
    return count;
}

fn probe(): int {
    var inferred = add(3, "x");
    return inferred;
}
)";
    ScratchPackage package{"lsp_ih_probe", {{"main.smalls", source}}};
    std::string uri = package.uri_for("main.smalls");

    auto messages = run_conversation({initialize_message(), did_open_message(uri, source),
        {{"jsonrpc", "2.0"}, {"id", 2}, {"method", "textDocument/inlayHint"},
            {"params", {{"textDocument", {{"uri", uri}}},
                {"range", make_range(0, 0, 8, 0)}}}}});

    const json* initialize = response_with_id(messages, 1);
    ASSERT_NE(initialize, nullptr);
    // Advertised as an object, not `true`, so tooltips can be deferred.
    EXPECT_TRUE((*initialize)["result"]["capabilities"]["inlayHintProvider"]["resolveProvider"]);

    const json* hints = response_with_id(messages, 2);
    ASSERT_NE(hints, nullptr);
    const json& items = (*hints)["result"];
    ASSERT_FALSE(items.empty());

    const json* type_hint = nullptr;
    const json* parameter_hint = nullptr;
    for (const auto& hint : items) {
        if (hint["kind"] == 1) {
            type_hint = &hint;
        } else if (hint["kind"] == 2) {
            parameter_hint = &hint;
        }
    }
    ASSERT_NE(type_hint, nullptr) << "inferred var type";
    ASSERT_NE(parameter_hint, nullptr) << "parameter name";

    // A type hint reads as `: T` after the name; a parameter hint reads as
    // `name:` before the argument.
    EXPECT_EQ(inlay_label(*type_hint).rfind(": ", 0), 0u);
    EXPECT_NE(inlay_label(*parameter_hint).find(':'), std::string::npos);

    // A parameter hint points at the parameter it names, which is what makes it
    // clickable rather than inert text.
    ASSERT_TRUE(parameter_hint->at("label").is_array());
    ASSERT_TRUE((*parameter_hint)["label"][0].contains("location"));
    EXPECT_EQ((*parameter_hint)["label"][0]["location"]["uri"].get<std::string_view>(), uri);

    // Tooltips are deferred to resolve.
    EXPECT_FALSE(parameter_hint->contains("tooltip"));

    auto resolved = run_conversation({initialize_message(), did_open_message(uri, source),
        {{"jsonrpc", "2.0"}, {"id", 3}, {"method", "inlayHint/resolve"},
            {"params", *parameter_hint}}});
    const json* resolve_response = response_with_id(resolved, 3);
    ASSERT_NE(resolve_response, nullptr);
    EXPECT_TRUE((*resolve_response)["result"].contains("tooltip"));
}

// The annotation beside a completion is built from the symbol's resolved type,
// not from a slice of source text. Slicing depends on knowing which script
// declared the symbol, and a wrong guess yields text from an unrelated
// declaration in the file being edited.
TEST_F(SmallsLSP, ProtocolCompletionDetailComesFromTheResolvedType)
{
    auto scripts = std::filesystem::weakly_canonical(
        std::filesystem::path{__FILE__}.parent_path().parent_path()
        / "lib/nw/smalls/scripts");
    nw::kernel::runtime().add_module_path(scripts / "core");
    nw::kernel::runtime().add_module_path(scripts / "nwn1");

    auto target = scripts / "nwn1" / "item.smalls";
    std::ifstream stream{target};
    std::string source{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
    ASSERT_FALSE(source.empty());

    // Insert a member access on a propset local, whose struct lives in another
    // module and is reached through the runtime type table.
    constexpr std::string_view anchor = "var stats = get_propset!(ItemStats)(item_obj);";
    size_t anchor_offset = source.find(anchor);
    ASSERT_NE(anchor_offset, std::string::npos);
    size_t line_end = source.find('\n', anchor_offset);
    ASSERT_NE(line_end, std::string::npos);
    source.insert(line_end + 1, "    stats.\n");

    int probe_line = static_cast<int>(
        std::count(source.begin(), source.begin() + static_cast<long>(line_end) + 1, '\n'));
    std::string uri = smalls_lsp::native_path_to_uri(target.string());

    json completion{{"jsonrpc", "2.0"}, {"id", 2}, {"method", "textDocument/completion"},
        {"params", {{"textDocument", {{"uri", uri}}}, {"position", {{"line", probe_line}, {"character", 10}}}, {"context", {{"triggerKind", 2}, {"triggerCharacter", "."}}}}}};

    auto messages = run_conversation({initialize_message(), did_open_message(uri, source),
        completion});
    const json* response = response_with_id(messages, 2);
    ASSERT_NE(response, nullptr);
    const json& items = (*response)["result"]["items"];
    ASSERT_FALSE(items.empty());

    auto resolved = run_conversation({initialize_message(), did_open_message(uri, source),
        completion,
        {{"jsonrpc", "2.0"}, {"id", 3}, {"method", "completionItem/resolve"},
            {"params", items[0]}}});
    const json* detail_response = response_with_id(resolved, 3);
    ASSERT_NE(detail_response, nullptr);

    ASSERT_TRUE((*detail_response)["result"].contains("detail"));
    auto detail = (*detail_response)["result"]["detail"].get<std::string>();
    auto label = items[0]["label"].get<std::string>();

    // A type name, not a stray line of the file being edited.
    EXPECT_EQ(detail.find('\n'), std::string::npos) << detail;
    EXPECT_EQ(detail.find("fn "), std::string::npos) << detail;
    EXPECT_FALSE(detail.empty());
    EXPECT_NE(detail, label);
}
