#include "server.hpp"

#include "ast_providers.hpp"
#include "lsp_text.hpp"
#include "lsp_uri.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/smalls/AstLocator.hpp>
#include <nw/smalls/Smalls.hpp>
#include <nw/smalls/runtime.hpp>

#include <absl/container/flat_hash_map.h>
#include <absl/container/flat_hash_set.h>
#include <absl/container/node_hash_map.h>

#include <fmt/core.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <istream>
#include <limits>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

using json = nlohmann::json;

namespace lang = nw::smalls;

namespace {

// == JSON-RPC codes ==========================================================

constexpr int error_parse = -32700;
constexpr int error_invalid_request = -32600;
constexpr int error_method_not_found = -32601;
constexpr int error_invalid_params = -32602;
constexpr int error_server_not_initialized = -32002;
constexpr int error_request_cancelled = -32800;

/// Stable diagnostic code a quick fix keys on.
constexpr const char* unused_import_code = "unused-import";

/// `window/logMessage` levels.
enum class LogLevel : int {
    error = 1,
    warning = 2,
    info = 3,
    log = 4,
};

// == JSON helpers ============================================================

const json* find_json_value(const json& value, std::initializer_list<const char*> path)
{
    const json* current = &value;
    for (const char* name : path) {
        if (!current->is_object()) {
            return nullptr;
        }
        auto it = current->find(name);
        if (it == current->end()) {
            return nullptr;
        }
        current = &*it;
    }
    return current;
}

std::optional<std::string> json_string(const json& value, std::initializer_list<const char*> path)
{
    const json* item = find_json_value(value, path);
    if (!item || !item->is_string()) {
        return std::nullopt;
    }
    return item->get<std::string>();
}

std::optional<int64_t> json_integer(const json& value, std::initializer_list<const char*> path)
{
    const json* item = find_json_value(value, path);
    if (!item || !item->is_number_integer()) {
        return std::nullopt;
    }

    try {
        return item->get<int64_t>();
    } catch (const json::exception&) {
        return std::nullopt;
    }
}

bool json_bool(const json& value, std::initializer_list<const char*> path)
{
    const json* item = find_json_value(value, path);
    return item && item->is_boolean() && item->get<bool>();
}

/// A JSON-RPC message id.
///
/// A request carries one and must receive exactly one response; a notification
/// carries none and must receive none. Extracting it once at dispatch is what
/// keeps handlers from indexing a key that may not exist -- `operator[]` on a
/// const `json` asserts, which compiles out under NDEBUG and then dereferences
/// the object's end iterator.
struct RequestId {
    json value;
    bool present = false;

    static RequestId from(const json& message)
    {
        // Goes through find_json_value rather than an iterator so the null
        // check is on a pointer the compiler can see, instead of a dereference
        // guarded by an iterator comparison it cannot prove.
        const json* id = find_json_value(message, {"id"});
        if (!id || !(id->is_number_integer() || id->is_string())) {
            return {};
        }
        return RequestId{*id, true};
    }
};

// == Position conversion =====================================================

std::optional<json> make_lsp_position(
    lang::SourcePosition position, std::string_view text, PositionEncoding encoding)
{
    if (position.line == 0) {
        return std::nullopt;
    }

    size_t source_line = position.line - 1;
    if (source_line > static_cast<size_t>(std::numeric_limits<int>::max())
        || position.column > static_cast<size_t>(std::numeric_limits<int>::max())) {
        return std::nullopt;
    }

    int line = static_cast<int>(source_line);
    auto character = byte_character_to_lsp(
        text, line, static_cast<int>(position.column), encoding);
    if (!character) {
        return std::nullopt;
    }
    return json{{"line", line}, {"character", *character}};
}

std::optional<json> make_lsp_range(
    lang::SourceRange range, std::string_view text, PositionEncoding encoding)
{
    auto start = make_lsp_position(range.start, text, encoding);
    auto end = make_lsp_position(range.end, text, encoding);
    if (!start || !end) {
        return std::nullopt;
    }
    return json{{"start", std::move(*start)}, {"end", std::move(*end)}};
}

json make_line_range(int start_line, int start_character, int end_line, int end_character)
{
    return json{{"start", {{"line", start_line}, {"character", start_character}}},
        {"end", {{"line", end_line}, {"character", end_character}}}};
}

struct TextDocumentPosition {
    std::string uri;
    int line = 0;
    int character = 0;
};

std::optional<TextDocumentPosition> text_document_position(const json& request)
{
    auto uri = json_string(request, {"params", "textDocument", "uri"});
    auto line = json_integer(request, {"params", "position", "line"});
    auto character = json_integer(request, {"params", "position", "character"});
    if (!uri || !line || !character
        || *line < 0 || *line > std::numeric_limits<int>::max()
        || *character < 0 || *character > std::numeric_limits<int>::max()) {
        return std::nullopt;
    }

    return TextDocumentPosition{
        std::move(*uri),
        static_cast<int>(*line),
        static_cast<int>(*character)};
}

std::optional<lang::SourceRange> source_range(const json& request)
{
    auto start_line = json_integer(request, {"params", "range", "start", "line"});
    auto start_character = json_integer(request, {"params", "range", "start", "character"});
    auto end_line = json_integer(request, {"params", "range", "end", "line"});
    auto end_character = json_integer(request, {"params", "range", "end", "character"});
    if (!start_line || !start_character || !end_line || !end_character
        || *start_line < 0 || *start_character < 0 || *end_line < 0 || *end_character < 0
        || *start_line >= std::numeric_limits<int>::max()
        || *start_character > std::numeric_limits<int>::max()
        || *end_line >= std::numeric_limits<int>::max()
        || *end_character > std::numeric_limits<int>::max()
        || std::tie(*start_line, *start_character) > std::tie(*end_line, *end_character)) {
        return std::nullopt;
    }

    return lang::SourceRange{
        {static_cast<size_t>(*start_line + 1), static_cast<size_t>(*start_character)},
        {static_cast<size_t>(*end_line + 1), static_cast<size_t>(*end_character)}};
}

} // namespace

// == URI Utilities ===========================================================

/// Resolves a document URI to a filesystem path.
///
/// Returns nullopt when the URI is not a `file` URI this platform can express,
/// which callers treat as "not a path on disk" rather than guessing.
std::optional<std::filesystem::path> uri_to_path(const std::string& uri)
{
    auto native = smalls_lsp::uri_to_native_path(uri);
    if (!native) {
        return std::nullopt;
    }
    return std::filesystem::path{*native};
}

std::string path_to_uri(const std::filesystem::path& path)
{
    return smalls_lsp::native_path_to_uri(path.string());
}

bool path_is_within(const std::filesystem::path& child, const std::filesystem::path& parent)
{
    auto child_it = child.begin();
    auto parent_it = parent.begin();

    for (; parent_it != parent.end(); ++parent_it, ++child_it) {
        if (child_it == child.end() || *child_it != *parent_it) {
            return false;
        }
    }
    return true;
}

std::filesystem::path canonical_or_normalized(const std::filesystem::path& p)
{
    std::error_code ec;
    auto canonical = std::filesystem::weakly_canonical(p, ec);
    if (ec) {
        return p.lexically_normal();
    }
    return canonical;
}

std::filesystem::path module_effective_root(const std::filesystem::path& module_path)
{
    std::error_code ec;
    if (std::filesystem::exists(module_path / "package.json", ec)) {
        return module_path.parent_path();
    }
    return module_path;
}

std::string module_name_for_uri(const lang::Runtime& rt, const std::string& uri)
{
    auto file_path = uri_to_path(uri);
    if (!file_path) {
        // Not a path on disk, so the URI itself is the only stable module name.
        return uri;
    }
    std::filesystem::path canonical_file = canonical_or_normalized(*file_path);

    // The most specific root wins. A file reachable from both a workspace root
    // and the package directory beneath it is `core.array`, not
    // `lib.nw.smalls.scripts.core.array`: the deeper root is the one imports
    // and the native module registry name it by, and picking the shallower one
    // makes every [[native]] declaration look unregistered.
    std::string best_module_name;
    size_t best_root_depth = 0;

    for (const auto& module_path : rt.module_paths()) {
        std::filesystem::path canonical_module_path = canonical_or_normalized(module_path);
        std::filesystem::path effective_root = module_effective_root(canonical_module_path);

        if (!path_is_within(canonical_file, effective_root)) {
            continue;
        }

        auto module_name = rt.path_to_module_name(canonical_file, effective_root);
        if (module_name.empty()) {
            continue;
        }

        auto depth = static_cast<size_t>(
            std::distance(effective_root.begin(), effective_root.end()));
        if (best_module_name.empty() || depth > best_root_depth) {
            best_module_name = std::string(module_name);
            best_root_depth = depth;
        }
    }

    if (!best_module_name.empty()) {
        return best_module_name;
    }

    return uri;
}

std::filesystem::path module_root_for_uri(const std::string& uri)
{
    auto file_path = uri_to_path(uri);
    if (!file_path) {
        return {};
    }
    std::filesystem::path canonical_file = canonical_or_normalized(*file_path);
    std::filesystem::path dir = canonical_file.parent_path();

    std::error_code ec;
    while (!dir.empty()) {
        if (std::filesystem::exists(dir / "package.json", ec)) {
            return dir;
        }
        if (dir == dir.root_path()) {
            break;
        }
        dir = dir.parent_path();
    }

    return {};
}

// == LSP Server ==============================================================

namespace {

struct LspServer {
    struct OpenDocument {
        std::string text;
        int64_t version = 0;
        /// Module names this document imported at its last successful compile.
        /// Used to republish only the open documents an edit can affect.
        std::vector<std::string> dependencies;
    };

    /// Cached semantic tokens, kept so a delta request can diff against them.
    struct TokenSnapshot {
        std::string result_id;
        std::vector<int> data;
    };

    LspServer(std::istream& input, std::ostream& output, InputPendingFn input_pending)
        : input_{input}
        , output_{output}
        , input_pending_{std::move(input_pending)}
    {
    }

    absl::node_hash_map<std::string, OpenDocument> open_documents;

    // -- Logging -------------------------------------------------------------

