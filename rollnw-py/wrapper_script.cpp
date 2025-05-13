#include "casters.hpp"
#include "opaque_types.hpp"

#include <nw/resources/ResourceManager.hpp>
#include <nw/script/Nss.hpp>
#include <nw/script/NssLexer.hpp>
#include <nw/script/Token.hpp>

#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>
#include <pybind11/stl_bind.h>

#include <string>

namespace py = pybind11;
namespace nws = nw::script;

using namespace std::literals;

void init_script(py::module& nw)
{
    py::class_<nws::SourcePosition>(nw, "SourcePosition")
        .def_readwrite("line", &nws::SourcePosition::line)
        .def_readwrite("column", &nws::SourcePosition::column);

    py::class_<nws::SourceRange>(nw, "SourceRange")
        .def(py::init<>())
        .def_readonly("start", &nws::SourceRange::start)
        .def_readonly("end", &nws::SourceRange::end);

    py::class_<nws::SourceLocation>(nw, "SourceLocation")
        .def("length", &nws::SourceLocation::length)
        .def("view", &nws::SourceLocation::view)
        .def_readonly("range", &nws::SourceLocation::range);

    py::enum_<nws::SymbolKind>(nw, "SymbolKind")
        .value("variable", nws::SymbolKind::variable)
        .value("function", nws::SymbolKind::function)
        .value("type", nws::SymbolKind::type)
        .value("param", nws::SymbolKind::param)
        .value("field", nws::SymbolKind::field);

    py::class_<nws::Symbol>(nw, "Symbol")
        .def_readonly("node", &nws::Symbol::node)
        .def_readonly("decl", &nws::Symbol::decl)
        .def_readonly("comment", &nws::Symbol::comment)
        .def_readonly("type", &nws::Symbol::type)
        .def_readonly("kind", &nws::Symbol::kind)
        .def_readonly("provider", &nws::Symbol::provider, py::return_value_policy::reference_internal)
        .def_readonly("view", &nws::Symbol::view);

    py::class_<nws::InlayHint>(nw, "InlayHint")
        .def_readonly("message", &nws::InlayHint::message)
        .def_readonly("position", &nws::InlayHint::position);

    py::class_<nws::SignatureHelp>(nw, "SignatureHelp")
        .def_readonly("decl", &nws::SignatureHelp::decl)
        .def_readonly("expr", &nws::SignatureHelp::expr)
        .def_readonly("active_param", &nws::SignatureHelp::active_param);

    py::class_<nws::Context>(nw, "Context")
        .def(py::init<>())
        .def(py::init([](py::list& includes) {
            std::vector<std::string> incs;
            for (const auto& include : includes) {
                incs.push_back(include.cast<std::string>());
            }
            return new nws::Context(incs);
        }))
        .def(py::init([](py::list& includes, std::string command_script) {
            std::vector<std::string> incs;
            for (const auto& include : includes) {
                incs.push_back(include.cast<std::string>());
            }
            return new nws::Context(incs, command_script);
        }))
        .def("add_include_path", &nws::Context::add_include_path)
        .def("command_script", &nws::Context::command_script, py::return_value_policy::reference_internal)
        .def("get", &nws::Context::get, py::return_value_policy::reference_internal,
            py::arg("resref"), py::arg("is_command_script") = false);

    py::enum_<nw::script::DiagnosticType>(nw, "DiagnosticType")
        .value("lexical", nws::DiagnosticType::lexical)
        .value("parse", nws::DiagnosticType::parse)
        .value("semantic", nws::DiagnosticType::semantic);

    py::enum_<nw::script::DiagnosticSeverity>(nw, "DiagnosticSeverity")
        .value("error", nws::DiagnosticSeverity::error)
        .value("hint", nws::DiagnosticSeverity::hint)
        .value("information", nws::DiagnosticSeverity::information)
        .value("warning", nws::DiagnosticSeverity::warning);

    py::class_<nws::Diagnostic>(nw, "Diagnostic")
        .def_readonly("type", &nws::Diagnostic::type)
        .def_readonly("severity", &nws::Diagnostic::severity)
        .def_readonly("script", &nws::Diagnostic::script)
        .def_readonly("message", &nws::Diagnostic::message)
        .def_readonly("location", &nws::Diagnostic::location);

    py::enum_<nws::NssTokenType>(nw, "NssTokenType")
        .value("INVALID", nws::NssTokenType::INVALID)
        .value("END", nws::NssTokenType::END)
        // Identifier
        .value("IDENTIFIER", nws::NssTokenType::IDENTIFIER)
        // Punctuation
        .value("LPAREN", nws::NssTokenType::LPAREN)       // (
        .value("RPAREN", nws::NssTokenType::RPAREN)       // )
        .value("LBRACE", nws::NssTokenType::LBRACE)       // {
        .value("RBRACE", nws::NssTokenType::RBRACE)       // }
        .value("LBRACKET", nws::NssTokenType::LBRACKET)   // [
        .value("RBRACKET", nws::NssTokenType::RBRACKET)   // ]
        .value("COMMA", nws::NssTokenType::COMMA)         //
        .value("COLON", nws::NssTokenType::COLON)         // :
        .value("QUESTION", nws::NssTokenType::QUESTION)   // ?
        .value("SEMICOLON", nws::NssTokenType::SEMICOLON) // ;
        .value("POUND", nws::NssTokenType::POUND)         // #
        .value("DOT", nws::NssTokenType::DOT)             // .
        // Operators
        .value("AND", nws::NssTokenType::AND)               // '&'
        .value("ANDAND", nws::NssTokenType::ANDAND)         // &&
        .value("ANDEQ", nws::NssTokenType::ANDEQ)           // &=
        .value("DIV", nws::NssTokenType::DIV)               // /
        .value("DIVEQ", nws::NssTokenType::DIVEQ)           // /=
        .value("EQ", nws::NssTokenType::EQ)                 // =
        .value("EQEQ", nws::NssTokenType::EQEQ)             // ==
        .value("GT", nws::NssTokenType::GT)                 // >
        .value("GTEQ", nws::NssTokenType::GTEQ)             // >=
        .value("LT", nws::NssTokenType::LT)                 // <
        .value("LTEQ", nws::NssTokenType::LTEQ)             // <=
        .value("MINUS", nws::NssTokenType::MINUS)           // -
        .value("MINUSEQ", nws::NssTokenType::MINUSEQ)       // -=
        .value("MINUSMINUS", nws::NssTokenType::MINUSMINUS) // --
        .value("MOD", nws::NssTokenType::MOD)               // %
        .value("MODEQ", nws::NssTokenType::MODEQ)           // %=
        .value("TIMES", nws::NssTokenType::TIMES)           // *
        .value("TIMESEQ", nws::NssTokenType::TIMESEQ)       // *=
        .value("NOT", nws::NssTokenType::NOT)               // !
        .value("NOTEQ", nws::NssTokenType::NOTEQ)           // !=
        .value("OR", nws::NssTokenType::OR)                 // '|'
        .value("OREQ", nws::NssTokenType::OREQ)             // |=
        .value("OROR", nws::NssTokenType::OROR)             // ||
        .value("PLUS", nws::NssTokenType::PLUS)             // +
        .value("PLUSEQ", nws::NssTokenType::PLUSEQ)         // +=
        .value("PLUSPLUS", nws::NssTokenType::PLUSPLUS)     // ++
        .value("SL", nws::NssTokenType::SL)                 // <<
        .value("SLEQ", nws::NssTokenType::SLEQ)             // <<=
        .value("SR", nws::NssTokenType::SR)                 // >>
        .value("SREQ", nws::NssTokenType::SREQ)             // >>=
        .value("TILDE", nws::NssTokenType::TILDE)           // ~
        .value("USR", nws::NssTokenType::USR)               // >>>
        .value("USREQ", nws::NssTokenType::USREQ)           // >>>=
        .value("XOR", nws::NssTokenType::XOR)               // ^
        .value("XOREQ", nws::NssTokenType::XOREQ)           // ^=
        // Constants
        .value("FLOAT_CONST", nws::NssTokenType::FLOAT_CONST)
        .value("INTEGER_CONST", nws::NssTokenType::INTEGER_CONST)
        .value("OBJECT_INVALID_CONST", nws::NssTokenType::OBJECT_INVALID_CONST)
        .value("OBJECT_SELF_CONST", nws::NssTokenType::OBJECT_SELF_CONST)
        .value("STRING_CONST", nws::NssTokenType::STRING_CONST)
        .value("STRING_RAW_CONST", nws::NssTokenType::STRING_RAW_CONST)
        // Keywords
        .value("ACTION", nws::NssTokenType::ACTION)                     // action
        .value("BREAK", nws::NssTokenType::BREAK)                       // break
        .value("CASE", nws::NssTokenType::CASE)                         // case
        .value("CASSOWARY", nws::NssTokenType::CASSOWARY)               // cassowary
        .value("CONST", nws::NssTokenType::CONST_)                      // const
        .value("CONTINUE", nws::NssTokenType::CONTINUE)                 // continue
        .value("DEFAULT", nws::NssTokenType::DEFAULT)                   // default
        .value("DO", nws::NssTokenType::DO)                             // do
        .value("EFFECT", nws::NssTokenType::EFFECT)                     // effect
        .value("ELSE", nws::NssTokenType::ELSE)                         // else
        .value("EVENT", nws::NssTokenType::EVENT)                       // event
        .value("FLOAT", nws::NssTokenType::FLOAT)                       // float
        .value("FOR", nws::NssTokenType::FOR)                           // for
        .value("IF", nws::NssTokenType::IF)                             // if
        .value("INT", nws::NssTokenType::INT)                           // int
        .value("ITEMPROPERTY", nws::NssTokenType::ITEMPROPERTY)         // itemproperty
        .value("JSON", nws::NssTokenType::JSON)                         // json
        .value("JSON_CONST", nws::NssTokenType::JSON_CONST)             // json constant
        .value("LOCATION", nws::NssTokenType::LOCATION)                 // location
        .value("LOCATION_INVALID", nws::NssTokenType::LOCATION_INVALID) // location invalid const
        .value("OBJECT", nws::NssTokenType::OBJECT)                     // object
        .value("RETURN", nws::NssTokenType::RETURN)                     // return
        .value("STRING", nws::NssTokenType::STRING)                     // string
        .value("STRUCT", nws::NssTokenType::STRUCT)                     // struct
        .value("SQLQUERY", nws::NssTokenType::SQLQUERY)                 // sqlquery
        .value("SWITCH", nws::NssTokenType::SWITCH)                     // switch
        .value("TALENT", nws::NssTokenType::TALENT)                     // talent
        .value("VECTOR", nws::NssTokenType::VECTOR)                     // vector
        .value("VOID", nws::NssTokenType::VOID_)                        // void
        .value("WHILE", nws::NssTokenType::WHILE);                      // while

    py::class_<nws::Type>(nw, "Type")
        .def_readonly("type_qualifier", &nws::Type::type_qualifier)
        .def_readonly("type_specifier", &nws::Type::type_specifier)
        .def_readonly("struct_id", &nws::Type::struct_id);

    py::class_<nws::NssToken>(nw, "NssToken")
        .def("__repr__", [](const nws::NssToken& tk) {
            std::stringstream ss;
            ss << tk;
            return ss.str();
        })
        .def_readonly("type", &nws::NssToken::type)
        .def_readonly("loc", &nws::NssToken::loc);

    py::class_<nws::NssLexer>(nw, "NssLexer")
        .def(py::init<std::string_view, nws::Context*>(),
            py::keep_alive<1, 3>())
        .def("next", &nws::NssLexer::next)
        .def("current", &nws::NssLexer::current);

    py::class_<nws::Export>(nw, "Export")
        .def_property_readonly(
            "decl", [](const nws::Export& self) { return self.decl; },
            py::return_value_policy::reference_internal)
        .def_property_readonly(
            "type", [](const nws::Export& self) { return static_cast<nws::Declaration*>(self.type); },
            py::return_value_policy::reference_internal)
        .def_readonly("command_id", &nws::Export::command_id);

    py::class_<nws::Nss>(nw, "Nss")
        .def(py::init<std::filesystem::path, nws::Context*, bool>(),
            py::arg("path"), py::arg("ctx"), py::arg("is_command_script") = false,
            py::keep_alive<1, 3>())
        .def(
            "ast", [](nws::Nss& self) {
                return &self.ast();
            },
            py::return_value_policy::reference_internal)
        .def("complete", [](const nws::Nss& self, const std::string& needle, bool no_filter) {
                nws::CompletionContext out;
                self.complete(needle, out, no_filter);
                return std::move(out.completions); })
        .def("complete_at", [](nws::Nss& self, const std::string& needle, size_t line, size_t character, bool no_filter) {
                nws::CompletionContext out;
                self.complete_at(needle, line, character, out, no_filter);
                return std::move(out.completions); })
        .def("complete_dot", [](nws::Nss& self, const std::string& needle, size_t line, size_t character, bool no_filter) {
                std::vector<nw::script::Symbol> out;
                self.complete_dot(needle, line, character, out, no_filter);
                return out; })
        .def("dependencies", &nws::Nss::dependencies)
        .def("diagnostics", &nws::Nss::diagnostics)
        .def("errors", &nws::Nss::errors)
        .def("exports", [](const nws::Nss& self) {
                std::vector<nws::Symbol> result;
                for (const auto& [key, exp] : self.exports()) {
                    if (exp.decl) { result.push_back(self.declaration_to_symbol(exp.decl)); }
                    if (exp.type) { result.push_back(self.declaration_to_symbol(exp.type)); }
                }
                return result; }, py::return_value_policy::reference_internal)
        .def("inlay_hints", &nws::Nss::inlay_hints)
        .def("locate_export", &nws::Nss::locate_export, py::arg("symbol"), py::arg("is_type"), py::arg("search_dependancies") = false, py::return_value_policy::reference_internal)
        .def("locate_symbol", &nws::Nss::locate_symbol, py::return_value_policy::reference_internal)
        .def("name", &nws::Nss::name)
        .def("parse", &nws::Nss::parse)
        .def("process_includes", &nws::Nss::process_includes, py::arg("parent") = nullptr)
        .def("resolve", &nws::Nss::resolve)
        .def("signature_help", &nws::Nss::signature_help, py::return_value_policy::reference_internal)
        .def("type_name", [](const nws::Nss& self, const nws::AstNode* node) {
            if (!node) { return ""sv; }
            return self.ctx() ? self.ctx()->type_name(node->type_id_) : ""sv; })
        .def("view_from_range", &nws::Nss::view_from_range)
        .def("warnings", &nws::Nss::warnings)
        .def_static("from_string", [](std::string_view str, nws::Context* ctx, bool command_script) { return new nws::Nss(str, ctx, command_script); }, py::arg("str"), py::arg("ctx"), py::arg("is_command_script") = false, py::keep_alive<0, 2>());

    py::class_<nws::Include>(nw, "Include")
        .def_readonly("resref", &nws::Include::resref)
        .def_readonly("location", &nws::Include::location)
        .def_readonly("script", &nws::Include::script)
        .def_readonly("used", &nws::Include::used);

    py::class_<nws::Comment>(nw, "Comment")
        .def("__str__", [](const nws::Comment& self) { return self.comment_; });

    py::class_<nws::Ast>(nw, "Ast")
        .def(
            "__getitem__", [](const nws::Ast& self, size_t index) {
                return self.decls[index];
            },
            py::return_value_policy::reference_internal)
        .def("__len__", [](const nws::Ast& self) { return self.decls.size(); })
        .def("__iter__", [](const nws::Ast& self) { return py::make_iterator(self.decls.begin(), self.decls.end()); }, py::keep_alive<0, 1>())
        .def_readonly("defines", &nws::Ast::defines)
        .def_readonly("includes", &nws::Ast::includes)
        .def_readonly("comments", &nws::Ast::comments)
        .def("find_comment", &nws::Ast::find_comment);

    py::class_<nws::AstNode>(nw, "AstNode")
        .def(
            "complete", [](nws::AstNode& self, const std::string& needle) {
                std::vector<const nws::Declaration*> out;
                self.complete(needle, out);
                return out;
            },
            py::return_value_policy::reference_internal);

    py::class_<nws::Expression, nws::AstNode>(nw, "Expression");

    py::class_<nws::AssignExpression, nws::Expression>(nw, "AssignExpression")
        .def_readonly("operator", &nws::AssignExpression::op)
        .def_property_readonly(
            "lhs", [](nws::AssignExpression& self) { return self.lhs; },
            py::return_value_policy::reference_internal)
        .def_property_readonly(
            "rhs", [](nws::AssignExpression& self) { return self.rhs; },
            py::return_value_policy::reference_internal);

    py::class_<nws::BinaryExpression, nws::Expression>(nw, "BinaryExpression")
        .def_readonly("operator", &nws::BinaryExpression::op)
        .def_property_readonly(
            "lhs", [](nws::BinaryExpression& self) { return self.lhs; },
            py::return_value_policy::reference_internal)
        .def_property_readonly(
            "rhs", [](nws::BinaryExpression& self) { return self.rhs; },
            py::return_value_policy::reference_internal);

    py::class_<nws::CallExpression, nws::Expression>(nw, "CallExpression")
        .def_property_readonly(
            "expr", [](nws::CallExpression& expr) { return expr.expr; },
            py::return_value_policy::reference_internal)
        .def("__len__", [](nws::CallExpression& self) { return self.args.size(); })
        .def(
            "__getitem__", [](nws::CallExpression& self, size_t idx) { return self.args[idx]; },
            py::return_value_policy::reference_internal)
        .def(
            "__iter__", [](nws::CallExpression& self) {
                return py::make_iterator(self.args.begin(), self.args.end());
            },
            py::keep_alive<0, 1>());

    py::class_<nws::ComparisonExpression, nws::Expression>(nw, "ComparisonExpression")
        .def_readonly("operator", &nws::ComparisonExpression::op)
        .def_property_readonly(
            "lhs", [](nws::ComparisonExpression& self) { return self.lhs; },
            py::return_value_policy::reference_internal)
        .def_property_readonly(
            "rhs", [](nws::ComparisonExpression& self) { return self.rhs; },
            py::return_value_policy::reference_internal);

    py::class_<nws::ConditionalExpression, nws::Expression>(nw, "ConditionalExpression")
        .def_property_readonly(
            "test", [](nws::ConditionalExpression& self) { return self.test; },
            py::return_value_policy::reference_internal)
        .def_property_readonly(
            "true_branch", [](nws::ConditionalExpression& self) { return self.true_branch; },
            py::return_value_policy::reference_internal)
        .def_property_readonly(
            "false_branch", [](nws::ConditionalExpression& self) { return self.false_branch; },
            py::return_value_policy::reference_internal);

    py::class_<nws::DotExpression, nws::Expression>(nw, "DotExpression")
        .def_property_readonly(
            "lhs", [](nws::DotExpression& self) { return self.lhs; },
            py::return_value_policy::reference_internal)
        .def_property_readonly(
            "rhs", [](nws::DotExpression& self) { return self.rhs; },
            py::return_value_policy::reference_internal);

    py::class_<nws::EmptyExpression, nws::Expression>(nw, "EmptyExpression");

    py::class_<nws::GroupingExpression, nws::Expression>(nw, "GroupingExpression")
        .def_property_readonly(
            "expr", [](nws::GroupingExpression& self) { return self.expr; },
            py::return_value_policy::reference_internal);

    py::class_<nws::LiteralExpression, nws::Expression>(nw, "LiteralExpression")
        .def_readonly("literal", &nws::LiteralExpression::literal)
        .def_readonly("data", &nws::LiteralExpression::data);

    py::class_<nws::LiteralVectorExpression, nws::Expression>(nw, "LiteralVectorExpression")
        .def_readonly("x", &nws::LiteralVectorExpression::x)
        .def_readonly("y", &nws::LiteralVectorExpression::y)
        .def_readonly("z", &nws::LiteralVectorExpression::z);

    py::class_<nws::LogicalExpression, nws::Expression>(nw, "LogicalExpression")
        .def_readonly("operator", &nws::LogicalExpression::op)
        .def_property_readonly(
            "lhs", [](nws::LogicalExpression& self) { return self.lhs; },
            py::return_value_policy::reference_internal)
        .def_property_readonly(
            "rhs", [](nws::LogicalExpression& self) { return self.rhs; },
            py::return_value_policy::reference_internal);

    py::class_<nws::PostfixExpression, nws::Expression>(nw, "PostfixExpression")
        .def_readonly("operator", &nws::PostfixExpression::op)
        .def_property_readonly(
            "lhs", [](nws::PostfixExpression& self) { return self.lhs; },
            py::return_value_policy::reference_internal);

    py::class_<nws::UnaryExpression, nws::Expression>(nw, "UnaryExpression")
        .def_readonly("operator", &nws::UnaryExpression::op)
        .def_property_readonly(
            "rhs", [](nws::UnaryExpression& self) { return self.rhs; },
            py::return_value_policy::reference_internal);

    py::class_<nws::VariableExpression, nws::Expression>(nw, "VariableExpression")
        .def_readonly("var", &nws::VariableExpression::var);

    py::class_<nws::Statement, nws::AstNode>(nw, "Statement");

    py::class_<nws::BlockStatement, nws::Statement>(nw, "BlockStatement")
        .def("__len__", [](nws::BlockStatement& self) { return self.nodes.size(); })
        .def(
            "__getitem__", [](nws::BlockStatement& self, size_t idx) { return self.nodes[idx]; },
            py::return_value_policy::reference_internal)
        .def(
            "__iter__", [](const nws::BlockStatement& self) {
                return py::make_iterator(self.nodes.begin(), self.nodes.end());
            },
            py::keep_alive<0, 1>())
        .def_readonly("range", &nws::BlockStatement::range_);

    py::class_<nws::DoStatement, nws::Statement>(nw, "DoStatement")
        .def_property_readonly(
            "block", [](nws::DoStatement& self) { return self.block; },
            py::return_value_policy::reference_internal)
        .def_property_readonly(
            "test", [](nws::DoStatement& self) { return self.expr; },
            py::return_value_policy::reference_internal);

    py::class_<nws::EmptyStatement, nws::Statement>(nw, "EmptyStatement");

    py::class_<nws::ExprStatement, nws::Statement>(nw, "ExprStatement")
        .def_property_readonly(
            "expr", [](nws::LabelStatement& self) { return self.expr; },
            py::return_value_policy::reference_internal);

    py::class_<nws::IfStatement, nws::Statement>(nw, "IfStatement")
        .def_property_readonly(
            "test", [](nws::IfStatement& self) { return self.expr; },
            py::return_value_policy::reference_internal)
        .def_property_readonly(
            "true_branch", [](nws::IfStatement& self) { return self.if_branch; },
            py::return_value_policy::reference_internal)
        .def_property_readonly(
            "false_branch", [](nws::IfStatement& self) { return self.else_branch; },
            py::return_value_policy::reference_internal);

    py::class_<nws::ForStatement, nws::Statement>(nw, "ForStatement")
        .def_property_readonly(
            "init", [](nws::ForStatement& self) { return self.init; },
            py::return_value_policy::reference_internal)
        .def_property_readonly(
            "test", [](nws::ForStatement& self) { return self.check; },
            py::return_value_policy::reference_internal)
        .def_property_readonly(
            "increment", [](nws::ForStatement& self) { return self.inc; },
            py::return_value_policy::reference_internal)
        .def_property_readonly(
            "block", [](nws::ForStatement& self) { return self.block; },
            py::return_value_policy::reference_internal);

    py::class_<nws::JumpStatement, nws::Statement>(nw, "JumpStatement")
        .def_readonly("operator", &nws::JumpStatement::op)
        .def_property_readonly(
            "expr", [](nws::JumpStatement& self) { return self.expr; },
            py::return_value_policy::reference_internal);

    py::class_<nws::LabelStatement, nws::Statement>(nw, "LabelStatement")
        .def_readonly("label", &nws::LabelStatement::type)
        .def_property_readonly(
            "expr", [](nws::LabelStatement& self) { return self.expr; },
            py::return_value_policy::reference_internal);

    py::class_<nws::SwitchStatement, nws::Statement>(nw, "SwitchStatement")
        .def_property_readonly(
            "block", [](nws::SwitchStatement& self) { return self.block; },
            py::return_value_policy::reference_internal)
        .def_property_readonly(
            "target", [](nws::SwitchStatement& self) { return self.target; },
            py::return_value_policy::reference_internal);

    py::class_<nws::WhileStatement, nws::Statement>(nw, "WhileStatement")
        .def_property_readonly(
            "block", [](nws::WhileStatement& self) { return self.block; },
            py::return_value_policy::reference_internal)
        .def_property_readonly(
            "test", [](nws::WhileStatement& self) { return self.check; },
            py::return_value_policy::reference_internal);

    py::class_<nws::Declaration, nws::Statement>(nw, "Declaration")
        .def("identifier", &nws::Declaration::identifier,
            "Gets the declarations identifier")
        .def_readonly("type", &nws::Declaration::type)
        .def("range", &nws::Declaration::range)
        .def("selection_range", &nws::Declaration::selection_range);

    py::class_<nws::FunctionDecl, nws::Declaration>(nw, "FunctionDecl")
        .def("__len__", [](nws::FunctionDecl& self) { return self.params.size(); })
        .def(
            "__getitem__", [](nws::FunctionDecl& self, size_t idx) { return self.params[idx]; },
            py::return_value_policy::reference_internal)
        .def(
            "__iter__", [](const nws::FunctionDecl& self) {
                return py::make_iterator(self.params.begin(), self.params.end());
            },
            py::keep_alive<0, 1>());

    py::class_<nws::FunctionDefinition, nws::Declaration>(nw, "FunctionDefinition")
        .def_property_readonly(
            "decl", [](nws::FunctionDefinition& self) { return self.decl_inline; },
            py::return_value_policy::reference_internal)
        .def_property_readonly(
            "block", [](nws::FunctionDefinition& self) { return self.block; },
            py::return_value_policy::reference_internal);

    py::class_<nws::StructDecl, nws::Declaration>(nw, "StructDecl")
        .def("__len__", [](nws::StructDecl& self) { return self.decls.size(); })
        .def(
            "__getitem__", [](nws::StructDecl& self, size_t idx) { return self.decls[idx]; },
            py::return_value_policy::reference_internal)
        .def(
            "__iter__", [](const nws::StructDecl& self) {
                return py::make_iterator(self.decls.begin(), self.decls.end());
            },
            py::keep_alive<0, 1>())
        .def("locate_member_decl", &nws::StructDecl::locate_member_decl, py::return_value_policy::reference_internal);

    py::class_<nws::VarDecl, nws::Declaration>(nw, "VarDecl")
        .def_property_readonly(
            "init", [](nws::VarDecl& self) { return self.init; },
            py::return_value_policy::reference_internal);

    py::class_<nws::DeclList, nws::Declaration>(nw, "DeclList")
        .def("__len__", [](nws::DeclList& self) { return self.decls.size(); })
        .def(
            "__getitem__", [](nws::DeclList& self, size_t idx) { return self.decls[idx]; },
            py::return_value_policy::reference_internal)
        .def(
            "__iter__", [](const nws::BlockStatement& self) {
                return py::make_iterator(self.nodes.begin(), self.nodes.end());
            },
            py::keep_alive<0, 1>())
        .def("locate_decl", &nws::DeclList::locate_decl, py::return_value_policy::reference_internal);
}
