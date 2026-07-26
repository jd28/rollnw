#include "server.hpp"

#include "lsp_text.hpp"

#include <nw/kernel/Kernel.hpp>
#include <nw/smalls/AstLocator.hpp>
#include <nw/smalls/NullVisitor.hpp>
#include <nw/smalls/Smalls.hpp>
#include <nw/smalls/runtime.hpp>

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
#include <map>
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

namespace lsp = nw::smalls;

// == LSP Helpers =============================================================

std::optional<json> make_lsp_position(
    lsp::SourcePosition position, std::string_view text, PositionEncoding encoding)
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
    lsp::SourceRange range, std::string_view text, PositionEncoding encoding)
{
    auto start = make_lsp_position(range.start, text, encoding);
    auto end = make_lsp_position(range.end, text, encoding);
    if (!start || !end) {
        return std::nullopt;
    }
    return json{{"start", std::move(*start)}, {"end", std::move(*end)}};
}

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

std::optional<lsp::SourceRange> source_range(const json& request)
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

    return lsp::SourceRange{
        {static_cast<size_t>(*start_line + 1), static_cast<size_t>(*start_character)},
        {static_cast<size_t>(*end_line + 1), static_cast<size_t>(*end_character)}};
}

// == URI Utilities ===========================================================

std::string uri_to_path(const std::string& uri)
{
    if (uri.substr(0, 7) == "file://") {
        return uri.substr(7);
    }
    return uri;
}