    void log(LogLevel level, std::string_view message)
    {
        if (!initialized_ || static_cast<int>(level) > static_cast<int>(log_level_)) {
            return;
        }
        send_notification("window/logMessage",
            {{"type", static_cast<int>(level)}, {"message", std::string{message}}});
    }

    // -- Document lookup helpers ---------------------------------------------

    std::optional<TextDocumentPosition> open_document_position(const json& request) const
    {
        auto position = text_document_position(request);
        if (!position) {
            return std::nullopt;
        }

        auto document = open_documents.find(position->uri);
        if (document == open_documents.end()) {
            return std::nullopt;
        }

        auto byte_character = lsp_character_to_byte(document->second.text,
            position->line, position->character, position_encoding_);
        if (!byte_character) {
            return std::nullopt;
        }
        position->character = *byte_character;
        return position;
    }

    std::optional<lang::SourceRange> open_document_range(
        const json& request, const std::string& uri) const
    {
        auto range = source_range(request);
        auto document = open_documents.find(uri);
        if (!range || document == open_documents.end()) {
            return std::nullopt;
        }

        int start_line = static_cast<int>(range->start.line - 1);
        int end_line = static_cast<int>(range->end.line - 1);
        auto start_character = lsp_character_to_byte(document->second.text,
            start_line, static_cast<int>(range->start.column), position_encoding_);
        auto end_character = lsp_character_to_byte(document->second.text,
            end_line, static_cast<int>(range->end.column), position_encoding_);
        if (!start_character || !end_character) {
            return std::nullopt;
        }

        range->start.column = static_cast<size_t>(*start_character);
        range->end.column = static_cast<size_t>(*end_character);
        return range;
    }

    /// Records which document a compiled script came from.
    void remember_module_uri(const lang::Script* script, const std::string& uri)
    {
        module_uris_[std::string(script->name())] = uri;
    }

    /// Resolves a URI to its module name, caching the filesystem work.
    ///
    /// `module_name_for_uri` canonicalizes the target and every module path, so
    /// calling it per request puts several stat calls on the keystroke path.
    const std::string& cached_module_name(lang::Runtime& rt, const std::string& uri)
    {
        size_t generation = rt.module_paths().size();
        if (generation != module_path_generation_) {
            module_names_.clear();
            module_path_generation_ = generation;
        }

        auto it = module_names_.find(uri);
        if (it != module_names_.end()) {
            return it->second;
        }
        return module_names_.emplace(uri, module_name_for_uri(rt, uri)).first->second;
    }

    void negotiate_position_encoding(const json& request)
    {
        position_encoding_ = PositionEncoding::utf16;
        const json* encodings = find_json_value(
            request, {"params", "capabilities", "general", "positionEncodings"});
        if (!encodings || !encodings->is_array()) {
            return;
        }

        for (const auto& encoding : *encodings) {
            if (encoding.is_string() && encoding.get_ref<const std::string&>() == "utf-8") {
                position_encoding_ = PositionEncoding::utf8;
                return;
            }
        }
    }

    std::string_view position_encoding_name() const noexcept
    {
        return position_encoding_ == PositionEncoding::utf8 ? "utf-8" : "utf-16";
    }

    // -- Transport -----------------------------------------------------------

    enum class FrameReadResult {
        message,
        end_of_stream,
        invalid,
    };

    FrameReadResult read_message(std::vector<char>& buffer)
    {
        constexpr std::string_view content_length_prefix = "Content-Length: ";
        std::optional<size_t> content_length;
        bool saw_header = false;
        bool header_complete = false;
        std::string header;

        while (std::getline(input_, header)) {
            saw_header = true;
            if (header == "\r") {
                header_complete = true;
                break;
            }
            if (!header.starts_with(content_length_prefix)) {
                continue;
            }
            if (content_length) {
                std::cerr << "LSP Frame Error: duplicate Content-Length" << std::endl;
                return FrameReadResult::invalid;
            }

            std::string_view value{header};
            value.remove_prefix(content_length_prefix.size());
            if (!value.empty() && value.back() == '\r') {
                value.remove_suffix(1);
            }

            size_t length = 0;
            auto [end, error] = std::from_chars(
                value.data(), value.data() + value.size(), length);
            if (error != std::errc{} || end != value.data() + value.size()
                || length == 0
                || length > static_cast<size_t>(std::numeric_limits<std::streamsize>::max())) {
                std::cerr << "LSP Frame Error: invalid Content-Length" << std::endl;
                return FrameReadResult::invalid;
            }
            content_length = length;
        }

        if (!saw_header && input_.eof()) {
            return FrameReadResult::end_of_stream;
        }
        if (!header_complete || !content_length) {
            std::cerr << "LSP Frame Error: incomplete header" << std::endl;
            return FrameReadResult::invalid;
        }

        try {
            buffer.resize(*content_length);
        } catch (const std::exception&) {
            std::cerr << "LSP Frame Error: message is too large" << std::endl;
            return FrameReadResult::invalid;
        }

        input_.read(buffer.data(), static_cast<std::streamsize>(*content_length));
        if (input_.gcount() != static_cast<std::streamsize>(*content_length)) {
            std::cerr << "LSP Frame Error: truncated message" << std::endl;
            return FrameReadResult::invalid;
        }
        return FrameReadResult::message;
    }

    lang::Script* get_or_load_module(lang::Runtime& rt, const std::string& module_name,
        const std::string& uri)
    {
        // Files open in the editor always go through load_module_from_source: it
        // returns the cached module when one is present and otherwise compiles
        // from the buffer, so an open buffer never loses to the on-disk copy.
        auto it = open_documents.find(uri);
        if (it != open_documents.end()) {
            lang::Script* script = rt.load_module_from_source(module_name, it->second.text);
            if (script) { remember_module_uri(script, uri); }
            return script;
        }
        return rt.get_module(module_name);
    }

    void run()
    {
#ifdef _WIN32
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
#endif

        std::vector<char> buffer;
        while (!exit_requested_) {
            FrameReadResult frame = read_message(buffer);
            if (frame != FrameReadResult::message) {
                break;
            }

            json request;
            try {
                request = json::parse(buffer.begin(), buffer.end());
            } catch (const std::exception& e) {
                std::cerr << "LSP Parse Error: " << e.what() << std::endl;
                send_error(RequestId{json(nullptr), true}, error_parse, "Parse error");
                continue;
            }

            RequestId id = RequestId::from(request);
            try {
                handle_message(request, id);
            } catch (const std::exception& e) {
                log(LogLevel::error, fmt::format("request failed: {}", e.what()));
                send_error(id, error_invalid_params, "Invalid params");
            }

            // Drain check: only analyze once the client has stopped talking.
            if (!input_pending()) {
                flush_pending_analysis();
            }
        }

        // The stream ended mid-burst; the last edit still deserves diagnostics.
        flush_pending_analysis();
    }

    // -- Output --------------------------------------------------------------

    void write_message(const json& message)
    {
        std::string body = message.dump();
        output_ << "Content-Length: " << body.length() << "\r\n\r\n"
                << body << std::flush;
    }

    void send_response(const RequestId& id, json result)
    {
        if (!id.present) {
            return;
        }
        write_message({{"jsonrpc", "2.0"}, {"id", id.value}, {"result", std::move(result)}});
    }

    /// Sends an error only when the message was a request. A notification gets
    /// no reply, and a reply carrying `id: null` is not valid JSON-RPC.
    void send_error(const RequestId& id, int code, std::string_view message)
    {
        if (!id.present) {
            return;
        }
        write_message({{"jsonrpc", "2.0"}, {"id", id.value},
            {"error", {{"code", code}, {"message", std::string{message}}}}});
    }

    void send_notification(std::string method, json params)
    {
        write_message({{"jsonrpc", "2.0"}, {"method", std::move(method)},
            {"params", std::move(params)}});
    }

    // -- Dispatch ------------------------------------------------------------

    /// Answers a request the client already cancelled without doing its work.
    bool consume_cancellation(const RequestId& id)
    {
        if (!id.present) {
            return false;
        }
        auto it = cancelled_.find(id.value.dump());
        if (it == cancelled_.end()) {
            return false;
        }
        cancelled_.erase(it);
        send_error(id, error_request_cancelled, "Request cancelled");
        return true;
    }

    void handle_message(const json& req, const RequestId& id)
    {
        auto protocol = json_string(req, {"jsonrpc"});
        auto method = json_string(req, {"method"});

        // A message with an id but no method is a response to a server request.
        // Nothing is outstanding, so drop it rather than answering it.
        if (!method) {
            if (!protocol || *protocol != "2.0" || !req.contains("result")) {
                send_error(id, error_invalid_request, "Invalid request");
            }
            return;
        }
        if (!protocol || *protocol != "2.0") {
            send_error(id, error_invalid_request, "Invalid request");
            return;
        }

        if (*method == "exit") {
            exit_requested_ = true;
            return;
        }

        if (*method == "$/cancelRequest") {
            const json* cancel_id = find_json_value(req, {"params", "id"});
            if (cancel_id && (cancel_id->is_number_integer() || cancel_id->is_string())) {
                cancelled_.insert(cancel_id->dump());
            }
            return;
        }

        if (shutdown_requested_) {
            send_error(id, error_invalid_request, "Server has shut down");
            return;
        }

        if (!initialized_ && *method != "initialize") {
            send_error(id, error_server_not_initialized, "Server not initialized");
            return;
        }

        if (consume_cancellation(id)) {
            return;
        }

        // A request must see analyzed state, so a pending burst is drained
        // before it is serviced rather than after.
        if (id.present && *method != "initialize") {
            flush_pending_analysis();
        }

        if (*method == "initialize") {
            handle_initialize(req, id);
        } else if (*method == "initialized") {
            register_module_watchers();
        } else if (*method == "$/setTrace") {
            set_trace(req);
        } else if (*method == "shutdown") {
            shutdown_requested_ = true;
            send_response(id, nullptr);
        } else if (*method == "textDocument/didOpen") {
            handle_did_open(req);
        } else if (*method == "textDocument/didChange") {
            handle_did_change(req);
        } else if (*method == "textDocument/didClose") {
            handle_did_close(req);
        } else if (*method == "textDocument/hover") {
            handle_hover(req, id);
        } else if (*method == "textDocument/definition"
            || *method == "textDocument/declaration") {
            handle_definition(req, id, false);
        } else if (*method == "textDocument/typeDefinition") {
            handle_definition(req, id, true);
        } else if (*method == "textDocument/completion") {
            handle_completion(req, id);
        } else if (*method == "completionItem/resolve") {
            handle_completion_resolve(req, id);
        } else if (*method == "textDocument/signatureHelp") {
            handle_signature_help(req, id);
        } else if (*method == "textDocument/inlayHint") {
            handle_inlay_hints(req, id);
        } else if (*method == "inlayHint/resolve") {
            handle_inlay_hint_resolve(req, id);
        } else if (*method == "workspace/diagnostic") {
            handle_workspace_diagnostic(req, id);
        } else if (*method == "textDocument/diagnostic") {
            handle_document_diagnostic(req, id);
        } else if (*method == "textDocument/codeAction") {
            handle_code_action(req, id);
        } else if (*method == "textDocument/documentSymbol") {
            handle_document_symbols(req, id);
        } else if (*method == "textDocument/foldingRange") {
            handle_folding_ranges(req, id);
        } else if (*method == "textDocument/selectionRange") {
            handle_selection_ranges(req, id);
        } else if (*method == "textDocument/semanticTokens/full") {
            handle_semantic_tokens(req, id, SemanticTokenRequest::full);
        } else if (*method == "textDocument/semanticTokens/full/delta") {
            handle_semantic_tokens(req, id, SemanticTokenRequest::delta);
        } else if (*method == "textDocument/semanticTokens/range") {
            handle_semantic_tokens(req, id, SemanticTokenRequest::range);
        } else if (*method == "workspace/didChangeConfiguration") {
            handle_did_change_configuration(req);
        } else if (*method == "workspace/didChangeWorkspaceFolders") {
            handle_did_change_workspace_folders(req);
        } else if (*method == "workspace/didChangeWatchedFiles") {
            refresh_all_open_documents();
        } else {
            send_error(id, error_method_not_found, "Method not found");
        }
    }

