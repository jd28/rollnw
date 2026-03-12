#include <nw/kernel/Kernel.hpp>
#include <nw/smalls/AstLocator.hpp>
#include <nw/smalls/NullVisitor.hpp>
#include <nw/smalls/Smalls.hpp>
#include <nw/smalls/runtime.hpp>

#include <fmt/core.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

using json = nlohmann::json;

namespace lsp = nw::smalls;

// == LSP Helpers =============================================================

json make_lsp_range(lsp::SourceRange range)
{
    int start_line = range.start.line > 0 ? static_cast<int>(range.start.line - 1) : 0;
    int end_line = range.end.line > 0 ? static_cast<int>(range.end.line - 1) : 0;
    return {
        {"start", {{"line", start_line}, {"character", static_cast<int>(range.start.column)}}},
        {"end", {{"line", end_line}, {"character", static_cast<int>(range.end.column)}}}};
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
        if (range.start.line == 0) return;
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
    std::map<std::string, std::string> open_buffers;

    void run()
    {
#ifdef _WIN32
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
#endif

        while (std::cin.good()) {
            std::string header;
            size_t content_length = 0;

            while (std::getline(std::cin, header) && header != "\r") {
                if (header.find("Content-Length: ") == 0) {
                    content_length = std::stoul(header.substr(16));
                }
            }
            if (!std::cin.good()) break;

            if (content_length == 0) continue;

            std::vector<char> buffer(content_length);
            std::cin.read(buffer.data(), content_length);

            try {
                auto request = json::parse(buffer.begin(), buffer.end());
                handle_request(request);
            } catch (const std::exception& e) {
                std::cerr << "LSP Parse Error: " << e.what() << std::endl;
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
        std::cout << "Content-Length: " << body.length() << "\r\n\r\n"
                  << body << std::flush;
    }

    void send_notification(std::string method, json params)
    {
        json notification = {
            {"jsonrpc", "2.0"},
            {"method", method},
            {"params", params}};
        std::string body = notification.dump();
        std::cout << "Content-Length: " << body.length() << "\r\n\r\n"
                  << body << std::flush;
    }

    void handle_request(const json& req)
    {
        if (!req.contains("method")) return;
        std::string method = req["method"];

        if (method == "initialize") {
            json result = {
                {"capabilities", {{"textDocumentSync", 1}, {"hoverProvider", true}, {"definitionProvider", true}, {"completionProvider", {{"resolveProvider", false}, {"triggerCharacters", {".", "!", "("}}}}, {"semanticTokensProvider", {{"legend", {{"tokenTypes", {"type", "enumMember", "variable", "parameter", "function", "keyword", "comment", "string", "number", "operator"}}, {"tokenModifiers", {}}}}, {"full", true}}}}}};
            send_response(req["id"], result);
        } else if (method == "textDocument/didOpen") {
            std::string uri = req["params"]["textDocument"]["uri"];
            std::string text = req["params"]["textDocument"]["text"];
            open_buffers[uri] = text;
            publish_diagnostics(uri);
        } else if (method == "textDocument/didChange") {
            std::string uri = req["params"]["textDocument"]["uri"];
            std::string text = req["params"]["contentChanges"][0]["text"];
            open_buffers[uri] = text;
            publish_diagnostics(uri);
        } else if (method == "textDocument/hover") {
            handle_hover(req);
        } else if (method == "textDocument/definition") {
            handle_definition(req);
        } else if (method == "textDocument/completion") {
            handle_completion(req);
        } else if (method == "textDocument/semanticTokens/full") {
            handle_semantic_tokens(req);
        } else if (method == "workspace/didChangeWatchedFiles") {
            // Invalidate everything on external file changes
            nw::kernel::runtime().evict_user_modules();
        } else if (method == "textDocument/didClose") {
            open_buffers.erase(std::string(req["params"]["textDocument"]["uri"]));
        } else if (method == "shutdown") {
            send_response(req["id"], nullptr);
        } else if (method == "exit") {
            std::exit(0);
        }
    }

    void handle_hover(const json& req)
    {
        auto& rt = nw::kernel::runtime();
        std::string uri = req["params"]["textDocument"]["uri"];
        int line = req["params"]["position"]["line"];
        int character = req["params"]["position"]["character"];

        std::string module_name = module_name_for_uri(rt, uri);
        lsp::Script* script = rt.get_module(module_name);
        if (!script) {
            send_response(req["id"], nullptr);
            return;
        }

        nw::smalls::AstLocator locator{script, "", static_cast<size_t>(line + 1), static_cast<size_t>(character)};
        locator.visit(&script->ast());

        if (locator.found_) {
            std::string content = "```smalls\n";
            if (!locator.result_.type.empty()) {
                content += "(" + locator.result_.type + ") ";
            }
            content += std::string(locator.result_.view) + "\n```";
            if (!locator.result_.comment.empty()) {
                content += "\n\n---\n\n" + locator.result_.comment;
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
        std::string uri = req["params"]["textDocument"]["uri"];
        int line = req["params"]["position"]["line"];
        int character = req["params"]["position"]["character"];

        std::string module_name = module_name_for_uri(rt, uri);
        lsp::Script* script = rt.get_module(module_name);
        if (!script) {
            send_response(req["id"], nullptr);
            return;
        }

        nw::smalls::AstLocator locator{script, "", static_cast<size_t>(line + 1), static_cast<size_t>(character)};
        locator.visit(&script->ast());

        if (locator.found_ && locator.result_.decl) {
            auto* decl = locator.result_.decl;
            std::string target_uri;
            if (locator.result_.provider) {
                std::string provider_name(locator.result_.provider->name());

                if (provider_name.rfind("file://", 0) == 0) {
                    target_uri = provider_name;
                } else {
                    for (const auto& [open_uri, _] : open_buffers) {
                        if (module_name_for_uri(rt, open_uri) == provider_name) {
                            target_uri = open_uri;
                            break;
                        }
                    }
                }
            }

            if (target_uri.empty()) {
                send_response(req["id"], nullptr);
                return;
            }

            json result = {
                {"uri", target_uri},
                {"range", make_lsp_range(decl->range())}};
            send_response(req["id"], result);
        } else {
            send_response(req["id"], nullptr);
        }
    }

    void handle_completion(const json& req)
    {
        auto& rt = nw::kernel::runtime();
        std::string uri = req["params"]["textDocument"]["uri"];
        int line = req["params"]["position"]["line"];
        int character = req["params"]["position"]["character"];

        std::string module_name = module_name_for_uri(rt, uri);
        lsp::Script* script = rt.get_module(module_name);
        if (!script) {
            send_response(req["id"], json::array());
            return;
        }

        lsp::CompletionContext context;
        script->complete_at("", static_cast<size_t>(line + 1), static_cast<size_t>(character), context, true);

        json result = json::array();
        for (const auto& sym : context.completions) {
            int kind = 1; // Text
            switch (sym.kind) {
            case lsp::SymbolKind::variable:
                kind = 6;
                break;
            case lsp::SymbolKind::function:
                kind = 3;
                break;
            case lsp::SymbolKind::type:
                kind = 7;
                break;
            case lsp::SymbolKind::param:
                kind = 6;
                break;
            case lsp::SymbolKind::field:
                kind = 5;
                break;
            }

            json item = {
                {"label", sym.view},
                {"kind", kind},
                {"detail", sym.type},
                {"documentation", sym.comment}};
            result.push_back(item);
        }

        send_response(req["id"], result);
    }

    void handle_semantic_tokens(const json& req)
    {
        auto& rt = nw::kernel::runtime();
        std::string uri = req["params"]["textDocument"]["uri"];

        std::string module_name = module_name_for_uri(rt, uri);
        lsp::Script* script = rt.get_module(module_name);
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
            int delta_line = token.line - last_line;
            int delta_start = delta_line == 0 ? token.start - last_start : token.start;

            data.push_back(delta_line);
            data.push_back(delta_start);
            data.push_back(token.length);
            data.push_back(token.type);
            data.push_back(0);

            last_line = token.line;
            last_start = token.start;
        }

        send_response(req["id"], {{"data", data}});
    }

    void publish_diagnostics(const std::string& uri)
    {
        auto& rt = nw::kernel::runtime();
        std::string text = open_buffers[uri];
        std::string module_name = module_name_for_uri(rt, uri);

        if (auto root = module_root_for_uri(uri); !root.empty()) {
            rt.add_module_path(root);
        }

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

            json lsp_diag = {
                {"range", make_lsp_range(diag.location)},
                {"message", diag.message},
                {"severity", severity},
                {"source", "smalls"}};
            lsp_diags.push_back(lsp_diag);
        }

        json params = {
            {"uri", uri},
            {"diagnostics", lsp_diags}};
        send_notification("textDocument/publishDiagnostics", params);
    }
};

int main(int argc, char* argv[])
{
    loguru::g_stderr_verbosity = loguru::Verbosity_INFO;
    nw::init_logger(argc, argv);

    nw::kernel::config().initialize();
    nw::kernel::services().start();

    auto& rt = nw::kernel::runtime();
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-I" || arg == "--module-path") && i + 1 < argc) {
            rt.add_module_path(argv[++i]);
        }
    }

    LspServer server;
    server.run();

    nw::kernel::services().shutdown();
    return 0;
}