std::string path_to_uri(const std::string& path)
{
    if (path.substr(0, 7) == "file://") {
        return path;
    }
    return "file://" + path;
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

std::string module_name_for_uri(const lsp::Runtime& rt, const std::string& uri)
{
    std::filesystem::path canonical_file = canonical_or_normalized(uri_to_path(uri));

    auto score_module_name = [](const std::string& module_name) -> size_t {
        return static_cast<size_t>(std::count(module_name.begin(), module_name.end(), '.'));
    };

    std::string best_module_name;
    size_t best_score = 0;

    for (const auto& module_path : rt.module_paths()) {
        std::filesystem::path canonical_module_path = canonical_or_normalized(module_path);
        std::filesystem::path effective_root = module_effective_root(canonical_module_path);

        if (!path_is_within(canonical_file, effective_root)) {
            continue;
        }

        auto module_name = rt.path_to_module_name(canonical_file, effective_root);
        if (!module_name.empty()) {
            std::string candidate(module_name);
            size_t score = score_module_name(candidate);
            if (best_module_name.empty() || score > best_score) {
                best_module_name = std::move(candidate);
                best_score = score;
            }
        }
    }

    if (!best_module_name.empty()) {
        return best_module_name;
    }

    return uri;
}

std::filesystem::path module_root_for_uri(const std::string& uri)
{
    std::filesystem::path canonical_file = canonical_or_normalized(uri_to_path(uri));
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

// == Semantic Token Tracking =================================================

struct SemanticToken {
    int line;
    int start;
    int length;
    int type;
};

struct SemanticTokenVisitor : public nw::smalls::NullVisitor {
    using lsp::NullVisitor::visit;
    std::vector<SemanticToken> tokens;

    void add_token(lsp::SourceRange range, int type)
    {
        if (range.start.line == 0 || range.start.line != range.end.line
            || range.end.column <= range.start.column
            || range.start.line - 1 > static_cast<size_t>(std::numeric_limits<int>::max())
            || range.start.column > static_cast<size_t>(std::numeric_limits<int>::max())
            || range.end.column - range.start.column
                > static_cast<size_t>(std::numeric_limits<int>::max())) {
            return;
        }
        tokens.push_back({static_cast<int>(range.start.line - 1),
            static_cast<int>(range.start.column),
            static_cast<int>(range.end.column - range.start.column),
            type});
    }

    void visit(lsp::FunctionDefinition* decl) override
    {
        add_token(decl->identifier_.loc.range, 4); // function
        for (auto* p : decl->params) {
            if (p) {
                add_token(p->identifier_.loc.range, 3); // parameter
                if (p->type) p->type->accept(this);
                if (p->init) p->init->accept(this);
            }
        }
        if (decl->return_type) decl->return_type->accept(this);
        if (decl->block) decl->block->accept(this);
    }

    void visit(lsp::VarDecl* decl) override
    {
        add_token(decl->identifier_.loc.range, 2); // variable
        if (decl->type) decl->type->accept(this);
        if (decl->init) decl->init->accept(this);
    }

    void visit(lsp::StructDecl* decl) override
    {
        if (decl->type && decl->type->name) {
            add_token(decl->type->name->range_, 0); // type
        }
        for (auto* d : decl->decls) {
            if (d) d->accept(this);
        }
    }

    void visit(lsp::SumDecl* decl) override
    {
        if (decl->type && decl->type->name) {
            add_token(decl->type->name->range_, 0); // type
        }
        for (auto* v : decl->variants) {
            if (v) v->accept(this);
        }
    }

    void visit(lsp::VariantDecl* decl) override
    {
        add_token(decl->identifier_.loc.range, 1); // enumMember
        if (decl->payload) decl->payload->accept(this);
    }

    void visit(lsp::TypeAlias* decl) override
    {
        if (decl->type && decl->type->name) {
            add_token(decl->type->name->range_, 0); // type
        }
        if (decl->aliased_type) decl->aliased_type->accept(this);
    }

    void visit(lsp::NewtypeDecl* decl) override
    {
        if (decl->type && decl->type->name) {
            add_token(decl->type->name->range_, 0); // type
        }
        if (decl->wrapped_type) decl->wrapped_type->accept(this);
    }

    void visit(lsp::AliasedImportDecl* decl) override
    {
        add_token(decl->alias.loc.range, 2); // variable (module alias)
    }

    void visit(lsp::SelectiveImportDecl* decl) override
    {
        for (const auto& sym : decl->imported_symbols) {
            add_token(sym.loc.range, 0); // type (conservative)
        }
    }

    void visit(lsp::TypeExpression* expr) override
    {
        if (expr->name) {
            add_token(expr->name->range_, 0); // type
        }
        for (auto* p : expr->params) {
            if (p) p->accept(this);
        }
        if (expr->fn_return_type) expr->fn_return_type->accept(this);
    }

    void visit(lsp::BlockStatement* stmt) override
    {
        for (auto* n : stmt->nodes) {
            if (n) n->accept(this);
        }
    }

    void visit(lsp::DeclList* stmt) override
    {
        for (auto* d : stmt->decls) {
            if (d) d->accept(this);
        }
    }

    void visit(lsp::ExprStatement* stmt) override
    {
        if (stmt->expr) stmt->expr->accept(this);
    }

    void visit(lsp::IfStatement* stmt) override
    {
        if (stmt->expr) stmt->expr->accept(this);
        if (stmt->if_branch) stmt->if_branch->accept(this);
        if (stmt->else_branch) stmt->else_branch->accept(this);
    }

    void visit(lsp::ForStatement* stmt) override
    {
        if (stmt->init) stmt->init->accept(this);
        if (stmt->check) stmt->check->accept(this);
        if (stmt->inc) stmt->inc->accept(this);
        if (stmt->block) stmt->block->accept(this);
    }

    void visit(lsp::AssignExpression* expr) override
    {
        if (expr->lhs) expr->lhs->accept(this);
        if (expr->rhs) expr->rhs->accept(this);
    }

    void visit(lsp::BinaryExpression* expr) override
    {
        if (expr->lhs) expr->lhs->accept(this);
        if (expr->rhs) expr->rhs->accept(this);
    }

    void visit(lsp::GroupingExpression* expr) override
    {
        if (expr->expr) expr->expr->accept(this);
    }

    void visit(lsp::CallExpression* expr) override
    {
        if (expr->expr) expr->expr->accept(this);
        for (auto* arg : expr->args) {
            if (arg) arg->accept(this);
        }
    }
    void visit(lsp::ConditionalExpression* expr) override
    {
        if (expr->test) expr->test->accept(this);
        if (expr->true_branch) expr->true_branch->accept(this);
        if (expr->false_branch) expr->false_branch->accept(this);
    }

    void visit(lsp::CastExpression* expr) override
    {
        if (expr->expr) expr->expr->accept(this);
        if (expr->target_type) expr->target_type->accept(this);
    }

    void visit(lsp::BraceInitLiteral* expr) override
    {
        for (auto& item : expr->items) {
            if (item.key) item.key->accept(this);
            if (item.value) item.value->accept(this);
        }
    }

    void visit(lsp::UnaryExpression* expr) override
    {
        if (expr->rhs) expr->rhs->accept(this);
    }

    void visit(lsp::PathExpression* expr) override
    {
        for (auto* part : expr->parts) {
            if (part) part->accept(this);
        }
    }

    void visit(lsp::IdentifierExpression* expr) override
    {
        if (expr->decl) {
            if (dynamic_cast<const lsp::FunctionDefinition*>(expr->decl)) {
                add_token(expr->range_, 4); // function
            } else if (dynamic_cast<const lsp::StructDecl*>(expr->decl)
                || dynamic_cast<const lsp::SumDecl*>(expr->decl)
                || dynamic_cast<const lsp::TypeAlias*>(expr->decl)
                || dynamic_cast<const lsp::NewtypeDecl*>(expr->decl)) {
                add_token(expr->range_, 0); // type
            } else {
                add_token(expr->range_, 2); // variable
            }
        }
    }

    void visit(lsp::LiteralExpression* expr) override
    {
        if (expr->literal.type == lsp::TokenType::STRING_LITERAL || expr->literal.type == lsp::TokenType::STRING_RAW_LITERAL) {
            add_token(expr->range_, 7); // string
        } else if (expr->literal.type == lsp::TokenType::INTEGER_LITERAL || expr->literal.type == lsp::TokenType::FLOAT_LITERAL) {
            add_token(expr->range_, 8); // number
        }
    }
};

// == LSP Server ==============================================================

struct LspServer {
    struct OpenDocument {
        std::string text;
        int64_t version = 0;
    };

    explicit LspServer(std::istream& input, std::ostream& output)
        : input_{input}
        , output_{output}
    {
    }

    std::map<std::string, OpenDocument> open_documents;

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

    std::optional<lsp::SourceRange> open_document_range(
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

    lsp::Script* get_or_load_module(lsp::Runtime& rt, const std::string& module_name,
        const std::string& uri)
    {
        // For files open in the editor always go through load_module_from_source:
        // it returns the cached module if still present, otherwise compiles from
        // the buffer.  Never fall back to rt.get_module for open files because
        // that would load a stale on-disk version after evict_user_modules().
        auto it = open_documents.find(uri);
        if (it != open_documents.end()) {
            return rt.load_module_from_source(module_name, it->second.text);
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

            try {
                auto request = json::parse(buffer.begin(), buffer.end());
                try {
                    handle_request(request);
                } catch (const std::exception& e) {
                    std::cerr << "LSP Request Error: " << e.what() << std::endl;
                    json id = request.is_object() && request.contains("id") ? request["id"] : json(nullptr);
                    if (!id.is_null()) {
                        send_error(std::move(id), -32602, "Invalid params");
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "LSP Parse Error: " << e.what() << std::endl;
                send_error(nullptr, -32700, "Parse error");
            }
        }
    }

    void send_response(json id, json result)
    {
        json response = {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result", result}};
        std::string body = response.dump();
        output_ << "Content-Length: " << body.length() << "\r\n\r\n"
                << body << std::flush;
    }

    void send_error(json id, int code, std::string_view message)
    {
        json response = {
            {"jsonrpc", "2.0"},
            {"id", std::move(id)},
            {"error", {{"code", code}, {"message", message}}}};
        std::string body = response.dump();
        output_ << "Content-Length: " << body.length() << "\r\n\r\n"
                << body << std::flush;
    }

    void send_notification(std::string method, json params)
    {
        json notification = {
            {"jsonrpc", "2.0"},
            {"method", method},
            {"params", params}};
        std::string body = notification.dump();
        output_ << "Content-Length: " << body.length() << "\r\n\r\n"
                << body << std::flush;
    }

    void handle_request(const json& req)
    {
        auto protocol = json_string(req, {"jsonrpc"});
        auto method = json_string(req, {"method"});
        if (!protocol || *protocol != "2.0" || !method) {
            json id = req.is_object() && req.contains("id") ? req["id"] : json(nullptr);
            send_error(std::move(id), -32600, "Invalid request");
            return;
        }

        if (shutdown_requested_ && *method != "exit") {
            if (req.contains("id")) {
                send_error(req["id"], -32600, "Server has shut down");
            }
            return;
        }

        if (*method == "exit") {
            exit_requested_ = true;
            return;
        }

        if (!initialized_ && *method != "initialize") {
            if (req.contains("id")) {
                send_error(req["id"], -32002, "Server not initialized");
            }
            return;
        }

        if (*method == "initialize") {
            if (!req.contains("id")) {
                return;
            }
            if (initialized_) {
                send_error(req["id"], -32600, "Server is already initialized");
                return;
            }
            negotiate_position_encoding(req);
            initialized_ = true;
            json result = {
                {"serverInfo", {{"name", "smalls-lsp"}}},
                {"capabilities", {{"positionEncoding", position_encoding_name()}, {"textDocumentSync", {{"openClose", true}, {"change", 1}}}, {"hoverProvider", true}, {"definitionProvider", true}, {"completionProvider", {{"resolveProvider", false}, {"triggerCharacters", {".", "!", "("}}}}, {"signatureHelpProvider", {{"triggerCharacters", {"(", ","}}}}, {"inlayHintProvider", true}, {"semanticTokensProvider", {{"legend", {{"tokenTypes", {"type", "enumMember", "variable", "parameter", "function", "keyword", "comment", "string", "number", "operator"}}, {"tokenModifiers", {}}}}, {"full", true}}}}}};
            send_response(req["id"], result);
        } else if (*method == "textDocument/didOpen") {
            handle_did_open(req);
        } else if (*method == "textDocument/didChange") {
            handle_did_change(req);
        } else if (*method == "textDocument/hover") {
            handle_hover(req);
        } else if (*method == "textDocument/definition") {
            handle_definition(req);
        } else if (*method == "textDocument/completion") {
            handle_completion(req);
        } else if (*method == "textDocument/signatureHelp") {
            handle_signature_help(req);
        } else if (*method == "textDocument/inlayHint") {
            handle_inlay_hints(req);
        } else if (*method == "textDocument/semanticTokens/full") {
            handle_semantic_tokens(req);
        } else if (*method == "workspace/didChangeWatchedFiles") {
            // Invalidate everything on external file changes, then re-diagnose all open files
            for (const auto& [uri, _] : open_documents) {
                publish_diagnostics(uri);
            }
        } else if (*method == "textDocument/didClose") {
            handle_did_close(req);
        } else if (*method == "shutdown") {
            if (req.contains("id")) {
                shutdown_requested_ = true;
                send_response(req["id"], nullptr);
            }
        } else if (req.contains("id")) {
            send_error(req["id"], -32601, "Method not found");
        }
    }

    void handle_did_open(const json& req)
    {
        auto uri = json_string(req, {"params", "textDocument", "uri"});
        auto text = json_string(req, {"params", "textDocument", "text"});
        auto version = json_integer(req, {"params", "textDocument", "version"});
        if (!uri || !text || !version) {
            std::cerr << "[didOpen] invalid parameters" << std::endl;
            return;
        }

        if (open_documents.contains(*uri)) {
            std::cerr << "[didOpen] document is already open" << std::endl;
            return;
        }

        open_documents.emplace(*uri, OpenDocument{std::move(*text), *version});
        publish_diagnostics(*uri);
    }

    void handle_did_change(const json& req)
    {
        auto uri = json_string(req, {"params", "textDocument", "uri"});
        auto version = json_integer(req, {"params", "textDocument", "version"});
        const json* changes = find_json_value(req, {"params", "contentChanges"});
        if (!uri || !version || !changes || !changes->is_array() || changes->empty()) {
            std::cerr << "[didChange] invalid parameters" << std::endl;
            return;
        }

        auto document = open_documents.find(*uri);
        if (document == open_documents.end() || *version <= document->second.version) {
            std::cerr << "[didChange] unopened document or stale version" << std::endl;
            return;
        }

        std::optional<std::string> next_text;
        for (const auto& change : *changes) {
            if (!change.is_object() || change.contains("range")) {
                std::cerr << "[didChange] incremental change received for full-sync document" << std::endl;
                return;
            }
            auto text = json_string(change, {"text"});
            if (!text) {
                std::cerr << "[didChange] change is missing text" << std::endl;
                return;
            }
            next_text = std::move(*text);
        }

        document->second = OpenDocument{std::move(*next_text), *version};
        publish_diagnostics(*uri);

        // Re-diagnose other open files that may import this module.
        for (const auto& [other_uri, _] : open_documents) {
            if (other_uri != *uri) {
                publish_diagnostics(other_uri);
            }
        }
    }

    void handle_did_close(const json& req)
    {
        auto uri = json_string(req, {"params", "textDocument", "uri"});
        if (!uri) {
            std::cerr << "[didClose] invalid parameters" << std::endl;
            return;
        }

        open_documents.erase(*uri);
        send_notification("textDocument/publishDiagnostics",
            {{"uri", *uri}, {"diagnostics", json::array()}});
    }

    void handle_hover(const json& req)
    {
        auto& rt = nw::kernel::runtime();
        auto position = open_document_position(req);
        if (!position) {
            send_error(req.value("id", json(nullptr)), -32602, "Invalid text document position");
            return;
        }
        const auto& [uri, line, character] = *position;
        auto document = open_documents.find(uri);

        std::string module_name = module_name_for_uri(rt, uri);
        lsp::Script* script = get_or_load_module(rt, module_name, uri);
        if (!script) {
            send_response(req["id"], nullptr);
            return;
        }

        std::string word;
        word = identifier_at(document->second.text, line, character);
        if (word.empty()) {
            send_response(req["id"], nullptr);
            return;
        }

        lsp::Symbol sym = script->locate_symbol(
            word, static_cast<size_t>(line + 1), static_cast<size_t>(character));

        // Fallback: if AstLocator couldn't resolve position, try module export table directly
        if (!sym.decl && sym.view.empty()) {
            sym = script->locate_export(word, true);
        }

        if (sym.decl || !sym.view.empty()) {
            std::string content = "```smalls\n";

            // For functions with a declaration, build a full signature with parameter names.
            auto* alias_imp = sym.decl ? dynamic_cast<const lsp::AliasedImportDecl*>(sym.decl) : nullptr;
            auto* fd = sym.decl ? dynamic_cast<const lsp::FunctionDefinition*>(sym.decl) : nullptr;
            if (alias_imp) {
                content += "module ";
                content += alias_imp->alias.loc.view();
                content += " = \"";
                content += alias_imp->module_path;
                content += "\"";
            } else if (fd) {
                content += "fn ";
                content += fd->identifier_.loc.view();
                content += "(";
                for (size_t i = 0; i < fd->params.size(); ++i) {
                    if (i > 0) content += ", ";
                    auto* p = fd->params[i];
                    if (!p) continue;
                    content += p->identifier_.loc.view();
                    if (p->type_id_ != lsp::invalid_type_id) {
                        content += ": ";
                        content += rt.type_name(p->type_id_);
                    }
                }
                content += ")";
                if (fd->return_type && fd->type_id_ != lsp::invalid_type_id) {
                    content += " -> ";
                    content += rt.type_name(fd->type_id_);
                }
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

            json result = {
                {"contents", {{"kind", "markdown"}, {"value", content}}}};
            send_response(req["id"], result);
        } else {
            send_response(req["id"], nullptr);
        }
    }

    void handle_definition(const json& req)
    {
        auto& rt = nw::kernel::runtime();
        auto position = open_document_position(req);
        if (!position) {
            send_error(req.value("id", json(nullptr)), -32602, "Invalid text document position");
            return;
        }
        const auto& [uri, line, character] = *position;
        auto document = open_documents.find(uri);

        std::string module_name = module_name_for_uri(rt, uri);
        lsp::Script* script = get_or_load_module(rt, module_name, uri);
        if (!script) {
            send_response(req["id"], nullptr);
            return;
        }

        std::string def_word;
        def_word = identifier_at(document->second.text, line, character);
        if (def_word.empty()) {
            send_response(req["id"], nullptr);
            return;
        }

        lsp::Symbol def_sym = script->locate_symbol(
            def_word, static_cast<size_t>(line + 1), static_cast<size_t>(character));

        if (def_sym.decl) {
            auto* decl = def_sym.decl;
            std::string target_uri;
            if (def_sym.provider) {
                std::string provider_name(def_sym.provider->name());

                if (provider_name.rfind("file://", 0) == 0) {
                    target_uri = provider_name;
                } else {
                    // Check open buffers first
                    for (const auto& [open_uri, _] : open_documents) {
                        if (module_name_for_uri(rt, open_uri) == provider_name) {
                            target_uri = open_uri;
                            break;
                        }
                    }
                    // Fall back: resolve module name to a file path on disk
                    if (target_uri.empty()) {
                        auto rel = rt.module_name_to_path(provider_name);
                        std::error_code ec;
                        for (const auto& mp : rt.module_paths()) {
                            auto candidate = canonical_or_normalized(mp / rel);
                            if (std::filesystem::exists(candidate, ec)) {
                                target_uri = path_to_uri(candidate.string());
                                break;
                            }
                        }
                    }
                }
            }

            if (target_uri.empty()) {
                send_response(req["id"], nullptr);
                return;
            }

            const lsp::Script* provider = def_sym.provider ? def_sym.provider : script;
            auto range = make_lsp_range(
                decl->range(), provider->text(), position_encoding_);
            if (!range) {
                send_response(req["id"], nullptr);
                return;
            }

            send_response(req["id"], {{"uri", target_uri}, {"range", std::move(*range)}});
        } else {
            send_response(req["id"], nullptr);
        }
    }

    static int symbol_kind(lsp::SymbolKind k)
    {
        switch (k) {
        case lsp::SymbolKind::variable:
            return 6;
        case lsp::SymbolKind::function:
            return 3;
        case lsp::SymbolKind::type:
            return 7;
        case lsp::SymbolKind::param:
            return 6;
        case lsp::SymbolKind::field:
            return 5;
        default:
            return 1;
        }
    }

    static json symbol_to_item(const lsp::Symbol& sym)
    {
        return {{"label", sym.decl->identifier()},
            {"kind", symbol_kind(sym.kind)},
            {"detail", std::string(sym.view)},
            {"documentation", sym.comment}};
    }

    static json symbols_to_items(const nw::Vector<lsp::Symbol>& syms)
    {
        json result = json::array();
        for (const auto& sym : syms) {
            result.push_back(symbol_to_item(sym));
        }
        return result;
    }

    void handle_completion(const json& req)
    {
        auto& rt = nw::kernel::runtime();
        auto position = open_document_position(req);
        if (!position) {
            send_error(req.value("id", json(nullptr)), -32602, "Invalid text document position");
            return;
        }
        const auto& [uri, line, character] = *position;
        auto document = open_documents.find(uri);

        std::string module_name = module_name_for_uri(rt, uri);
        lsp::Script* script = get_or_load_module(rt, module_name, uri);
        if (!script) {
            std::cerr << "[completion] no module: " << module_name << std::endl;
            send_response(req["id"], json::array());
            return;
        }

        // Check for dot-triggered completion via LSP context OR by peeking at the buffer.
        // Also handle re-trigger (triggerKind:3) where cursor may be mid-word after a dot,
        // e.g. "foo.ba|r" — scan left past the partial word to find a dot, then adjust
        // the column so identifier_before extracts the identifier to the left of the dot.
        bool is_dot_trigger = false;
        int dot_col = character; // column right after the dot (adjusted if re-trigger)
        const json* request_context = find_json_value(req, {"params", "context"});
        if (request_context && request_context->is_object()) {
            auto trigger_kind = json_integer(*request_context, {"triggerKind"}).value_or(0);
            auto trigger_character = json_string(*request_context, {"triggerCharacter"}).value_or("");
            std::cerr << "[completion] triggerKind=" << trigger_kind
                      << " triggerChar='" << trigger_character << "'"
                      << " line=" << line << " char=" << character
                      << " buf=" << (document != open_documents.end() ? "found" : "MISSING")
                      << std::endl;
            if (trigger_character == ".") {
                is_dot_trigger = true;
                dot_col = character;
            }
        }
        if (!is_dot_trigger && document != open_documents.end()) {
            is_dot_trigger = detect_dot_trigger(document->second.text, line, character, dot_col);
        }

        if (is_dot_trigger && document != open_documents.end()) {
            std::string needle = identifier_before(document->second.text, line, dot_col);
            nw::Vector<lsp::Symbol> syms;
            std::cerr << "[completion] dot trigger: needle='" << needle
                      << "' line=" << (line + 1) << " dot_col=" << dot_col
                      << " character=" << character << std::endl;
            script->complete_dot(needle, static_cast<size_t>(line + 1),
                static_cast<size_t>(character), syms, true);
            std::cerr << "[completion] dot results: " << syms.size() << std::endl;
            send_response(req["id"], symbols_to_items(syms));
            return;
        }

        std::cerr << "[completion] fallthrough to complete_at: is_dot=" << is_dot_trigger
                  << " buf=" << (document != open_documents.end() ? "found" : "MISSING") << std::endl;

        lsp::CompletionContext context;
        script->complete_at("", static_cast<size_t>(line + 1), static_cast<size_t>(character), context, true);

        json result = json::array();
        for (const auto& sym : context.completions) {
            result.push_back(symbol_to_item(sym));
        }

        send_response(req["id"], result);
    }

    void handle_signature_help(const json& req)
    {
        auto& rt = nw::kernel::runtime();
        auto position = open_document_position(req);
        if (!position) {
            send_error(req.value("id", json(nullptr)), -32602, "Invalid text document position");
            return;
        }
        const auto& [uri, line, character] = *position;

        std::string module_name = module_name_for_uri(rt, uri);
        lsp::Script* script = get_or_load_module(rt, module_name, uri);
        if (!script) {
            send_response(req["id"], nullptr);
            return;
        }

        lsp::SignatureHelp sh = script->signature_help(
            static_cast<size_t>(line + 1), static_cast<size_t>(character));

        if (!sh.decl) {
            send_response(req["id"], nullptr);
            return;
        }

        auto* fd = dynamic_cast<const lsp::FunctionDefinition*>(sh.decl);
        if (!fd) {
            send_response(req["id"], nullptr);
            return;
        }

        // Build label: name(param: type, ...) -> rettype
        std::string label = std::string(fd->identifier_.loc.view()) + "(";
        json parameters = json::array();
        for (size_t i = 0; i < fd->params.size(); ++i) {
            auto* p = fd->params[i];
            if (!p) continue;
            if (i > 0) label += ", ";
            size_t param_start = label.size();
            label += std::string(p->identifier_.loc.view());
            if (p->type) {
                label += ": " + std::string(rt.type_name(p->type_id_));
            }
            size_t param_end = label.size();
            parameters.push_back({{"label", json::array({param_start, param_end})}});
        }
        label += ")";
        if (fd->return_type) {
            label += " -> " + std::string(rt.type_name(fd->type_id_));
        }

        json sig = {
            {"label", label},
            {"parameters", parameters}};

        send_response(req["id"], {{"signatures", json::array({sig})}, {"activeSignature", 0}, {"activeParameter", static_cast<int>(sh.active_param)}});
    }

    void handle_inlay_hints(const json& req)
    {
        auto uri = json_string(req, {"params", "textDocument", "uri"});
        if (!uri) {
            send_error(req.value("id", json(nullptr)), -32602, "Invalid inlay hint range");
            return;
        }
        auto range = open_document_range(req, *uri);
        if (!range) {
            send_error(req.value("id", json(nullptr)), -32602, "Invalid inlay hint range");
            return;
        }

        auto& rt = nw::kernel::runtime();

        std::string module_name = module_name_for_uri(rt, *uri);
        lsp::Script* script = get_or_load_module(rt, module_name, *uri);
        if (!script) {
            send_response(req["id"], json::array());
            return;
        }

        json result = json::array();
        for (const auto& hint : script->inlay_hints(*range)) {
            if (hint.position.line == 0) {
                continue;
            }
            auto position = make_lsp_position(
                hint.position, script->text(), position_encoding_);
            if (!position) {
                continue;
            }
            result.push_back({{"position", std::move(*position)},
                {"label", hint.message + ":"},
                {"kind", 2},
                {"paddingRight", true}});
        }
        send_response(req["id"], std::move(result));
    }

    void handle_semantic_tokens(const json& req)
    {
        auto& rt = nw::kernel::runtime();
        auto uri = json_string(req, {"params", "textDocument", "uri"});
        if (!uri) {
            send_error(req.value("id", json(nullptr)), -32602, "Invalid text document");
            return;
        }
        if (!open_documents.contains(*uri)) {
            send_error(req.value("id", json(nullptr)), -32602, "Document is not open");
            return;
        }

        std::string module_name = module_name_for_uri(rt, *uri);
        lsp::Script* script = get_or_load_module(rt, module_name, *uri);
        if (!script) {
            send_response(req["id"], {{"data", json::array()}});
            return;
        }

        SemanticTokenVisitor visitor;
        for (auto* decl : script->ast().decls) {
            if (decl) decl->accept(&visitor);
        }

        std::sort(visitor.tokens.begin(), visitor.tokens.end(), [](const SemanticToken& a, const SemanticToken& b) {
            if (a.line != b.line) return a.line < b.line;
            return a.start < b.start;
        });

        json data = json::array();
        int last_line = 0;
        int last_start = 0;

        for (const auto& token : visitor.tokens) {
            if (token.length <= 0
                || token.start > std::numeric_limits<int>::max() - token.length) {
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
            int delta_start = delta_line == 0 ? *start - last_start : *start;

            data.push_back(delta_line);
            data.push_back(delta_start);
            data.push_back(*end - *start);
            data.push_back(token.type);
            data.push_back(0);

            last_line = token.line;
            last_start = *start;
        }

        send_response(req["id"], {{"data", data}});
    }

    void publish_diagnostics(const std::string& uri)
    {
        auto& rt = nw::kernel::runtime();
        auto document = open_documents.find(uri);
        if (document == open_documents.end()) {
            return;
        }
        const std::string& text = document->second.text;
        std::string module_name = module_name_for_uri(rt, uri);

        if (auto root = module_root_for_uri(uri); !root.empty()) {
            rt.add_module_path(root);
        }

        std::array<std::string_view, 1> changed_modules{module_name};
        rt.evict_modules(changed_modules);
        rt.evict_user_modules();

        lsp::Script* script = rt.load_module_from_source(module_name, text);
        if (!script) return;

        script->resolve();

        json lsp_diags = json::array();
        for (const auto& diag : script->diagnostics()) {
            int severity = 1;
            switch (diag.severity) {
            case lsp::DiagnosticSeverity::error:
                severity = 1;
                break;
            case lsp::DiagnosticSeverity::warning:
                severity = 2;
                break;
            case lsp::DiagnosticSeverity::information:
                severity = 3;
                break;
            case lsp::DiagnosticSeverity::hint:
                severity = 4;
                break;
            }

            auto range = make_lsp_range(
                diag.location, script->text(), position_encoding_);
            if (!range) {
                continue;
            }
            json lsp_diag = {
                {"range", std::move(*range)},
                {"message", diag.message},
                {"severity", severity},
                {"source", "smalls"}};
            lsp_diags.push_back(lsp_diag);
        }

        // Suppress false positives for config data files (no imports, no decls, but has errors)
        bool looks_like_config_file = script->ast().imports.empty()
            && script->ast().decls.empty()
            && !script->diagnostics().empty();
        if (looks_like_config_file) {
            send_notification("textDocument/publishDiagnostics",
                {{"uri", uri},
                    {"version", document->second.version},
                    {"diagnostics", json::array()}});
            send_notification("workspace/semanticTokens/refresh", json::object());
            return;
        }

        json params = {
            {"uri", uri},
            {"version", document->second.version},
            {"diagnostics", lsp_diags}};
        send_notification("textDocument/publishDiagnostics", params);
        send_notification("workspace/semanticTokens/refresh", json::object());
    }

    std::istream& input_;
    std::ostream& output_;
    PositionEncoding position_encoding_ = PositionEncoding::utf16;
    bool initialized_ = false;
    bool shutdown_requested_ = false;
    bool exit_requested_ = false;
};

void run_smalls_lsp(std::istream& input, std::ostream& output)
{
    LspServer server{input, output};
    server.run();
}
