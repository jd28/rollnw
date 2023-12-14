import enum
from enum import auto
from typing import List, Sequence, Optional

# Script ######################################################################
###############################################################################


class NssTokenType(enum.IntEnum):
    INVALID = auto()
    END = auto()
    # Identifier
    IDENTIFIER = auto()
    # Punctuation
    LPAREN = auto()     # (
    RPAREN = auto()     # )
    LBRACE = auto()     # {
    RBRACE = auto()     # }
    LBRACKET = auto()   # [
    RBRACKET = auto()   # ]
    COMMA = auto()      # ,
    COLON = auto()      # :
    QUESTION = auto()   # ?
    SEMICOLON = auto()  # ;
    POUND = auto()      # #
    DOT = auto()        # .
    # Operators
    AND = auto()        # '&'
    ANDAND = auto()     # &&
    ANDEQ = auto()      # &=
    DIV = auto()        # /
    DIVEQ = auto()      # /=
    EQ = auto()         # =
    EQEQ = auto()       # ==
    GT = auto()         # >
    GTEQ = auto()       # >=
    LT = auto()         # <
    LTEQ = auto()       # <=
    MINUS = auto()      # -
    MINUSEQ = auto()    # -=
    MINUSMINUS = auto()  # --
    MOD = auto()        # %
    MODEQ = auto()      # %=
    TIMES = auto()      # *
    TIMESEQ = auto()    # *=
    NOT = auto()        # !
    NOTEQ = auto()      # !=
    OR = auto()         # '|'
    OREQ = auto()       # |=
    OROR = auto()       # ||
    PLUS = auto()       # +
    PLUSEQ = auto()     # +=
    PLUSPLUS = auto()   # ++
    SL = auto()         # <<
    SLEQ = auto()       # <<=
    SR = auto()         # >>
    SREQ = auto()       # >>=
    TILDE = auto()      # ~
    USR = auto()        # >>>
    USREQ = auto()      # >>>=
    XOR = auto()        # ^
    XOREQ = auto()      # ^=
    # Constants
    FLOAT_CONST = auto()
    INTEGER_CONST = auto()
    OBJECT_INVALID_CONST = auto()
    OBJECT_SELF_CONST = auto()
    STRING_CONST = auto()
    # Keywords
    ACTION = auto()         # action
    BREAK = auto()          # break
    CASE = auto()           # case
    CASSOWARY = auto()      # cassowary
    CONST = auto()          # const
    CONTINUE = auto()       # continue
    DEFAULT = auto()        # default
    DO = auto()             # do
    EFFECT = auto()         # effect
    ELSE = auto()           # else
    EVENT = auto()          # event
    FLOAT = auto()          # float
    FOR = auto()            # for
    IF = auto()             # if
    INT = auto()            # int
    ITEMPROPERTY = auto()   # itemproperty
    JSON = auto()           # json
    LOCATION = auto()       # location
    OBJECT = auto()         # object
    RETURN = auto()         # return
    STRING = auto()         # string
    STRUCT = auto()         # struct
    SQLQUERY = auto()       # sqlquery
    SWITCH = auto()         # switch
    TALENT = auto()         # talent
    VECTOR = auto()         # vector
    VOID = auto()           # void
    WHILE = auto()          # while

    JSON_CONST = auto()
    LOCATION_INVALID = auto()


class Context:
    """Provides a context for parsing a NWScript file"""

    def __init__(self, command_script: str = "nwscript"):
        pass

    def add_include_path(self, path: str):
        """Adds path to internal resman"""
        pass


class DiagnosticType(enum.IntEnum):
    lexical = auto()
    parse = auto()
    semantic = auto()


class DiagnosticLevel(enum.IntEnum):
    warning = auto()
    error = auto()


class Diagnostic:
    """Parsed script Diagnostic

    Attributes:
        type (DiagnosticType)
        level (DiagnosticLevel)
        script (str)
        message (str)
        location (SourceRange): Source range in script
    """


class LspContext:
    """Provides a context built around providing diagnostics to a language server"""

    def __init__(self, command_script: str = "nwscript"):
        pass


class Include:
    """
    Attributes:
        resref (str): Resref of included script
        location (SourceRange): Source range in script
        script (Nss): Loaded script
        used (int): Number of times include is used in script file
    """
    pass