    void set_trace(const json& req)
    {
        auto value = json_string(req, {"params", "value"});
        if (!value) {
            return;
        }
        if (*value == "off") {
            log_level_ = LogLevel::error;
        } else if (*value == "messages") {
            log_level_ = LogLevel::info;
        } else if (*value == "verbose") {
            log_level_ = LogLevel::log;
        }
    }

    // -- Lifecycle -----------------------------------------------------------

    void handle_initialize(const json& req, const RequestId& id)
    {
        if (!id.present) {
            return;
        }
        if (initialized_) {
            send_error(id, error_invalid_request, "Server is already initialized");
            return;
        }

        negotiate_position_encoding(req);
        definition_link_support_ = json_bool(req,
            {"params", "capabilities", "textDocument", "definition", "linkSupport"});
        code_action_literal_support_ = json_bool(req,
            {"params", "capabilities", "textDocument", "codeAction",
                "codeActionLiteralSupport", "codeActionKind", "valueSet"})
            || find_json_value(req, {"params", "capabilities", "textDocument", "codeAction",
                   "codeActionLiteralSupport"})
                != nullptr;
        watched_files_registration_ = json_bool(req,
            {"params", "capabilities", "workspace", "didChangeWatchedFiles",
                "dynamicRegistration"});
        if (auto trace = json_string(req, {"params", "trace"})) {
            json trace_request{{"params", {{"value", *trace}}}};
            set_trace(trace_request);
        }

        auto& rt = nw::kernel::runtime();
        apply_workspace_folders(rt, find_json_value(req, {"params", "workspaceFolders"}));
        apply_module_paths(rt,
            find_json_value(req, {"params", "initializationOptions", "modulePaths"}));
        apply_inlay_hint_options(
            find_json_value(req, {"params", "initializationOptions", "inlayHints"}));

        json legend{
            {"tokenTypes", smalls_lsp::semantic_token_type_names()},
            {"tokenModifiers", smalls_lsp::semantic_token_modifier_names()}};

        json capabilities{
            {"positionEncoding", position_encoding_name()},
            {"textDocumentSync", {{"openClose", true}, {"change", 2}}},
            {"hoverProvider", true},
            {"definitionProvider", true},
            {"declarationProvider", true},
            {"typeDefinitionProvider", true},
            {"documentSymbolProvider", true},
            {"codeActionProvider", {{"codeActionKinds", json::array({"quickfix"})}}},
            // workspaceDiagnostics is implemented and measured but not
            // advertised: a corpus pass reports ~4,200 syntax errors on
            // 2da-generated config data files, which are not Smalls source.
            // The blocker is file identity, not cost. See
            // issues/smalls-lsp-propset-config-awareness.md.
            {"diagnosticProvider",
                {{"identifier", "smalls"}, {"interFileDependencies", true},
                    {"workspaceDiagnostics", false}}},
            {"foldingRangeProvider", true},
            {"selectionRangeProvider", true},
            {"completionProvider",
                {{"resolveProvider", true}, {"triggerCharacters", {".", "!", "("}}}},
            {"signatureHelpProvider", {{"triggerCharacters", {"(", ","}}}},
            {"inlayHintProvider", {{"resolveProvider", true}}},
            {"semanticTokensProvider",
                {{"legend", std::move(legend)},
                    {"full", {{"delta", true}}},
                    {"range", true}}},
        };

        initialized_ = true;
        send_response(id, {{"serverInfo", {{"name", "smalls-lsp"}, {"version", "0.1.0"}}},
                              {"capabilities", std::move(capabilities)}});
    }

    // -- Configuration -------------------------------------------------------

    void apply_workspace_folders(lang::Runtime& rt, const json* folders)
    {
        if (!folders || !folders->is_array()) {
            return;
        }
        for (const auto& folder : *folders) {
            if (!folder.is_object()) {
                continue;
            }
            auto uri = json_string(folder, {"uri"});
            if (!uri) {
                continue;
            }
            if (auto folder_path = uri_to_path(*uri)) {
                add_module_path(rt, folder_path->string());
            } else {
                log(LogLevel::warning,
                    fmt::format("ignoring workspace folder with unusable URI: {}", *uri));
            }
        }
    }

    void apply_module_paths(lang::Runtime& rt, const json* paths)
    {
        if (!paths || !paths->is_array()) {
            return;
        }
        for (const auto& path : *paths) {
            if (path.is_string()) {
                add_module_path(rt, path.get<std::string>());
            }
        }
    }

    void add_module_path(lang::Runtime& rt, const std::string& path)
    {
        if (path.empty()) {
            return;
        }
        std::error_code ec;
        if (!std::filesystem::is_directory(path, ec)) {
            return;
        }
        rt.add_module_path(path);
    }

    void apply_inlay_hint_options(const json* settings)
    {
        if (!settings || !settings->is_object()) {
            return;
        }
        auto toggle = [&](const char* name, bool& target) {
            if (const json* value = find_json_value(*settings, {name});
                value && value->is_boolean()) {
                target = value->get<bool>();
            }
        };
        toggle("parameterNames", inlay_hint_options_.parameter_names);
        toggle("variableTypes", inlay_hint_options_.variable_types);
        toggle("foreachTypes", inlay_hint_options_.foreach_types);
        toggle("lambdaReturnTypes", inlay_hint_options_.lambda_return_types);
    }

    void handle_did_change_configuration(const json& req)
    {
        auto& rt = nw::kernel::runtime();
        apply_inlay_hint_options(
            find_json_value(req, {"params", "settings", "smalls", "inlayHints"}));
        send_notification("workspace/inlayHint/refresh", json::object());
        size_t before = rt.module_paths().size();
        apply_module_paths(rt, find_json_value(req, {"params", "settings", "smalls", "modulePaths"}));
        if (rt.module_paths().size() != before) {
            log(LogLevel::info, "module search paths changed; re-analyzing open documents");
            register_module_watchers();
            refresh_all_open_documents();
        }
    }

    void handle_did_change_workspace_folders(const json& req)
    {
        auto& rt = nw::kernel::runtime();
        size_t before = rt.module_paths().size();
        apply_workspace_folders(rt, find_json_value(req, {"params", "event", "added"}));
        if (rt.module_paths().size() != before) {
            register_module_watchers();
            refresh_all_open_documents();
        }
    }

    void refresh_all_open_documents()
    {
        invalidate_export_index();
        for (const auto& [uri, _] : open_documents) {
            mark_dirty(uri);
        }
        flush_pending_analysis();
    }

    /// Watches every active module path.
    ///
    /// The client's own watcher covers the workspace only, so a stdlib living
    /// outside it would change without the server ever being told.
    void register_module_watchers()
    {
        if (!watched_files_registration_) {
            return;
        }

        json watchers = json::array();
        for (const auto& module_path : nw::kernel::runtime().module_paths()) {
            watchers.push_back({{"globPattern", (module_path / "**" / "*.smalls").string()}});
        }
        if (watchers.empty()) {
            return;
        }

        // Re-registering under the same id replaces the previous watcher set.
        if (watchers_registered_) {
            write_message({{"jsonrpc", "2.0"},
                {"id", fmt::format("smalls-unreg-{}", ++server_request_id_)},
                {"method", "client/unregisterCapability"},
                {"params", {{"unregisterations", json::array({json{
                    {"id", watcher_registration_id}, 
                    {"method", "workspace/didChangeWatchedFiles"}}})}}}});
        }

        write_message({{"jsonrpc", "2.0"},
            {"id", fmt::format("smalls-reg-{}", ++server_request_id_)},
            {"method", "client/registerCapability"},
            {"params", {{"registrations", json::array({json{
                {"id", watcher_registration_id},
                {"method", "workspace/didChangeWatchedFiles"},
                {"registerOptions", {{"watchers", std::move(watchers)}}}}})}}}});
        watchers_registered_ = true;
    }

    // -- Document synchronization --------------------------------------------

    void handle_did_open(const json& req)
    {
        auto uri = json_string(req, {"params", "textDocument", "uri"});
        auto text = json_string(req, {"params", "textDocument", "text"});
        auto version = json_integer(req, {"params", "textDocument", "version"});
        if (!uri || !text || !version) {
            log(LogLevel::warning, "didOpen: invalid parameters");
            return;
        }

        if (open_documents.contains(*uri)) {
            log(LogLevel::warning, "didOpen: document is already open");
            return;
        }

        open_documents.emplace(*uri, OpenDocument{std::move(*text), *version, {}});
        mark_dirty(*uri);
    }

