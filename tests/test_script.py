import rollnw

from rollnw.script import *


def test_script_lexer():
    lex = NssLexer("void main() { }")
    assert lex.next().type == NssTokenType.VOID
    assert lex.next().type == NssTokenType.IDENTIFIER
    assert lex.current().loc.view() == "main"
    assert lex.next().type == NssTokenType.LPAREN
    assert lex.next().type == NssTokenType.RPAREN
    assert lex.next().type == NssTokenType.LBRACE
    assert lex.next().type == NssTokenType.RBRACE


def test_function_decl():
    nss = Nss.from_string("void test_function(string s, int b);")
    script = nss.parse()
    nss.resolve()
    assert isinstance(script[0], FunctionDecl)
    decl = script[0]
    assert len(decl) == 2
    assert isinstance(decl[0], VarDecl)


def test_var_decl():
    nss = Nss.from_string("const int MY_GLOBAL = 1;")
    script = nss.parse()
    nss.resolve()
    assert isinstance(script[0], VarDecl)
    decl = script[0]
    assert isinstance(decl.init, LiteralExpression)