class Comment:
    """Abstracts Comment
    """

    def __str__(self) -> str:
        return ""


class SignatureHelp:
    """
    Attributes
        decl ()
        expr (CallExpression)
        active_param (int)
    """


class Ast:
    """Class containing a parsed ast

    Attributes:
        defines ([str])
        includes ([str])
    """

    def __getitem__(self, index):
        """Gets a toplevel declaration"""
        pass

    def __iter__(self):
        """Gets an iterator of toplevel declarations"""
        pass

    def __len__(self):
        """Gets number of toplevel declarations"""
        pass

    def comments(self) -> Sequence[Comment]:
        """Gets all comments in Ast"""
        return []

    def find_comment(self, line) -> str:
        """Finds first comment that the source range of which ends on ``line`` or ``line`` - 1
        """
        return ""


class Nss:
    """Implementation of nwscript"""

    def __init__(self, path: str):
        """Constructs Nss object"""
        pass

    def ast(self) -> Ast:
        """Gets the parsed script"""
        pass

    def complete(self, needle: str) -> Sequence["Symbol"]:
        """Generates a list of potential completions (excluding dependencies)
        """
        return []

    def complete_at(self, needle: str, line: int, character: int) -> Sequence["Symbol"]:
        """Get all completions (including dependencies)
        """
        return []

    def complete_dot(self, needle: str, line: int, character: int) -> Sequence["Symbol"]:
        """Get all completions for struct fields
        """
        return []

    def diagnostics(self) -> List[Diagnostic]:
        return []

    def errors(self):
        """Gets number of errors encountered while parsing"""
        pass

    def exports(self) -> List["Symbol"]:
        """Gets all of the scripts exports, i.e. top level declarations"""
        return []

    def locate_export(self, is_type: bool,
                      search_dependencies: bool) -> "Symbol":
        """Locate export, i.e. a top level symbols"""
        return Symbol()

    def locate_symbol(self, symbol: str, line: int, character: int) -> "Symbol":
        """Locate symbol in source file"""
        return Symbol()

    def parse(self):
        """Parses the script"""
        pass

    def process_includes(self):
        """Process includes and dependencies"""
        pass

    def resolve(self):
        """Resolves and type-checks Ast"""
        pass

    def signature_help(self, line: int, character: int) -> SignatureHelp:
        """Gets signature help for a call expression that contains the provided position"""
        return SignatureHelp()

    def view_from_range(self, range: "SourceRange") -> str:
        """Gets number of errors encountered while parsing"""
        return ""

    def warnings(self):
        """Gets number of errors encountered while parsing"""
        pass

    @staticmethod
    def from_string(string: str):
        """Loads Nss from string"""
        pass


class NssLexer:
    """A nwscript lexer"""

    def __init__(self, script: str):
        """Constructs lexer from a string"""

    def current(self):
        """Gets next token"""

    def next(self):
        """Gets next token"""


class SourcePosition:
    """Position in source code

    Attributes:
        column (int): Starting column
        line (int): Starting line
    """


class SourceRange:
    """Range into the source code

    Attributes:
        start (SourcePosition): Start
        end (SourcePosition): End
    """
    pass


class SourceLocation:
    """Nss source location

    Attributes:
        range (SourceRange): Range in source code
    """

    def length(self) -> int:
        """Length of the source location"""
        return 0

    def view(self) -> str:
        """String view of the location
        """
        return ""


class SymbolKind(enum.IntEnum):
    """Enum of different symbol kinds
    """
    variable = auto()
    function = auto()
    type = auto()
    param = auto()
    field = auto()


class Symbol:
    """Info regarding a particular symbol somewhere in a source file

    Attributes:
        node (AstNode)
        decl (Declaration)
        comment (str)
        type (str)
        kind (SymbolKind)
        provider (str)
    """
    pass


class InlayHint:
    """An inlay source code hint

    Attributes:
        message (str)
        position (SourcePosition)
    """
    pass


class NssToken:
    """Nss token

    Attributes:
        type (NssTokenType)
        loc (SourceLocation)
    """
    pass


class AstNode:
    """Base Ast Node class"""

    def complete(self, needle: str) -> Sequence["Symbol"]:
        """Find completions for any Ast Node

        @note This function does not traverse dependencies
        """
        return []