    void handle_did_change(const json& req)
    {
        auto uri = json_string(req, {"params", "textDocument", "uri"});
        auto version = json_integer(req, {"params", "textDocument", "version"});
        const json* changes = find_json_value(req, {"params", "contentChanges"});
        if (!uri || !version || !changes || !changes->is_array() || changes->empty()) {
            log(LogLevel::warning, "didChange: invalid parameters");
            return;
        }

        auto document = open_documents.find(*uri);
        if (document == open_documents.end() || *version <= document->second.version) {
            log(LogLevel::warning, "didChange: unopened document or stale version");
            return;
        }

        // Move out and restore on rejection rather than copying: a rejected
        // batch must leave the owned document intact, but copying the whole
        // buffer on every keystroke is a real cost on a large file.
        std::string next_text = std::move(document->second.text);
        auto restore = [&] { document->second.text = std::move(next_text); };
        for (const auto& change : *changes) {
            if (!change.is_object()) {
                log(LogLevel::warning, "didChange: malformed change");
                restore();
                return;
            }

            auto text = json_string(change, {"text"});
            if (!text) {
                log(LogLevel::warning, "didChange: change is missing text");
                restore();
                return;
            }

            const json* range = find_json_value(change, {"range"});
            if (!range) {
                next_text = std::move(*text);
                continue;
            }

            auto start_line = json_integer(*range, {"start", "line"});
            auto start_character = json_integer(*range, {"start", "character"});
            auto end_line = json_integer(*range, {"end", "line"});
            auto end_character = json_integer(*range, {"end", "character"});
            if (!start_line || !start_character || !end_line || !end_character
                || *start_line < 0 || *start_character < 0 || *end_line < 0 || *end_character < 0
                || *start_line > std::numeric_limits<int>::max()
                || *start_character > std::numeric_limits<int>::max()
                || *end_line > std::numeric_limits<int>::max()
                || *end_character > std::numeric_limits<int>::max()) {
                log(LogLevel::warning, "didChange: malformed range");
                restore();
                return;
            }

            if (!apply_content_change(next_text, static_cast<int>(*start_line),
                    static_cast<int>(*start_character), static_cast<int>(*end_line),
                    static_cast<int>(*end_character), *text, position_encoding_)) {
                log(LogLevel::warning, "didChange: range is outside the document");
                restore();
                return;
            }
        }

        restore();
        document->second.version = *version;
        mark_dirty(*uri);
    }

    void handle_did_close(const json& req)
    {
        auto uri = json_string(req, {"params", "textDocument", "uri"});
        if (!uri) {
            log(LogLevel::warning, "didClose: invalid parameters");
            return;
        }

        open_documents.erase(*uri);
        token_snapshots_.erase(*uri);
        latest_diagnostics_.erase(*uri);
        dirty_documents_.erase(
            std::remove(dirty_documents_.begin(), dirty_documents_.end(), *uri),
            dirty_documents_.end());
        send_notification("textDocument/publishDiagnostics",
            {{"uri", *uri}, {"diagnostics", json::array()}});
    }

    /// Sends one refresh per analysis pass rather than one per document.
    void request_semantic_token_refresh()
    {
        send_notification("workspace/semanticTokens/refresh", json::object());
    }

    /// Queues a document for analysis without running it yet.
    void mark_dirty(const std::string& uri)
    {
        if (std::find(dirty_documents_.begin(), dirty_documents_.end(), uri)
            == dirty_documents_.end()) {
            dirty_documents_.push_back(uri);
        }
    }

    /// Analyzes every queued document.
    ///
    /// Deferring to here is what makes a burst of edits cost one pass: an
    /// intermediate version is superseded before it is ever analyzed, and the
    /// client sees diagnostics for the version it ended on.
    void flush_pending_analysis()
    {
        if (dirty_documents_.empty()) {
            return;
        }

        std::vector<std::string> pending;
        pending.swap(dirty_documents_);
        for (const auto& uri : pending) {
            // A document closed while the burst was draining has nothing to say.
            if (!open_documents.contains(uri)) {
                continue;
            }
            publish_diagnostics(uri);
            cascade_diagnostics(uri);
        }
        request_semantic_token_refresh();
    }

    /// True when another message is already waiting, so analysis can wait too.
    bool input_pending() const
    {
        if (input_pending_) {
            return input_pending_();
        }
        return input_.rdbuf() && input_.rdbuf()->in_avail() > 0;
    }

    // -- Diagnostics ---------------------------------------------------------

    /// Encodes a script's diagnostics, including the unused-import hints the
    /// compiler does not produce. Shared by push and workspace pull so the two
    /// cannot report a document differently.
    json diagnostics_for(lang::Script& script, const std::string& uri)
    {
        json result = json::array();
        for (const auto& diag : script.diagnostics()) {
            auto range = make_lsp_range(diag.location, script.text(), position_encoding_);
            if (!range) {
                continue;
            }
            json lsp_diag{{"range", std::move(*range)},
                {"message", diag.message},
                {"severity", diagnostic_severity(diag.severity)},
                {"source", "smalls"}};

            if (auto code = lang::diagnostic_code_name(diag.code); !code.empty()) {
                lsp_diag["code"] = std::string{code};
            }

            // A second location belongs in relatedInformation rather than
            // concatenated into the message.
            json related = json::array();
            for (const auto& link : diag.related) {
                auto link_range = make_lsp_range(
                    link.location, script.text(), position_encoding_);
                if (!link_range) {
                    continue;
                }
                related.push_back({{"location", {{"uri", uri}, {"range", std::move(*link_range)}}},
                    {"message", link.message}});
            }
            if (!related.empty()) {
                lsp_diag["relatedInformation"] = std::move(related);
            }

            // A client returns `data` untouched with the code action request,
            // which is how a fix gets the candidate names without recomputing
            // them or parsing the message.
            if (!diag.candidates.empty()) {
                json candidates = json::array();
                for (const auto& candidate : diag.candidates) {
                    candidates.push_back(candidate);
                }
                lsp_diag["data"] = json{{"candidates", std::move(candidates)}};
            }

            result.push_back(std::move(lsp_diag));
        }

        // The compiler has no opinion on unused imports, so they are derived
        // here and reported as a hint the editor can render faded.
        for (const auto& unused : smalls_lsp::collect_unused_imports(script)) {
            auto range = make_lsp_range(unused.name_range, script.text(), position_encoding_);
            if (!range) {
                continue;
            }
            result.push_back({{"range", std::move(*range)},
                {"message", fmt::format("'{}' is imported but never used", unused.name)},
                {"severity", 4},
                {"code", unused_import_code},
                {"tags", json::array({1})}, // Unnecessary
                {"source", "smalls"}});
        }

        return result;
    }

    void publish_diagnostics(const std::string& uri)
    {
        auto& rt = nw::kernel::runtime();
        auto document = open_documents.find(uri);
        if (document == open_documents.end()) {
            return;
        }
        const std::string& text = document->second.text;

        if (auto root = module_root_for_uri(uri); !root.empty()) {
            rt.add_module_path(root);
        }
        std::string module_name = cached_module_name(rt, uri);

        // Evicting this module also evicts its cached dependents, which is the
        // whole set an edit can invalidate. Evicting every user module instead
        // leaves the cache cold for every other open document.
        std::array<std::string_view, 1> changed_modules{module_name};
        rt.evict_modules(changed_modules);

        lang::Script* script = rt.load_module_from_source(module_name, text);
        if (!script) {
            return;
        }

        script->resolve();
        remember_module_uri(script, uri);

        document->second.dependencies.clear();
        for (const auto& dependency : script->dependencies()) {
            document->second.dependencies.emplace_back(dependency);
        }

        json lsp_diags = diagnostics_for(*script, uri);

        latest_diagnostics_[uri] = lsp_diags;
        send_notification("textDocument/publishDiagnostics",
            {{"uri", uri}, {"version", document->second.version},
                {"diagnostics", std::move(lsp_diags)}});
    }

    // -- Pull diagnostics ----------------------------------------------------

    /// Answers `textDocument/diagnostic`.
    ///
    /// The result id is the document version, so a client that already holds
    /// the report for this version gets an `unchanged` reply instead of the
    /// whole set.
    void handle_document_diagnostic(const json& req, const RequestId& id)
    {
        auto uri = json_string(req, {"params", "textDocument", "uri"});
        if (!uri) {
            send_error(id, error_invalid_params, "Invalid diagnostic request");
            return;
        }
        auto document = open_documents.find(*uri);
        if (document == open_documents.end()) {
            send_error(id, error_invalid_params, "Document is not open");
            return;
        }

        std::string result_id = std::to_string(document->second.version);
        auto previous = json_string(req, {"params", "previousResultId"});
        if (previous && *previous == result_id) {
            send_response(id, {{"kind", "unchanged"}, {"resultId", result_id}});
            return;
        }

        auto cached = latest_diagnostics_.find(*uri);
        json items = cached != latest_diagnostics_.end() ? cached->second : json::array();
        send_response(id, {{"kind", "full"}, {"resultId", result_id},
                              {"items", std::move(items)}});
    }

