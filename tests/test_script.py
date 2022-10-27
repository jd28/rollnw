import rollnw
from rollnw import Nss


def test_script_lexer():
    lex = rollnw.NssLexer("void main() { }")
    assert lex.next().type == rollnw.NssTokenType.VOID
    assert lex.next().type == rollnw.NssTokenType.IDENTIFIER
    assert lex.current().id == "main"
    assert lex.next().type == rollnw.NssTokenType.LPAREN
    assert lex.next().type == rollnw.NssTokenType.RPAREN
    assert lex.next().type == rollnw.NssTokenType.LBRACE
    assert lex.next().type == rollnw.NssTokenType.RBRACE


def test_function_decl():
    nss = Nss.from_string("void test_function(string s, int b);")
    script = nss.parse()
    assert isinstance(script[0], rollnw.FunctionDecl)
    decl = script[0]
    assert len(decl) == 2
    assert isinstance(decl[0], rollnw.VarDecl)


def test_var_decl():
    nss = Nss.from_string("const int MY_GLOBAL = 1;")
    script = nss.parse()
    assert isinstance(script[0], rollnw.VarDecl)
    decl = script[0]
    assert decl.type.type_specifier.id == "int"
    assert decl.type.type_qualifier.id == "const"
    assert isinstance(decl.init, rollnw.LiteralExpression)
