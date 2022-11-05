#include "opaque_types.hpp"

#include <nw/script/Nss.hpp>
#include <pybind11/functional.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl_bind.h>

#include <string>
#include <strstream>
#include <variant>

namespace py = pybind11;
namespace nws = nw::script;

void init_script(py::module& nw)
{
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
        // Keywords
        .value("ACTION", nws::NssTokenType::ACTION)             // action
        .value("BREAK", nws::NssTokenType::BREAK)               // break
        .value("CASE", nws::NssTokenType::CASE)                 // case
        .value("CASSOWARY", nws::NssTokenType::CASSOWARY)       // cassowary
        .value("CONST", nws::NssTokenType::CONST_)              // const
        .value("CONTINUE", nws::NssTokenType::CONTINUE)         // continue
        .value("DEFAULT", nws::NssTokenType::DEFAULT)           // default
        .value("DO", nws::NssTokenType::DO)                     // do
        .value("EFFECT", nws::NssTokenType::EFFECT)             // effect
        .value("ELSE", nws::NssTokenType::ELSE)                 // else
        .value("EVENT", nws::NssTokenType::EVENT)               // event
        .value("FLOAT", nws::NssTokenType::FLOAT)               // float
        .value("FOR", nws::NssTokenType::FOR)                   // for
        .value("IF", nws::NssTokenType::IF)                     // if
        .value("INT", nws::NssTokenType::INT)                   // int
        .value("ITEMPROPERTY", nws::NssTokenType::ITEMPROPERTY) // itemproperty
        .value("JSON", nws::NssTokenType::JSON)                 // json
        .value("LOCATION", nws::NssTokenType::LOCATION)         // location
        .value("OBJECT", nws::NssTokenType::OBJECT)             // object
        .value("RETURN", nws::NssTokenType::RETURN)             // return
        .value("STRING", nws::NssTokenType::STRING)             // string
        .value("STRUCT", nws::NssTokenType::STRUCT)             // struct
        .value("SQLQUERY", nws::NssTokenType::SQLQUERY)         // sqlquery
        .value("SWITCH", nws::NssTokenType::SWITCH)             // switch
        .value("TALENT", nws::NssTokenType::TALENT)             // talent
        .value("VECTOR", nws::NssTokenType::VECTOR)             // vector
        .value("VOID", nws::NssTokenType::VOID_)                // void
        .value("WHILE", nws::NssTokenType::WHILE);              // while

    py::class_<nws::NssToken>(nw, "NssToken")
        .def("__repr__", [](const nws::NssToken& tk) {
            std::stringstream ss;
            ss << tk;
            return ss.str();
        })
        .def_readonly("type", &nws::NssToken::type)
        .def_readonly("id", &nws::NssToken::id)
        .def_readonly("line", &nws::NssToken::line)
        .def_readonly("start", &nws::NssToken::start)
        .def_readonly("end", &nws::NssToken::end);

    py::class_<nws::NssLexer>(nw, "NssLexer")
        .def(py::init<std::string_view>())
        .def("next", &nws::NssLexer::next)
        .def("current", &nws::NssLexer::current);

    py::class_<nws::Type>(nw, "Type")
        .def_readonly("type_qualifier", &nws::Type::type_qualifier)
        .def_readonly("type_specifier", &nws::Type::type_specifier)
        .def_readonly("struct_id", &nws::Type::struct_id); // Only set if type_specifier if of type NssTokenType::STRUCT

    py::class_<nws::Nss>(nw, "Nss")
        .def(py::init<std::filesystem::path>())
        .def("errors", &nws::Nss::errors)
        .def("warnings", &nws::Nss::warnings)
        .def("set_error_callback", [](nws::Nss& self, std::function<void(std::string_view, nws::NssToken)> cb) {
            self.parser().set_error_callback(std::move(cb));
        })
        .def("set_warning_callback", [](nws::Nss& self, std::function<void(std::string_view, nws::NssToken)> cb) {
            self.parser().set_warning_callback(std::move(cb));
        })
        .def(
            "parse", [](nws::Nss& self) {
                self.parse();
                return &self.script();
            },
            py::return_value_policy::reference_internal)
        .def(
            "script", [](nws::Nss& self) {
                return &self.script();
            },
            py::return_value_policy::reference_internal)
        .def_static("from_string", [](std::string_view str) {
            return new nws::Nss(str);
        });

    py::class_<nws::Script>(nw, "Script")
        .def(
            "__getitem__", [](const nws::Script& self, size_t index) {
                return self.decls[index].get();
            },
            py::return_value_policy::reference_internal)
        .def("__len__", [](const nws::Script& self) { return self.decls.size(); })
        .def("__iter__", [](const nws::Script& self) {
            auto pylist = py::list();
            for (auto& ptr : self.decls) {
                auto pyobj = py::cast(*ptr, py::return_value_policy::reference);
                pylist.append(pyobj);
            }
            return py::iter(pylist);
        })
        .def_readonly("defines", &nws::Script::defines)
        .def_readonly("includes", &nws::Script::includes);

    py::class_<nws::AstNode>(nw, "AstNode")
        .def("accept", &nws::AstNode::accept);

    py::class_<nws::Expression, nws::AstNode>(nw, "Expression");

    py::class_<nws::AssignExpression, nws::Expression>(nw, "AssignExpression")
        .def_readonly("operator", &nws::AssignExpression::op)
        .def_property_readonly(
            "lhs", [](nws::AssignExpression& self) { return self.lhs.get(); },
            py::return_value_policy::reference_internal)
        .def_property_readonly(
            "rhs", [](nws::AssignExpression& self) { return self.rhs.get(); },
            py::return_value_policy::reference_internal);

    py::class_<nws::BinaryExpression, nws::Expression>(nw, "BinaryExpression")
        .def_readonly("operator", &nws::BinaryExpression::op)
        .def_property_readonly(
            "lhs", [](nws::BinaryExpression& self) { return self.lhs.get(); },
            py::return_value_policy::reference_internal)
        .def_property_readonly(
            "rhs", [](nws::BinaryExpression& self) { return self.rhs.get(); },
            py::return_value_policy::reference_internal);

    py::class_<nws::CallExpression, nws::Expression>(nw, "CallExpression")
        .def_property_readonly(
            "expr", [](nws::CallExpression& expr) { return expr.expr.get(); },
            py::return_value_policy::reference_internal)
        .def("__len__", [](nws::CallExpression& self) { return self.args.size(); })
        .def(
            "__getitem__", [](nws::CallExpression& self, size_t idx) { return self.args[idx].get(); },
            py::return_value_policy::reference_internal);

    py::class_<nws::ConditionalExpression, nws::Expression>(nw, "ConditionalExpression")
        .def_property_readonly(
            "test", [](nws::ConditionalExpression& self) { return self.test.get(); },
            py::return_value_policy::reference_internal)
        .def_property_readonly(
            "true_branch", [](nws::ConditionalExpression& self) { return self.true_branch.get(); },
            py::return_value_policy::reference_internal)
        .def_property_readonly(
            "false_branch", [](nws::ConditionalExpression& self) { return self.false_branch.get(); },
            py::return_value_policy::reference_internal);

    py::class_<nws::DotExpression, nws::Expression>(nw, "DotExpression")
        .def_property_readonly(
            "lhs", [](nws::DotExpression& self) { return self.lhs.get(); },
            py::return_value_policy::reference_internal)
        .def_property_readonly(
            "rhs", [](nws::DotExpression& self) { return self.rhs.get(); },
            py::return_value_policy::reference_internal);

    py::class_<nws::GroupingExpression, nws::Expression>(nw, "GroupingExpression")
        .def_property_readonly(
            "expr", [](nws::GroupingExpression& self) { return self.expr.get(); },
            py::return_value_policy::reference_internal);

    py::class_<nws::LiteralExpression, nws::Expression>(nw, "LiteralExpression")
        .def_readonly("literal", &nws::LiteralExpression::literal);

    py::class_<nws::LiteralVectorExpression, nws::Expression>(nw, "LiteralVectorExpression")
        .def_readonly("x", &nws::LiteralVectorExpression::x)
        .def_readonly("y", &nws::LiteralVectorExpression::y)
        .def_readonly("z", &nws::LiteralVectorExpression::z);

    py::class_<nws::LogicalExpression, nws::Expression>(nw, "LogicalExpression")
        .def_readonly("operator", &nws::LogicalExpression::op)
        .def_property_readonly(
            "lhs", [](nws::LogicalExpression& self) { return self.lhs.get(); },
            py::return_value_policy::reference_internal)
        .def_property_readonly(
            "rhs", [](nws::LogicalExpression& self) { return self.rhs.get(); },
            py::return_value_policy::reference_internal);

    py::class_<nws::PostfixExpression, nws::Expression>(nw, "PostfixExpression")
        .def_readonly("operator", &nws::PostfixExpression::op)
        .def_property_readonly(
            "lhs", [](nws::PostfixExpression& self) { return self.lhs.get(); },
            py::return_value_policy::reference_internal);

    py::class_<nws::UnaryExpression, nws::Expression>(nw, "UnaryExpression")
        .def_readonly("operator", &nws::UnaryExpression::op)
        .def_property_readonly(
            "rhs", [](nws::UnaryExpression& self) { return self.rhs.get(); },
            py::return_value_policy::reference_internal);

    py::class_<nws::VariableExpression, nws::Expression>(nw, "VariableExpression")
        .def_readonly("var", &nws::VariableExpression::var);

    py::class_<nws::Statement, nws::AstNode>(nw, "Statement");

    py::class_<nws::BlockStatement, nws::Statement>(nw, "BlockStatement")
        .def("__len__", [](nws::BlockStatement& self) { return self.nodes.size(); })
        .def(
            "__getitem__", [](nws::BlockStatement& self, size_t idx) { return self.nodes[idx].get(); },
            py::return_value_policy::reference_internal);

    py::class_<nws::DeclStatement, nws::Statement>(nw, "DeclStatement")
        .def("__len__", [](nws::DeclStatement& self) { return self.decls.size(); })
        .def(
            "__getitem__", [](nws::DeclStatement& self, size_t idx) { return self.decls[idx].get(); },
            py::return_value_policy::reference_internal);

    py::class_<nws::DoStatement, nws::Statement>(nw, "DoStatement")
        .def_property_readonly(
            "block", [](nws::DoStatement& self) { return self.block.get(); },
            py::return_value_policy::reference_internal)
        .def_property_readonly(
            "test", [](nws::DoStatement& self) { return self.expr.get(); },
            py::return_value_policy::reference_internal);

    py::class_<nws::EmptyStatement, nws::Statement>(nw, "EmptyStatement");

    py::class_<nws::ExprStatement, nws::Statement>(nw, "ExprStatement")
        .def_property_readonly(
            "expr", [](nws::LabelStatement& self) { return self.expr.get(); },
            py::return_value_policy::reference_internal);

    py::class_<nws::IfStatement, nws::Statement>(nw, "IfStatement")
        .def_property_readonly(
            "test", [](nws::IfStatement& self) { return self.expr.get(); },
            py::return_value_policy::reference_internal)
        .def_property_readonly(
            "true_branch", [](nws::IfStatement& self) { return self.if_branch.get(); },
            py::return_value_policy::reference_internal)
        .def_property_readonly(
            "false_branch", [](nws::IfStatement& self) { return self.else_branch.get(); },
            py::return_value_policy::reference_internal);

    py::class_<nws::ForStatement, nws::Statement>(nw, "ForStatement")
        .def_property_readonly(
            "init", [](nws::ForStatement& self) { return self.init.get(); },
            py::return_value_policy::reference_internal)
        .def_property_readonly(
            "test", [](nws::ForStatement& self) { return self.check.get(); },
            py::return_value_policy::reference_internal)
        .def_property_readonly(
            "increment", [](nws::ForStatement& self) { return self.inc.get(); },
            py::return_value_policy::reference_internal)
        .def_property_readonly(
            "block", [](nws::ForStatement& self) { return self.block.get(); },
            py::return_value_policy::reference_internal);

    py::class_<nws::JumpStatement, nws::Statement>(nw, "JumpStatement")
        .def_readonly("operator", &nws::JumpStatement::op)
        .def_property_readonly(
            "expr", [](nws::JumpStatement& self) { return self.expr.get(); },
            py::return_value_policy::reference_internal);

    py::class_<nws::LabelStatement, nws::Statement>(nw, "LabelStatement")
        .def_readonly("label", &nws::LabelStatement::type)
        .def_property_readonly(
            "expr", [](nws::LabelStatement& self) { return self.expr.get(); },
            py::return_value_policy::reference_internal);

    py::class_<nws::SwitchStatement, nws::Statement>(nw, "SwitchStatement")
        .def_property_readonly(
            "block", [](nws::SwitchStatement& self) { return self.block.get(); },
            py::return_value_policy::reference_internal)
        .def_property_readonly(
            "target", [](nws::SwitchStatement& self) { return self.target.get(); },
            py::return_value_policy::reference_internal);

    py::class_<nws::WhileStatement, nws::Statement>(nw, "WhileStatement")
        .def_property_readonly(
            "block", [](nws::WhileStatement& self) { return self.block.get(); },
            py::return_value_policy::reference_internal)
        .def_property_readonly(
            "test", [](nws::WhileStatement& self) { return self.check.get(); },
            py::return_value_policy::reference_internal);

    py::class_<nws::Declaration, nws::Statement>(nw, "Declaration")
        .def_readonly("type", &nws::Declaration::type);

    py::class_<nws::FunctionDecl, nws::Declaration>(nw, "FunctionDecl")
        .def_readonly("identifier", &nws::FunctionDecl::identifier,
            "Gets the declarations identifier")
        .def("__len__", [](nws::FunctionDecl& self) { return self.params.size(); })
        .def(
            "__getitem__", [](nws::FunctionDecl& self, size_t idx) { return self.params[idx].get(); },
            py::return_value_policy::reference_internal);

    py::class_<nws::FunctionDefinition, nws::Declaration>(nw, "FunctionDefinition")
        .def_property_readonly(
            "decl", [](nws::FunctionDefinition& self) { return self.decl.get(); },
            py::return_value_policy::reference_internal)
        .def_property_readonly(
            "block", [](nws::FunctionDefinition& self) { return self.block.get(); },
            py::return_value_policy::reference_internal);

    py::class_<nws::StructDecl, nws::Declaration>(nw, "StructDecl")
        .def("__len__", [](nws::StructDecl& self) { return self.decls.size(); })
        .def(
            "__getitem__", [](nws::StructDecl& self, size_t idx) { return self.decls[idx].get(); },
            py::return_value_policy::reference_internal);

    py::class_<nws::VarDecl, nws::Declaration>(nw, "VarDecl")
        .def_readonly("identifier", &nws::VarDecl::identifier,
            "Gets the declarations identifier")
        .def_property_readonly(
            "init", [](nws::VarDecl& self) { return self.init.get(); },
            py::return_value_policy::reference_internal);
}