    /// Answers `workspace/diagnostic`.
    ///
    /// Reports every `.smalls` file under an active module path, so a
    /// project-wide error count does not require opening 2,192 files by hand.
    /// Open buffers win over their on-disk copy. Result ids are the document
    /// version for an open buffer and the file's write time otherwise, so a
    /// repeat poll that finds nothing changed answers `unchanged` without
    /// recompiling.
    void handle_workspace_diagnostic(const json& req, const RequestId& id)
    {
        auto& rt = nw::kernel::runtime();

        absl::flat_hash_map<std::string, std::string> previous_ids;
        if (const json* previous = find_json_value(req, {"params", "previousResultIds"});
            previous && previous->is_array()) {
            for (const auto& entry : *previous) {
                auto uri = json_string(entry, {"uri"});
                auto value = json_string(entry, {"value"});
                if (uri && value) {
                    previous_ids.emplace(*uri, *value);
                }
            }
        }

        json items = json::array();
        absl::flat_hash_set<std::string> reported;

        auto emit = [&](const std::string& uri, const std::string& result_id, json diagnostics) {
            if (!reported.insert(uri).second) {
                return;
            }
            auto known = previous_ids.find(uri);
            if (known != previous_ids.end() && known->second == result_id) {
                items.push_back({{"kind", "unchanged"}, {"uri", uri}, {"resultId", result_id}});
                return;
            }
            items.push_back({{"kind", "full"}, {"uri", uri}, {"resultId", result_id},
                {"items", std::move(diagnostics)}});
        };

        for (const auto& [uri, document] : open_documents) {
            auto cached = latest_diagnostics_.find(uri);
            emit(uri, std::to_string(document.version),
                cached != latest_diagnostics_.end() ? cached->second : json::array());
        }

        std::error_code ec;
        for (const auto& module_path : rt.module_paths()) {
            std::filesystem::recursive_directory_iterator it{module_path, ec}, end;
            if (ec) {
                continue;
            }
            for (; it != end; it.increment(ec)) {
                if (ec) {
                    break;
                }
                if (!it->is_regular_file(ec) || it->path().extension() != ".smalls") {
                    continue;
                }

                std::string uri = path_to_uri(canonical_or_normalized(it->path()));
                if (reported.contains(uri)) {
                    continue;
                }

                auto written = std::filesystem::last_write_time(it->path(), ec);
                std::string result_id = ec
                    ? std::string{}
                    : std::to_string(written.time_since_epoch().count());
                if (!result_id.empty()) {
                    auto known = previous_ids.find(uri);
                    if (known != previous_ids.end() && known->second == result_id) {
                        emit(uri, result_id, json::array());
                        continue;
                    }
                }

                lang::Script* script = rt.get_module(cached_module_name(rt, uri));
                emit(uri, result_id, script ? diagnostics_for(*script, uri) : json::array());
            }
        }

        send_response(id, {{"items", std::move(items)}});
    }

    // -- Export index --------------------------------------------------------

    /// Maps an exported symbol name to the modules exporting it.
    ///
    /// Add-import needs to find a symbol in a module the file does not import,
    /// which is therefore usually not loaded, so the modules under every search
    /// path are enumerated once and cached. This is a narrow index: it answers
    /// "who exports this name" and nothing else. References and rename need
    /// declaration identity and remain gated on the full index.
    void build_export_index()
    {
        if (export_index_built_) {
            return;
        }
        export_index_built_ = true;

        auto& rt = nw::kernel::runtime();
        std::error_code ec;
        for (const auto& module_path : rt.module_paths()) {
            std::filesystem::directory_iterator it{module_path, ec}, end;
            if (ec) {
                continue;
            }
            for (; it != end; it.increment(ec)) {
                if (ec) {
                    break;
                }
                if (!it->is_regular_file(ec) || it->path().extension() != ".smalls") {
                    continue;
                }

                std::string uri = path_to_uri(canonical_or_normalized(it->path()));
                std::string module_name = cached_module_name(rt, uri);
                lang::Script* script = rt.get_module(module_name);
                if (!script) {
                    continue;
                }
                for (const auto& entry : script->exports()) {
                    export_index_[entry.first].push_back(module_name);
                }
            }
        }
    }

    void invalidate_export_index()
    {
        export_index_.clear();
        export_index_built_ = false;
    }

    // -- Code actions --------------------------------------------------------

    /// Offers fixes for the diagnostics the client passes back in `context`.
    ///
    /// Actions key on a diagnostic's `code`, never on its message text, so
    /// rewording a message cannot silently disable a fix.
    void handle_code_action(const json& req, const RequestId& id)
    {
        auto uri = json_string(req, {"params", "textDocument", "uri"});
        if (!uri) {
            send_error(id, error_invalid_params, "Invalid code action request");
            return;
        }
        auto document = open_documents.find(*uri);
        if (document == open_documents.end()) {
            send_error(id, error_invalid_params, "Document is not open");
            return;
        }

        // Honor the client's filter rather than computing every kind.
        if (const json* only = find_json_value(req, {"params", "context", "only"});
            only && only->is_array()) {
            bool wants_quickfix = false;
            for (const auto& kind : *only) {
                if (kind.is_string() && kind.get_ref<const std::string&>().rfind("quickfix", 0) == 0) {
                    wants_quickfix = true;
                }
            }
            if (!wants_quickfix) {
                send_response(id, json::array());
                return;
            }
        }

        json actions = json::array();
        const json* diagnostics = find_json_value(req, {"params", "context", "diagnostics"});
        if (diagnostics && diagnostics->is_array()) {
            for (const auto& diagnostic : *diagnostics) {
                auto code = json_string(diagnostic, {"code"});
                if (!code) {
                    continue;
                }
                if (*code == unused_import_code) {
                    add_remove_import_action(actions, *uri, diagnostic);
                } else if (*code == "unresolved-identifier" || *code == "unknown-type") {
                    add_suggestion_actions(actions, *uri, diagnostic);
                    add_import_actions(actions, *uri, diagnostic);
                }
            }
        }

        add_missing_field_actions(actions, req, *uri);

        // A client without codeActionLiteralSupport cannot read a CodeAction
        // and only understands the legacy Command form.
        if (!code_action_literal_support_) {
            json commands = json::array();
            for (auto& action : actions) {
                commands.push_back({{"title", action["title"]},
                    {"command", "smalls.applyEdit"},
                    {"arguments", json::array({action["edit"]})}});
            }
            send_response(id, std::move(commands));
            return;
        }

        send_response(id, std::move(actions));
    }

    static json single_edit_action(std::string title, const std::string& uri, json edit,
        const json& diagnostic, bool preferred)
    {
        json action{{"title", std::move(title)},
            {"kind", "quickfix"},
            {"diagnostics", json::array({diagnostic})},
            {"edit", {{"changes", {{uri, json::array({std::move(edit)})}}}}}};
        if (preferred) {
            action["isPreferred"] = true;
        }
        return action;
    }

    void add_remove_import_action(json& actions, const std::string& uri, const json& diagnostic)
    {
        auto line = json_integer(diagnostic, {"range", "start", "line"});
        if (!line || *line < 0) {
            return;
        }
        // Deleting the line, rather than the declaration's own range, avoids
        // leaving a blank line behind.
        json edit{{"range", make_line_range(static_cast<int>(*line), 0,
                      static_cast<int>(*line) + 1, 0)},
            {"newText", ""}};
        actions.push_back(
            single_edit_action("Remove unused import", uri, std::move(edit), diagnostic, true));
    }

    /// Offers the near-matches the resolver already ranked.
    void add_suggestion_actions(json& actions, const std::string& uri, const json& diagnostic)
    {
        const json* candidates = find_json_value(diagnostic, {"data", "candidates"});
        if (!candidates || !candidates->is_array()) {
            return;
        }
        const json* range = find_json_value(diagnostic, {"range"});
        if (!range) {
            return;
        }

        bool only_one = candidates->size() == 1;
        for (const auto& candidate : *candidates) {
            if (!candidate.is_string()) {
                continue;
            }
            json edit{{"range", *range}, {"newText", candidate}};
            actions.push_back(single_edit_action(
                fmt::format("Change to '{}'", candidate.get_ref<const std::string&>()), uri,
                std::move(edit), diagnostic, only_one));
        }
    }

    /// Offers an import for each module exporting the unresolved name.
    void add_import_actions(json& actions, const std::string& uri, const json& diagnostic)
    {
        auto document = open_documents.find(uri);
        if (document == open_documents.end()) {
            return;
        }
        auto line = json_integer(diagnostic, {"range", "start", "line"});
        auto character = json_integer(diagnostic, {"range", "start", "character"});
        if (!line || !character) {
            return;
        }
        auto byte = lsp_character_to_byte(document->second.text, static_cast<int>(*line),
            static_cast<int>(*character), position_encoding_);
        if (!byte) {
            return;
        }
        std::string name = identifier_at(document->second.text, static_cast<int>(*line), *byte);
        if (name.empty()) {
            return;
        }

        build_export_index();
        auto found = export_index_.find(name);
        if (found == export_index_.end()) {
            return;
        }

        auto& rt = nw::kernel::runtime();
        lang::Script* script = get_or_load_module(rt, cached_module_name(rt, uri), uri);
        if (!script) {
            return;
        }
        std::string own_module = cached_module_name(rt, uri);

        bool only_one = found->second.size() == 1;
        for (const auto& module_name : found->second) {
            if (module_name == own_module) {
                continue;
            }
            int insert_line = smalls_lsp::import_insert_line(*script, module_name);
            json edit{{"range", make_line_range(insert_line, 0, insert_line, 0)},
                {"newText", fmt::format("from {} import {{ {} }};\n", module_name, name)}};
            actions.push_back(single_edit_action(
                fmt::format("Import '{}' from '{}'", name, module_name), uri, std::move(edit),
                diagnostic, only_one));
        }
    }

    /// Offers to fill in fields a brace initializer left out.
    void add_missing_field_actions(json& actions, const json& req, const std::string& uri)
    {
        auto requested = open_document_range(req, uri);
        if (!requested) {
            return;
        }

        auto& rt = nw::kernel::runtime();
        lang::Script* script = get_or_load_module(rt, cached_module_name(rt, uri), uri);
        if (!script) {
            return;
        }

        for (const auto& literal : smalls_lsp::collect_incomplete_struct_literals(*script)) {
            // Editors send the cursor's range, which is usually empty and need
            // not contain the literal, so overlap is the right test rather than
            // containment in either direction.
            if (literal.range.end < requested->start || requested->end < literal.range.start) {
                continue;
            }

            std::string text;
            bool separated = literal.has_existing_fields;
            for (const auto& field : literal.missing) {
                if (separated) {
                    text += ", ";
                }
                separated = true;
                text += fmt::format("{} = {}", field.name, field.default_value);
            }

            auto position = make_lsp_position(
                literal.insert_at, script->text(), position_encoding_);
            if (!position) {
                continue;
            }
            json edit{{"range", {{"start", *position}, {"end", *position}}},
                {"newText", std::move(text)}};

            json action{{"title", fmt::format("Add missing fields to '{}'", literal.type_name)},
                {"kind", "quickfix"},
                {"isPreferred", true},
                {"edit", {{"changes", {{uri, json::array({std::move(edit)})}}}}}};
            actions.push_back(std::move(action));
        }
    }

