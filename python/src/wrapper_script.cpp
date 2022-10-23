#include "opaque_types.hpp"

#include <nw/script/Nss.hpp>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl_bind.h>

#include <string>
#include <strstream>
#include <variant>

namespace py = pybind11;
namespace nws = nw::script;

void init_script(py::module& nw, py::module& script)
{
    py::enum_<nws::NssTokenType>(script, "NssTokenType")
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
        .value("CONST", nws::NssTokenType::CONST)               // const
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
        .value("VOID", nws::NssTokenType::VOID)                 // void
        .value("WHILE", nws::NssTokenType::WHILE);              // while

    py::class_<nws::NssToken>(script, "NssToken")
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

    py::class_<nws::NssLexer>(script, "NssLexer")
        .def(py::init<std::string_view>())
        .def("match", &nws::NssLexer::match)
        .def("next", &nws::NssLexer::next)
        .def("current", &nws::NssLexer::current);

    py::class_<nws::Type>(script, "Type")
        .def_readonly("type_qualifier", &nws::Type::type_qualifier)
        .def_readonly("type_specifier", &nws::Type::type_specifier)
        .def_readonly("struct_id", &nws::Type::struct_id); // Only set if type_specifier if of type NssTokenType::STRUCT

    py::class_<nws::Nss>(script, "Nss")
        .def(py::init<std::filesystem::path>())
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
            py::return_value_policy::reference_internal);

    py::class_<nws::Script>(script, "Script")
        .def(
            "__getitem__", [](const nws::Script& self, size_t index) {
                return self.decls[index].get();
            },
            py::return_value_policy::reference_internal)
        .def("__len__", [](const nws::Script& self) { return self.decls.size(); })
        .def_readonly("defines", &nws::Script::defines)
        .def_readonly("includes", &nws::Script::includes)
        .def("accept", &nws::Script::accept);

    py::class_<nws::AstNode>(script, "AstNode")
        .def("accept", &nws::AstNode::accept);

    py::class_<nws::Expression>(script, "Expression");
    py::class_<nws::AssignExpression, nws::Expression>(script, "AssignExpression");
    py::class_<nws::BinaryExpression, nws::Expression>(script, "BinaryExpression");
    py::class_<nws::CallExpression, nws::Expression>(script, "CallExpression");
    py::class_<nws::ConditionalExpression, nws::Expression>(script, "ConditionalExpression");
    py::class_<nws::DotExpression, nws::Expression>(script, "DotExpression");
    py::class_<nws::GroupingExpression, nws::Expression>(script, "GroupingExpression");
    py::class_<nws::LiteralExpression, nws::Expression>(script, "LiteralExpression");
    py::class_<nws::LiteralVectorExpression, nws::Expression>(script, "LiteralVectorExpression");
    py::class_<nws::LogicalExpression, nws::Expression>(script, "LogicalExpression");
    py::class_<nws::PostfixExpression, nws::Expression>(script, "PostfixExpression");
    py::class_<nws::UnaryExpression, nws::Expression>(script, "UnaryExpression");
    py::class_<nws::VariableExpression, nws::Expression>(script, "VariableExpression");

    py::class_<nws::Statement>(script, "Statement");
    py::class_<nws::BlockStatement, nws::Statement>(script, "BlockStatement");
    py::class_<nws::DoStatement, nws::Statement>(script, "DoStatement");
    py::class_<nws::ExprStatement, nws::Statement>(script, "ExprStatement");
    py::class_<nws::IfStatement, nws::Statement>(script, "IfStatement");
    py::class_<nws::ForStatement, nws::Statement>(script, "ForStatement");
    py::class_<nws::JumpStatement, nws::Statement>(script, "JumpStatement");
    py::class_<nws::LabelStatement, nws::Statement>(script, "LabelStatement");
    py::class_<nws::SwitchStatement, nws::Statement>(script, "SwitchStatement");
    py::class_<nws::WhileStatement, nws::Statement>(script, "WhileStatement");

    py::class_<nws::Declaration, nws::Statement>(script, "Declaration")
        .def_readonly("type", &nws::Declaration::type);

    py::class_<nws::FunctionDecl, nws::Declaration>(script, "FunctionDecl")
        .def_readonly("identifier", &nws::FunctionDecl::identifier,
            "Gets the declarations identifier")
        .def("__len__", [](nws::FunctionDecl& self) { return self.params.size(); })
        .def(
            "__getitem__", [](nws::FunctionDecl& self, size_t idx) { return self.params[idx].get(); },
            py::return_value_policy::reference_internal);

    py::class_<nws::FunctionDefinition, nws::Declaration>(script, "FunctionDefinition");
    py::class_<nws::StructDecl, nws::Declaration>(script, "StructDecl");
    py::class_<nws::VarDecl, nws::Declaration>(script, "VarDecl")
        .def_readonly("identifier", &nws::VarDecl::identifier,
            "Gets the declarations identifier");
}