class Expression(AstNode):
    """Base Expression AST node"""
    pass


class AssignExpression(Expression):
    """Assignment operation expression

    Attributes:
        lhs
        operator
        rhs
    """
    pass


class BinaryExpression(Expression):
    """Binary operation expression

    Attributes:
        lhs
        operator
        rhs
    """
    pass


class CallExpression(Expression):
    """Call operation expression

    Attributes:
        expr
    """

    def __len__(self):
        """Gets the number of parameters"""
        pass

    def __getitem__(self, idx: int):
        """Gets a parameter"""
        pass


class ConditionalExpression(Expression):
    """Conditional operation expression

    Attributes:
        test
        true_branch
        false_branch
    """
    pass


class DotExpression(Expression):
    """Dot operation expression

    Attributes:
        expr
    """
    pass


class EmptyExpression(Expression):
    """Empty expression only used in case of expression parsing erros
    """
    pass


class GroupingExpression(Expression):
    """Grouping operation expression

    Attributes:
        expr
    """
    pass


class LiteralExpression(Expression):
    """Literal expression

    Attributes:
        literal
    """
    pass


class LiteralVectorExpression(Expression):
    """Literal vector expression

    Attributes:
        x
        y
        z
    """

    pass


class LogicalExpression(Expression):
    """Binary operation expression

    Attributes:
        lhs
        operator
        rhs
    """
    pass


class PostfixExpression(Expression):
    """Postfix operation expression

    Attributes:
        lhs
        operator
    """
    pass


class UnaryExpression(Expression):
    """Unary operation expression

    Attributes:
        rhs
        operator
    """
    pass


class VariableExpression(Expression):
    """Variable expression

    Attributes:
        var
    """
    pass


class Statement(AstNode):
    """Base statement class"""
    pass


class BlockStatement (Statement):
    """Block statement

    Attributes:
        range (SourceRange): Range in source code
    """
    pass

    def __len__(self):
        """Gets the number of statements"""
        pass

    def __getitem__(self, idx: int):
        """Gets a statement in the block"""
        pass


class DoStatement (Statement):
    """If statement

    Attributes:
        block (BlockStatement)
        test (Expression)
    """
    pass


class EmptyStatement (Statement):
    """Empty statement"""
    pass


class ExprStatement (Statement):
    """Expression statement

    Attributes:
        expr (Expression)
    """
    pass


class IfStatement (Statement):
    """If statement

    Attributes:
        test
        true_branch
        false_branch
    """
    pass


class ForStatement (Statement):
    """For statement

    Attributes:
        init
        test
        increment
        block (BlockStatement)
    """
    pass


class JumpStatement (Statement):
    """Jump statement

    Attributes:
        operator (NssToken)
        expr (Expression)
    """
    pass


class LabelStatement (Statement):
    """Label statement

    Attributes:
        label (NssToken)
        expr (Expression)
    """
    pass


class SwitchStatement (Statement):
    """Switch statement

    Attributes:
        target (Expression)
        block (BlockStatement)
    """
    pass


class WhileStatement (Statement):
    """While statement

    Attributes:
        test (Expression)
        block (BlockStatement)
    """
    pass


class Declaration(Statement):
    """Base Declaration class

    Attributes:
        type
    """

    def identifier(self) -> str:
        """Get declaration identifier"""
        return ""


class FunctionDecl (Declaration):
    """Function declaration
    """

    def __len__(self):
        """Gets the number of parameters"""
        pass

    def __getitem__(self, idx: int):
        """Gets a parameter"""
        pass


class FunctionDefinition (Declaration):
    """Function definition

    Attributes:
        decl (FunctionDecl)
        block (BlockStatement)
    """
    pass


class StructDecl (Declaration):
    """Struct declaration
    """

    def __len__(self):
        """Gets the number of struct members"""
        pass

    def __getitem__(self, idx: int):
        """Gets a struct member declaration"""
        pass


class VarDecl (Declaration):
    """Variable declaration

    Attributes:
        init
    """
    pass


class DeclList (Declaration):
    def __len__(self):
        """Gets the number of declarations"""
        pass

    def __getitem__(self, idx: int):
        """Gets a declaration"""
        pass