    /// Republishes the open documents that reach the edited module.
    ///
    /// `evict_modules` drops the edited module and its cached dependents
    /// transitively, so the republish set must be transitive too.
    /// `Script::dependencies` reports direct imports only, so the set is grown
    /// to a fixpoint rather than tested one level deep.
    void cascade_diagnostics(const std::string& changed_uri)
    {
        auto& rt = nw::kernel::runtime();

        absl::flat_hash_set<std::string> invalidated;
        invalidated.insert(cached_module_name(rt, changed_uri));

        bool grew = true;
        while (grew) {
            grew = false;
            for (const auto& [uri, document] : open_documents) {
                if (uri == changed_uri) {
                    continue;
                }
                const std::string& module_name = cached_module_name(rt, uri);
                if (invalidated.contains(module_name)) {
                    continue;
                }
                for (const auto& dependency : document.dependencies) {
                    if (invalidated.contains(dependency)) {
                        invalidated.insert(module_name);
                        grew = true;
                        break;
                    }
                }
            }
        }

        std::vector<std::string> affected;
        for (const auto& [uri, _] : open_documents) {
            if (uri != changed_uri && invalidated.contains(cached_module_name(rt, uri))) {
                affected.push_back(uri);
            }
        }
        for (const auto& uri : affected) {
            publish_diagnostics(uri);
        }
    }

    static int diagnostic_severity(lang::DiagnosticSeverity severity)
    {
        switch (severity) {
        case lang::DiagnosticSeverity::error:
            return 1;
        case lang::DiagnosticSeverity::warning:
            return 2;
        case lang::DiagnosticSeverity::information:
            return 3;
        case lang::DiagnosticSeverity::hint:
            return 4;
        }
        return 1;
    }

    // -- Hover ---------------------------------------------------------------

    void handle_hover(const json& req, const RequestId& id)
    {
        auto& rt = nw::kernel::runtime();
        auto position = open_document_position(req);
        if (!position) {
            send_error(id, error_invalid_params, "Invalid text document position");
            return;
        }
        const auto& [uri, line, character] = *position;
        auto document = open_documents.find(uri);

        lang::Script* script = get_or_load_module(rt, cached_module_name(rt, uri), uri);
        if (!script) {
            send_response(id, nullptr);
            return;
        }

        std::string word = identifier_at(document->second.text, line, character);
        if (word.empty()) {
            send_response(id, nullptr);
            return;
        }

        lang::Symbol sym = script->locate_symbol(
            word, static_cast<size_t>(line + 1), static_cast<size_t>(character));

        // Fallback: if AstLocator could not resolve the position, try the module
        // export table directly.
        if (!sym.decl && sym.view.empty()) {
            sym = script->locate_export(word, true);
        }

        if (!sym.decl && sym.view.empty()) {
            send_response(id, nullptr);
            return;
        }

        std::string content = "```smalls\n";
        auto* alias_import = sym.decl ? dynamic_cast<const lang::AliasedImportDecl*>(sym.decl) : nullptr;
        auto* function = sym.decl ? dynamic_cast<const lang::FunctionDefinition*>(sym.decl) : nullptr;
        if (alias_import) {
            content += fmt::format("module {} = \"{}\"",
                alias_import->alias.loc.view(), alias_import->module_path);
        } else if (function) {
            content += function_signature(rt, function);
        } else {
            if (!sym.type.empty()) {
                content += "(" + sym.type + ") ";
            }
            content += std::string(sym.view);
        }
        content += "\n```";
        if (!sym.comment.empty()) {
            content += "\n\n---\n\n" + sym.comment;
        }

        json result{{"contents", {{"kind", "markdown"}, {"value", std::move(content)}}}};
        if (auto range = word_range(document->second.text, line, character)) {
            result["range"] = std::move(*range);
        }
        send_response(id, std::move(result));
    }

    /// The LSP range of the identifier under a byte column, for hover and for
    /// completion replace ranges.
    std::optional<json> word_range(const std::string& text, int line, int character) const
    {
        auto bounds = line_bounds(text, line);
        if (!bounds) {
            return std::nullopt;
        }
        size_t line_length = bounds->second - bounds->first;
        if (static_cast<size_t>(character) > line_length) {
            return std::nullopt;
        }

        int start = character;
        while (start > 0 && is_identifier_char(text[bounds->first + start - 1])) {
            --start;
        }
        int end = character;
        while (static_cast<size_t>(end) < line_length
            && is_identifier_char(text[bounds->first + end])) {
            ++end;
        }

        auto lsp_start = byte_character_to_lsp(text, line, start, position_encoding_);
        auto lsp_end = byte_character_to_lsp(text, line, end, position_encoding_);
        if (!lsp_start || !lsp_end) {
            return std::nullopt;
        }
        return make_line_range(line, *lsp_start, line, *lsp_end);
    }

    static std::string function_signature(const lang::Runtime& rt,
        const lang::FunctionDefinition* function)
    {
        std::string result = fmt::format("fn {}(", function->identifier_.loc.view());
        bool first = true;
        for (const auto* param : function->params) {
            if (!param) {
                continue;
            }
            if (!first) {
                result += ", ";
            }
            first = false;
            result += std::string(param->identifier_.loc.view());
            if (param->type_id_ != lang::invalid_type_id) {
                result += ": " + std::string(rt.type_name(param->type_id_));
            }
        }
        result += ")";
        if (function->return_type && function->type_id_ != lang::invalid_type_id) {
            result += " -> " + std::string(rt.type_name(function->type_id_));
        }
        return result;
    }

    // -- Definition ----------------------------------------------------------

    void handle_definition(const json& req, const RequestId& id, bool type_definition)
    {
        auto& rt = nw::kernel::runtime();
        auto position = open_document_position(req);
        if (!position) {
            send_error(id, error_invalid_params, "Invalid text document position");
            return;
        }
        const auto& [uri, line, character] = *position;
        auto document = open_documents.find(uri);

        lang::Script* script = get_or_load_module(rt, cached_module_name(rt, uri), uri);
        if (!script) {
            send_response(id, nullptr);
            return;
        }

        std::string word = identifier_at(document->second.text, line, character);
        if (word.empty()) {
            send_response(id, nullptr);
            return;
        }

        lang::Symbol sym = script->locate_symbol(
            word, static_cast<size_t>(line + 1), static_cast<size_t>(character));
        if (!sym.decl) {
            send_response(id, nullptr);
            return;
        }

        const lang::Declaration* target = sym.decl;
        const lang::Script* provider = sym.provider ? sym.provider : script;

        if (type_definition) {
            // Resolve to the declaration of the symbol's type rather than the
            // symbol itself.
            auto type_name = rt.type_name(target->type_id_);
            if (type_name.empty()) {
                send_response(id, nullptr);
                return;
            }
            lang::Symbol type_symbol = script->locate_export(std::string{type_name}, true);
            if (!type_symbol.decl) {
                send_response(id, nullptr);
                return;
            }
            target = type_symbol.decl;
            provider = type_symbol.provider ? type_symbol.provider : script;
        }

        std::string target_uri = resolve_provider_uri(rt, provider);
        if (target_uri.empty()) {
            send_response(id, nullptr);
            return;
        }

        auto full_range = make_lsp_range(target->range(), provider->text(), position_encoding_);
        if (!full_range) {
            send_response(id, nullptr);
            return;
        }
        auto selection = make_lsp_range(
            target->selection_range(), provider->text(), position_encoding_);
        if (!selection) {
            selection = full_range;
        }

        if (!definition_link_support_) {
            send_response(id, {{"uri", target_uri}, {"range", std::move(*full_range)}});
            return;
        }

        json link{{"targetUri", target_uri},
            {"targetRange", std::move(*full_range)},
            {"targetSelectionRange", std::move(*selection)}};
        if (auto origin = word_range(document->second.text, line, character)) {
            link["originSelectionRange"] = std::move(*origin);
        }
        send_response(id, json::array({std::move(link)}));
    }

    /// Maps a providing script to a document URI, or empty when the provider is
    /// native or synthesized and has no file on disk.
    std::string resolve_provider_uri(lang::Runtime& rt, const lang::Script* provider)
    {
        if (!provider) {
            return {};
        }
        std::string provider_name(provider->name());

        // The runtime normalizes a module name by turning separators into dots,
        // so a provider's name is never the URI it was loaded from. Match on the
        // name the script actually reports instead of reconstructing it.
        auto known = module_uris_.find(provider_name);
        if (known != module_uris_.end()) {
            return known->second;
        }
        if (provider_name.rfind("file://", 0) == 0) {
            return provider_name;
        }

        auto relative = rt.module_name_to_path(provider_name);
        std::error_code ec;
        for (const auto& module_path : rt.module_paths()) {
            auto candidate = canonical_or_normalized(module_path / relative);
            if (std::filesystem::exists(candidate, ec)) {
                return path_to_uri(candidate);
            }
        }
        return {};
    }

    // -- Completion ----------------------------------------------------------

    static int symbol_kind(lang::SymbolKind kind)
    {
        switch (kind) {
        case lang::SymbolKind::variable:
            return 6;
        case lang::SymbolKind::function:
            return 3;
        case lang::SymbolKind::type:
            return 7;
        case lang::SymbolKind::param:
            return 6;
        case lang::SymbolKind::field:
            return 5;
        default:
            return 1;
        }
    }

    /// Ranks a candidate so locals outrank distant stdlib exports.
    static char completion_rank(const lang::Symbol& sym, const lang::Script* script)
    {
        switch (sym.kind) {
        case lang::SymbolKind::param:
        case lang::SymbolKind::variable:
            return '0';
        case lang::SymbolKind::field:
            return '1';
        default:
            break;
        }
        if (sym.provider == script) {
            return '2';
        }
        if (sym.provider && std::string_view{sym.provider->name()}.starts_with("core.")) {
            return '4';
        }
        return '3';
    }

