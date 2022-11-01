import rollnw
from rollnw.script import *


def test_script_lexer():
    lex = NssLexer("void main() { }")
    assert lex.next().type == NssTokenType.VOID
    assert lex.next().type == NssTokenType.IDENTIFIER
    assert lex.current().id == "main"
    assert lex.next().type == NssTokenType.LPAREN
    assert lex.next().type == NssTokenType.RPAREN
    assert lex.next().type == NssTokenType.LBRACE
    assert lex.next().type == NssTokenType.RBRACE


def test_function_decl():
    nss = Nss.from_string("void test_function(string s, int b);")
    script = nss.parse()
    assert isinstance(script[0], FunctionDecl)
    decl = script[0]
    assert len(decl) == 2
    assert isinstance(decl[0], VarDecl)


def test_var_decl():
    nss = Nss.from_string("const int MY_GLOBAL = 1;")
    script = nss.parse()
    assert isinstance(script[0], VarDecl)
    decl = script[0]
    assert decl.type.type_specifier.id == "int"
    assert decl.type.type_qualifier.id == "const"
    assert isinstance(decl.init, LiteralExpression)


class ErrorCollector:
    def __init__(self) -> None:
        self.data = []

    def __call__(self, msg, token):
        self.data.append((msg, token))


def test_script_callbacks():
    nss = Nss.from_string("int MY_GLOBAL = 1")
    ec = ErrorCollector()
    nss.set_error_callback(ec)
    try:
        script = nss.parse()
    except:
        pass

    assert len(ec.data) == 1
    assert ec.data[0][0] == "Expected ';'., EOF"
