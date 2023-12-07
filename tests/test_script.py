import rollnw

from rollnw.script import *


def test_script_lexer():
    ctx = Context()
    lex = NssLexer("void main() { }", ctx)
    assert lex.next().type == NssTokenType.VOID
    assert lex.next().type == NssTokenType.IDENTIFIER
    assert lex.current().loc.view() == "main"
    assert lex.next().type == NssTokenType.LPAREN
    assert lex.next().type == NssTokenType.RPAREN
    assert lex.next().type == NssTokenType.LBRACE
    assert lex.next().type == NssTokenType.RBRACE


def test_function_decl():
    ctx = Context("nwscript")
    nss = Nss.from_string("void test_function(string s, int b);", ctx, False)
    nss.parse()
    nss.resolve()
    script = nss.ast()
    assert isinstance(script[0], FunctionDecl)
    decl = script[0]
    assert len(decl) == 2
    assert isinstance(decl[0], VarDecl)


def test_function_decl2():
    ctx = LspContext("nwscript")
    nss = Nss.from_string("void test_function(string s, int b)", ctx, False)
    try:
        nss.parse()
    except:
        pass
    nss.resolve()
    assert len(nss.diagnostics()) == 1


def test_var_decl():
    ctx = Context("nwscript")
    nss = Nss.from_string("int TRUE = 1; const int MY_GLOBAL = 1;", ctx, False)
    nss.parse()
    nss.resolve()
    script = nss.ast()
    assert isinstance(script[1], VarDecl)
    decl = script[1]
    assert isinstance(decl.init, LiteralExpression)
    assert decl.complete("TRUE")[0].identifier() == "TRUE"