    /// The right-aligned annotation an editor shows beside a completion.
    ///
    /// Built from the symbol's resolved type rather than from a slice of source
    /// text: a slice depends on knowing which script declared the symbol, and
    /// getting that wrong yields text from an unrelated declaration in the file
    /// being edited.
    static std::string completion_detail(const lang::Symbol& sym)
    {
        auto& rt = nw::kernel::runtime();
        if (const auto* function = dynamic_cast<const lang::FunctionDefinition*>(sym.decl)) {
            return function_signature(rt, function);
        }
        if (sym.decl && sym.decl->type_id_ != lang::invalid_type_id) {
            return std::string{rt.type_name(sym.decl->type_id_)};
        }
        if (!sym.type.empty()) {
            return sym.type;
        }
        return {};
    }

    json completion_item(const lang::Symbol& sym, const lang::Script* script,
        const std::optional<json>& replace_range)
    {
        std::string label = sym.decl ? sym.decl->identifier() : std::string{sym.view};
        json item{{"label", label},
            {"kind", symbol_kind(sym.kind)},
            {"sortText", std::string{completion_rank(sym, script)} + label},
            {"filterText", label}};

        std::string insert = label;
        int insert_format = 1;
        if (const auto* function = sym.decl
                ? dynamic_cast<const lang::FunctionDefinition*>(sym.decl)
                : nullptr) {
            insert = snippet_for_function(function, label);
            insert_format = 2;
        }
        item["insertTextFormat"] = insert_format;

        if (replace_range) {
            item["textEdit"] = json{{"range", *replace_range}, {"newText", insert}};
        } else {
            item["insertText"] = insert;
        }

        // Detail and documentation are the expensive fields; supply them only
        // for the item the client highlights.
        pending_completions_.push_back({completion_detail(sym), sym.comment});
        item["data"] = json{{"generation", completion_generation_},
            {"index", static_cast<int64_t>(pending_completions_.size() - 1)}};
        return item;
    }

    static std::string snippet_for_function(const lang::FunctionDefinition* function,
        const std::string& label)
    {
        std::string result = label + "(";
        int placeholder = 0;
        bool first = true;
        for (const auto* param : function->params) {
            if (!param) {
                continue;
            }
            if (!first) {
                result += ", ";
            }
            first = false;
            result += fmt::format("${{{}:{}}}", ++placeholder, param->identifier_.loc.view());
        }
        result += ")";
        return result;
    }

    void handle_completion(const json& req, const RequestId& id)
    {
        auto& rt = nw::kernel::runtime();
        auto position = open_document_position(req);
        if (!position) {
            send_error(id, error_invalid_params, "Invalid text document position");
            return;
        }
        const auto& [uri, line, character] = *position;
        auto document = open_documents.find(uri);

        lang::Script* script = get_or_load_module(rt, cached_module_name(rt, uri), uri);
        if (!script) {
            send_response(id, json{{"isIncomplete", true}, {"items", json::array()}});
            return;
        }

        ++completion_generation_;
        pending_completions_.clear();
        auto replace_range = word_range(document->second.text, line, character);

        // Dot completion is detected from the buffer as well as the trigger, so
        // a re-trigger with the cursor mid-word still resolves the receiver.
        bool is_dot_trigger = false;
        int dot_col = character;
        auto trigger_character = json_string(req, {"params", "context", "triggerCharacter"});
        if (trigger_character && *trigger_character == ".") {
            is_dot_trigger = true;
        }
        if (!is_dot_trigger) {
            is_dot_trigger = detect_dot_trigger(document->second.text, line, character, dot_col);
        }

        json items = json::array();
        if (is_dot_trigger) {
            std::string needle = identifier_before(document->second.text, line, dot_col);
            nw::Vector<lang::Symbol> symbols;
            script->complete_dot(needle, static_cast<size_t>(line + 1),
                static_cast<size_t>(character), symbols, true);
            for (const auto& sym : symbols) {
                items.push_back(completion_item(sym, script, replace_range));
            }
        } else {
            lang::CompletionContext context;
            script->complete_at("", static_cast<size_t>(line + 1),
                static_cast<size_t>(character), context, true);
            for (const auto& sym : context.completions) {
                items.push_back(completion_item(sym, script, replace_range));
            }
        }

        // The candidate set depends on the prefix, so the client must re-query
        // rather than filter one snapshot client-side.
        send_response(id, json{{"isIncomplete", true}, {"items", std::move(items)}});
    }

    void handle_completion_resolve(const json& req, const RequestId& id)
    {
        json item = req.contains("params") ? req["params"] : json::object();
        auto generation = json_integer(item, {"data", "generation"});
        auto index = json_integer(item, {"data", "index"});
        if (generation && index && *generation == completion_generation_
            && *index >= 0
            && static_cast<size_t>(*index) < pending_completions_.size()) {
            const auto& [detail, documentation] = pending_completions_[static_cast<size_t>(*index)];
            if (!detail.empty()) {
                item["detail"] = detail;
            }
            if (!documentation.empty()) {
                item["documentation"] = json{{"kind", "markdown"}, {"value", documentation}};
            }
        }
        send_response(id, std::move(item));
    }

    // -- Signature help ------------------------------------------------------

    void handle_signature_help(const json& req, const RequestId& id)
    {
        auto& rt = nw::kernel::runtime();
        auto position = open_document_position(req);
        if (!position) {
            send_error(id, error_invalid_params, "Invalid text document position");
            return;
        }
        const auto& [uri, line, character] = *position;

        lang::Script* script = get_or_load_module(rt, cached_module_name(rt, uri), uri);
        if (!script) {
            send_response(id, nullptr);
            return;
        }

        lang::SignatureHelp help = script->signature_help(
            static_cast<size_t>(line + 1), static_cast<size_t>(character));
        auto* function = help.decl
            ? dynamic_cast<const lang::FunctionDefinition*>(help.decl)
            : nullptr;
        if (!function) {
            send_response(id, nullptr);
            return;
        }

        std::string label = std::string(function->identifier_.loc.view()) + "(";
        json parameters = json::array();
        bool first = true;
        for (const auto* param : function->params) {
            if (!param) {
                continue;
            }
            if (!first) {
                label += ", ";
            }
            first = false;
            size_t param_start = label.size();
            label += std::string(param->identifier_.loc.view());
            if (param->type) {
                label += ": " + std::string(rt.type_name(param->type_id_));
            }
            parameters.push_back({{"label", json::array({param_start, label.size()})}});
        }
        label += ")";
        if (function->return_type) {
            label += " -> " + std::string(rt.type_name(function->type_id_));
        }

        send_response(id, {{"signatures", json::array({json{{"label", std::move(label)},
                                             {"parameters", std::move(parameters)}}})},
                              {"activeSignature", 0},
                              {"activeParameter", static_cast<int>(help.active_param)}});
    }

    // -- Inlay hints ---------------------------------------------------------

    void handle_inlay_hints(const json& req, const RequestId& id)
    {
        auto uri = json_string(req, {"params", "textDocument", "uri"});
        if (!uri) {
            send_error(id, error_invalid_params, "Invalid inlay hint range");
            return;
        }
        auto range = open_document_range(req, *uri);
        if (!range) {
            send_error(id, error_invalid_params, "Invalid inlay hint range");
            return;
        }

        auto& rt = nw::kernel::runtime();
        lang::Script* script = get_or_load_module(rt, cached_module_name(rt, *uri), *uri);
        if (!script) {
            send_response(id, json::array());
            return;
        }

        json result = json::array();
        for (const auto& hint : script->inlay_hints(*range, inlay_hint_options_)) {
            if (hint.position.line == 0) {
                continue;
            }
            auto position = make_lsp_position(hint.position, script->text(), position_encoding_);
            if (!position) {
                continue;
            }

            bool is_type = hint.kind == lang::InlayHintKind::type;
            // A type hint reads as `: T` after the name; a parameter hint reads
            // as `name:` before the argument.
            std::string label = is_type ? ": " + hint.message : hint.message + ":";

            json encoded{{"position", std::move(*position)},
                {"kind", static_cast<int>(hint.kind)},
                {"paddingRight", !is_type}};

            // A label part can carry a location, which is what makes a hinted
            // type clickable rather than inert text.
            if (auto part = hint_label_part(label, hint, *uri, *script)) {
                encoded["label"] = json::array({std::move(*part)});
            } else {
                encoded["label"] = std::move(label);
            }
            result.push_back(std::move(encoded));
        }
        send_response(id, std::move(result));
    }

    /// Builds a label part pointing at the declaration a hint names.
    std::optional<json> hint_label_part(const std::string& label, const lang::InlayHint& hint,
        const std::string& uri, const lang::Script& script)
    {
        if (!hint.target) {
            return std::nullopt;
        }
        const lang::Script* provider = script.provider_for_decl(hint.target);
        if (!provider) {
            return std::nullopt;
        }

        auto& rt = nw::kernel::runtime();
        std::string target_uri = provider == &script ? uri : resolve_provider_uri(rt, provider);
        if (target_uri.empty()) {
            return std::nullopt;
        }

        auto range = make_lsp_range(
            hint.target->selection_range(), provider->text(), position_encoding_);
        if (!range) {
            return std::nullopt;
        }
        return json{{"value", label},
            {"location", {{"uri", target_uri}, {"range", std::move(*range)}}}};
    }

    /// Answers `inlayHint/resolve`. Tooltips are the expensive field and are
    /// only worth building for the hint a user actually points at.
    void handle_inlay_hint_resolve(const json& req, const RequestId& id)
    {
        json hint = req.contains("params") ? req["params"] : json::object();

        // The label is an array of parts, which find_json_value cannot walk.
        const json* label = find_json_value(hint, {"label"});
        if (label && label->is_array() && !label->empty()) {
            if (auto target_uri = json_string((*label)[0], {"location", "uri"})) {
                hint["tooltip"] = json{{"kind", "markdown"},
                    {"value", fmt::format("Declared in `{}`", *target_uri)}};
            }
        }
        send_response(id, std::move(hint));
    }

    // -- Document symbols ----------------------------------------------------

    json document_symbol_json(const smalls_lsp::DocumentSymbol& symbol,
        std::string_view text) const
    {
        auto range = make_lsp_range(symbol.range, text, position_encoding_);
        auto selection = make_lsp_range(symbol.selection_range, text, position_encoding_);
        if (!range) {
            return json(nullptr);
        }
        if (!selection) {
            selection = range;
        }

        json result{{"name", symbol.name},
            {"kind", static_cast<int>(symbol.kind)},
            {"range", std::move(*range)},
            {"selectionRange", std::move(*selection)}};
        if (!symbol.detail.empty()) {
            result["detail"] = symbol.detail;
        }
        if (!symbol.children.empty()) {
            json children = json::array();
            for (const auto& child : symbol.children) {
                json encoded = document_symbol_json(child, text);
                if (!encoded.is_null()) {
                    children.push_back(std::move(encoded));
                }
            }
            if (!children.empty()) {
                result["children"] = std::move(children);
            }
        }
        return result;
    }

    void handle_document_symbols(const json& req, const RequestId& id)
    {
        lang::Script* script = script_for_request(req, id);
        if (!script) {
            return;
        }

        json result = json::array();
        for (const auto& symbol : smalls_lsp::collect_document_symbols(*script)) {
            json encoded = document_symbol_json(symbol, script->text());
            if (!encoded.is_null()) {
                result.push_back(std::move(encoded));
            }
        }
        send_response(id, std::move(result));
    }

    // -- Folding ranges ------------------------------------------------------

    void handle_folding_ranges(const json& req, const RequestId& id)
    {
        lang::Script* script = script_for_request(req, id);
        if (!script) {
            return;
        }

        json result = json::array();
        for (const auto& range : smalls_lsp::collect_folding_ranges(*script)) {
            json encoded{{"startLine", range.start_line}, {"endLine", range.end_line}};
            switch (range.kind) {
            case smalls_lsp::FoldingKind::comment:
                encoded["kind"] = "comment";
                break;
            case smalls_lsp::FoldingKind::imports:
                encoded["kind"] = "imports";
                break;
            case smalls_lsp::FoldingKind::none:
                break;
            }
            result.push_back(std::move(encoded));
        }
        send_response(id, std::move(result));
    }

    // -- Selection ranges ----------------------------------------------------

    void handle_selection_ranges(const json& req, const RequestId& id)
    {
        auto uri = json_string(req, {"params", "textDocument", "uri"});
        const json* positions = find_json_value(req, {"params", "positions"});
        if (!uri || !positions || !positions->is_array()) {
            send_error(id, error_invalid_params, "Invalid selection range request");
            return;
        }

        auto document = open_documents.find(*uri);
        if (document == open_documents.end()) {
            send_error(id, error_invalid_params, "Document is not open");
            return;
        }

        auto& rt = nw::kernel::runtime();
        lang::Script* script = get_or_load_module(rt, cached_module_name(rt, *uri), *uri);
        if (!script) {
            send_response(id, json::array());
            return;
        }

        json result = json::array();
        for (const auto& position : *positions) {
            auto line = json_integer(position, {"line"});
            auto character = json_integer(position, {"character"});
            if (!line || !character || *line < 0 || *character < 0
                || *line > std::numeric_limits<int>::max()
                || *character > std::numeric_limits<int>::max()) {
                result.push_back(json(nullptr));
                continue;
            }

            auto byte_character = lsp_character_to_byte(document->second.text,
                static_cast<int>(*line), static_cast<int>(*character), position_encoding_);
            if (!byte_character) {
                result.push_back(json(nullptr));
                continue;
            }

            auto chain = smalls_lsp::collect_selection_range_chain(*script,
                {static_cast<size_t>(*line + 1), static_cast<size_t>(*byte_character)});
            result.push_back(selection_range_json(chain, script->text()));
        }
        send_response(id, std::move(result));
    }

    /// Builds the nested `parent` chain the protocol expects, outermost last.
    json selection_range_json(const std::vector<lang::SourceRange>& chain,
        std::string_view text) const
    {
        json result(nullptr);
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            auto range = make_lsp_range(*it, text, position_encoding_);
            if (!range) {
                continue;
            }
            json node{{"range", std::move(*range)}};
            if (!result.is_null()) {
                node["parent"] = std::move(result);
            }
            result = std::move(node);
        }
        return result;
    }

    // -- Semantic tokens -----------------------------------------------------

    enum class SemanticTokenRequest {
        full,
        delta,
        range,
    };

    lang::Script* script_for_request(const json& req, const RequestId& id)
    {
        auto uri = json_string(req, {"params", "textDocument", "uri"});
        if (!uri) {
            send_error(id, error_invalid_params, "Invalid text document");
            return nullptr;
        }
        if (!open_documents.contains(*uri)) {
            send_error(id, error_invalid_params, "Document is not open");
            return nullptr;
        }

        auto& rt = nw::kernel::runtime();
        lang::Script* script = get_or_load_module(rt, cached_module_name(rt, *uri), *uri);
        if (!script) {
            send_response(id, json::array());
            return nullptr;
        }
        return script;
    }

    void handle_semantic_tokens(const json& req, const RequestId& id, SemanticTokenRequest kind)
    {
        auto uri = json_string(req, {"params", "textDocument", "uri"});
        if (!uri) {
            send_error(id, error_invalid_params, "Invalid text document");
            return;
        }
        if (!open_documents.contains(*uri)) {
            send_error(id, error_invalid_params, "Document is not open");
            return;
        }

        auto& rt = nw::kernel::runtime();
        lang::Script* script = get_or_load_module(rt, cached_module_name(rt, *uri), *uri);
        if (!script) {
            send_response(id, {{"data", json::array()}});
            return;
        }

        auto tokens = smalls_lsp::collect_semantic_tokens(*script);

        std::optional<int> range_start_line;
        std::optional<int> range_end_line;
        if (kind == SemanticTokenRequest::range) {
            auto range = source_range(req);
            if (!range) {
                send_error(id, error_invalid_params, "Invalid range");
                return;
            }
            range_start_line = static_cast<int>(range->start.line) - 1;
            range_end_line = static_cast<int>(range->end.line) - 1;
        }

        std::vector<int> data;
        data.reserve(tokens.size() * 5);
        int last_line = 0;
        int last_start = 0;
        for (const auto& token : tokens) {
            if (range_start_line
                && (token.line < *range_start_line || token.line > *range_end_line)) {
                continue;
            }
            auto start = byte_character_to_lsp(
                script->text(), token.line, token.start, position_encoding_);
            auto end = byte_character_to_lsp(
                script->text(), token.line, token.start + token.length, position_encoding_);
            if (!start || !end || *end <= *start) {
                continue;
            }

            int delta_line = token.line - last_line;
            data.push_back(delta_line);
            data.push_back(delta_line == 0 ? *start - last_start : *start);
            data.push_back(*end - *start);
            data.push_back(token.type);
            data.push_back(token.modifiers);

            last_line = token.line;
            last_start = *start;
        }

        if (kind == SemanticTokenRequest::range) {
            send_response(id, {{"data", std::move(data)}});
            return;
        }

        std::string result_id = std::to_string(++token_result_id_);
        auto previous = token_snapshots_.find(*uri);
        if (kind == SemanticTokenRequest::delta) {
            auto previous_result_id = json_string(req, {"params", "previousResultId"});
            if (previous_result_id && previous != token_snapshots_.end()
                && previous->second.result_id == *previous_result_id) {
                json edits = token_delta(previous->second.data, data);
                token_snapshots_[*uri] = TokenSnapshot{result_id, std::move(data)};
                send_response(id, {{"resultId", result_id}, {"edits", std::move(edits)}});
                return;
            }
        }

        token_snapshots_[*uri] = TokenSnapshot{result_id, data};
        send_response(id, {{"resultId", result_id}, {"data", std::move(data)}});
    }

    /// Expresses the change between two token arrays as a single replacement of
    /// the region between their common prefix and common suffix.
    static json token_delta(const std::vector<int>& previous, const std::vector<int>& current)
    {
        size_t prefix = 0;
        size_t limit = std::min(previous.size(), current.size());
        while (prefix < limit && previous[prefix] == current[prefix]) {
            ++prefix;
        }

        size_t suffix = 0;
        while (suffix < limit - prefix
            && previous[previous.size() - 1 - suffix] == current[current.size() - 1 - suffix]) {
            ++suffix;
        }

        size_t delete_count = previous.size() - prefix - suffix;
        std::vector<int> inserted(current.begin() + static_cast<std::ptrdiff_t>(prefix),
            current.end() - static_cast<std::ptrdiff_t>(suffix));
        if (delete_count == 0 && inserted.empty()) {
            return json::array();
        }
        return json::array({json{{"start", prefix},
            {"deleteCount", delete_count},
            {"data", std::move(inserted)}}});
    }

    // -- State ---------------------------------------------------------------

    std::istream& input_;
    std::ostream& output_;
    InputPendingFn input_pending_;
    std::vector<std::string> dirty_documents_;
    PositionEncoding position_encoding_ = PositionEncoding::utf16;
    LogLevel log_level_ = LogLevel::info;
    bool initialized_ = false;
    bool shutdown_requested_ = false;
    bool exit_requested_ = false;
    bool definition_link_support_ = false;
    bool watched_files_registration_ = false;
    bool code_action_literal_support_ = false;
    lang::InlayHintOptions inlay_hint_options_;
    bool watchers_registered_ = false;
    uint64_t server_request_id_ = 0;
    static constexpr const char* watcher_registration_id = "smalls-module-watchers";

    /// Cancelled request ids, keyed by their serialized form so that
    /// numeric and string ids share one set.
    absl::flat_hash_set<std::string> cancelled_;
    absl::node_hash_map<std::string, std::string> module_names_;
    /// Normalized script name to the URI it was compiled from.
    absl::node_hash_map<std::string, std::string> module_uris_;
    size_t module_path_generation_ = std::numeric_limits<size_t>::max();
    absl::node_hash_map<std::string, TokenSnapshot> token_snapshots_;
    /// Last published set per document, so a pull request needs no recompile.
    absl::node_hash_map<std::string, json> latest_diagnostics_;
    absl::node_hash_map<std::string, std::vector<std::string>> export_index_;
    bool export_index_built_ = false;
    uint64_t token_result_id_ = 0;
    int64_t completion_generation_ = 0;
    std::vector<std::pair<std::string, std::string>> pending_completions_;
};

} // namespace

void run_smalls_lsp(std::istream& input, std::ostream& output, InputPendingFn input_pending)
{
    LspServer server{input, output, std::move(input_pending)};
    server.run();
}
