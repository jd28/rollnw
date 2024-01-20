from . import Location
import enum
from enum import auto
from typing import List, Sequence, Optional, Iterator

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

    def __init__(self, include_paths: List[str] = [],
                 command_script: str = "nwscript"):
        pass

    def add_include_path(self, path: str):
        """Adds path to internal resman"""
        pass


class DiagnosticType(enum.IntEnum):
    lexical = auto()
    parse = auto()
    semantic = auto()


class DiagnosticSeverity(enum.IntEnum):
    error = auto()
    hint = auto()
    information = auto()
    warning = auto()


class Diagnostic:
    """Information for a script diagnostic
    """
    #: The type of the diagnostic
    type: DiagnosticType

    #: The severity of the diagnostic
    severity: DiagnosticSeverity

    #: Name of script
    script: str

    #: A helpful message
    message: str

    #: Source range in script
    location: "SourceRange"


class Include:
    """Abstracts a script include
    """
    #: Resref of included script
    resref: str

    #: Source range in script
    location: "SourceRange"

    #: Loaded script
    script: "Nss"

    #: Number of times include is used in script file
    used: int


class Comment:
    """Abstracts Comment
    """

    def __str__(self) -> str:
        return ""


class SignatureHelp:
    """Data required for providing Signature Help in an LSP
    """
    #: The declaration for ``expr``
    decl: "Declaration"

    #: The current call expression
    expr: "CallExpression"

    #: The currently active parameter, i.e. where the cursor is in the parameter
    active_param: int


class Ast:
    """Class containing a parsed ast
    """
    #: Defines from ``#define`` directive.  Only used in command script, i.e. nwscript.nss
    defines: dict[str, str]

    #: Scripts that are included in the current script
    includes: List[Include]

    def __getitem__(self, index: int) -> "Declaration":
        """Gets a toplevel declaration"""
        pass

    def __iter__(self) -> "Iterator[Declaration]":
        """Gets an iterator of toplevel declarations"""
        pass

    def __len__(self) -> int:
        """Gets number of toplevel declarations"""
        pass

    def comments(self) -> List[Comment]:
        """Gets all comments in Ast"""
        return []

    def find_comment(self, line) -> str:
        """Finds first comment that the source range of which ends on ``line`` or ``line`` - 1
        """
        return ""


class Nss:
    """Implementation of nwscript"""

    def __init__(self, path: str, ctx: Context, is_command_script: bool = False):
        """Constructs Nss object"""
        pass

    def ast(self) -> Ast:
        """Gets the parsed script"""
        pass

    def complete(self, needle: str) -> List["Symbol"]:
        """Generates a list of potential completions (excluding dependencies)
        """
        return []

    def complete_at(self, needle: str, line: int, character: int) -> List["Symbol"]:
        """Get all completions (including dependencies)
        """
        return []

    def complete_dot(self, needle: str, line: int, character: int) -> List["Symbol"]:
        """Get all completions for struct fields
        """
        return []

    def diagnostics(self) -> List[Diagnostic]:
        return []

    def errors(self) -> int:
        """Gets number of errors encountered while parsing"""
        pass

    def exports(self) -> List["Symbol"]:
        """Gets all of the scripts exports, i.e. top level declarations"""
        return []

    def locate_export(self, is_type: bool,
                      search_dependencies: bool = False) -> "Symbol":
        """Locate export, i.e. a top level symbols"""
        return Symbol()

    def locate_symbol(self, symbol: str, line: int, character: int) -> "Symbol":
        """Locate symbol in source file"""
        return Symbol()

    def name(self) -> str:
        """Gets script's name"""
        return ""

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
        """Gets string view of the source at ``range``"""
        return ""

    def warnings(self) -> int:
        """Gets number of errors encountered while parsing"""
        pass

    @staticmethod
    def from_string(string: str, ctx: Context, is_command_script: bool = False) -> "Nss":
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
    """
    #: Starting column
    column: int

    #: Starting line
    line: int


class SourceRange:
    """Range into the source code
    """
    #: Start
    start: SourcePosition

    #: End
    end: SourcePosition


class SourceLocation:
    """Nss source location
    """
    #: Range in source code
    range: SourceRange

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
    """
    #: The ast node where the symbol was found, if availble
    node: "Optional[AstNode]"

    #: The declaration of the symbol
    decl: "Declaration"

    #: Comment associated with the line the symbol is on or the line prior
    comment: str

    #: The symbols type as a string
    type: str

    #: The symbols kind, for use with an LSP
    kind: SymbolKind

    #: The script in which the symbol was found
    provider: Nss

    #: A string view of the symbol in source
    view: str


class InlayHint:
    """An inlay source code hint for an LSP
    """
    #: Helpful message to display inline or a type, etc.
    message: str

    #: The postion where the hint should be displayed
    position: SourcePosition


class NssToken:
    """Nss token
    """
    #: The type of the token
    type: NssTokenType

    #: The location of the token in a source file
    loc: SourceLocation


class AstNode:
    """Base Ast Node class"""

    def complete(self, needle: str) -> List["Symbol"]:
        """Find completions for any Ast Node

        @note This function does not traverse dependencies
        """
        return []


class Expression(AstNode):
    """Base Expression AST node"""
    pass


class AssignExpression(Expression):
    """Assignment operation expression
    """
    #: Expression being assigned to.  Note that in a simple language like NWScript
    #: this can only be a variable expression or a dot expression (i.e. assigning a struct member)
    lhs: "VariableExpression | DotExpression"

    #: The assignment operator, '=', '+=', etc, etc.
    operator: NssToken

    #: The expression being assigned
    rhs: Expression


class BinaryExpression(Expression):
    """Binary operation expression
    """
    #: Lefthand side of the binary expression
    lhs: Expression

    #: The binary operator, '+', '-', etc, etc.
    operator: NssToken

    #: Righthand side of the binary expression
    rhs: Expression


class CallExpression(Expression):
    """Call operation expression
    """
    #: The expression prior to ``(...)``
    expr: Expression

    def __len__(self) -> int:
        """Gets the number of arguments"""
        pass

    def __getitem__(self, idx: int) -> Expression:
        """Gets an argument"""
        pass

    def __iter__(self) -> Iterator[Expression]:
        """Gets iterator of arguments"""
        pass


class ComparisonExpression(Expression):
    """Comparison operation expression
    """
    #: Lefthand side of the Comparison expression
    lhs: Expression

    #: The Comparison operator, '==', '<', etc, etc.
    operator: NssToken

    #: Righthand side of the Comparison expression
    rhs: Expression


class ConditionalExpression(Expression):
    """Conditional operation expression
    """

    #: The expression that is tested
    test: Expression

    #: The branch where ``test`` is True
    true_branch: "Statement"

    #: The branch where ``test`` is False
    false_branch: "Statement"


class DotExpression(Expression):
    """Dot operation expression
    """
    #: In NWScript the only two possible expressions on the left hand of the dot
    #: are var_expr.var_expr or call_expr.var_expr
    lhs: "VariableExpression | CallExpression"

    #: The right hand side of a dot operator
    rhs: "VariableExpression"


class EmptyExpression(Expression):
    """Empty expression only used in case of expression parsing erros
    """
    pass


class GroupingExpression(Expression):
    """Grouping operation expression
    """
    #: Expression contained in the grouping parenthesis.
    expr: Expression


class LiteralExpression(Expression):
    """Literal expression
    """
    #: Data of the literal value
    data: int | str | float | Location

    #: Token of the literal value
    literal: NssToken


class LiteralVectorExpression(Expression):
    """Literal vector expression
    """
    #: Token representation for ``x`` value
    x: NssToken

    #: Token representation for ``y`` value
    y: NssToken

    #: Token representation for ``z`` value
    z: NssToken


class LogicalExpression(Expression):
    """Logical operation expression
    """
    #: Lefthand side of the logical expression
    lhs: Expression

    #: The logical operator, '||', '&&', etc, etc.
    operator: NssToken

    #: Righthand side of the logical expression
    rhs: Expression


class PostfixExpression(Expression):
    """Postfix operation expression
    """
    #: Lefthand side of the postfix expression
    lhs: Expression

    #: The postix operator, '++', '--', etc.
    operator: NssToken


class UnaryExpression(Expression):
    """Unary operation expression
    """
    #: The postix operator, '++', '--', etc.
    operator: NssToken

    #: Righthand side of the postfix expression
    rhs: Expression


class VariableExpression(Expression):
    """Variable expression
    """
    #: Token containing variable identifier
    var: NssToken


class Statement(AstNode):
    """Base statement class"""
    pass


class BlockStatement (Statement):
    """Block statement

    Attributes:
        range (SourceRange): Range in source code
    """
    pass

    def __len__(self) -> int:
        """Gets the number of statements"""
        pass

    def __getitem__(self, idx: int) -> Statement:
        """Gets a statement in the block"""
        pass

    def __iter__(self) -> Iterator[Statement]:
        """Gets iterator of statements"""
        pass


class DoStatement (Statement):
    """Do statement
    """
    #: The do block statement
    block: BlockStatement

    #: The test at the end of the block
    test: Expression


class EmptyStatement (Statement):
    """Empty statement"""
    pass


class ExprStatement (Statement):
    """Expression statement
    """
    #: An expression
    expr: Expression


class IfStatement (Statement):
    """If statement
    """
    #: The expression that is tested
    test: Expression

    #: The branch where ``test`` is True
    true_branch: "Statement"

    #: The optional branch where ``test`` is False
    false_branch: "Statement"


class ForStatement (Statement):
    """For statement
    """
    #: An optional initialization.  Normally this is a Declaration or just an expression
    init: Optional[AstNode]

    #: An optional expression that determines if the loop is to continue
    test: Optional[Expression]

    #: An optional increment expression
    increment: Optional[Expression]

    #: While this is called ``block``, any (single) statement can follow a for loop.
    block: Statement


class JumpStatement (Statement):
    """Jump statement
    """
    #: Token representing the jump statement (i.e. ``return``, ``break``, ``continue``)
    operator: NssToken

    #: Optional expression when returning a value
    expr: Optional[Expression]


class LabelStatement (Statement):
    """Label statement
    """
    #: Token representing the label statement (i.e. ``case``, ``default``)
    label: NssToken

    #: Expression when label is a ``case``.
    expr: Optional[Expression]


class SwitchStatement (Statement):
    """Switch statement
    """
    #: The target expression for the switch
    target: Expression

    #: The block of labels and stuff
    block: BlockStatement


class WhileStatement (Statement):
    """While statement
    """
    #: The expression that determines if the loop is to continue
    test: Expression

    #: While this is called ``block``, any (single) statement can follow a for loop.
    block: Statement


class Declaration(Statement):
    """Base Declaration class
        type
    """

    def identifier(self) -> str:
        """Get declaration identifier"""
        return ""


class FunctionDecl (Declaration):
    """Function declaration
    """

    def __len__(self) -> int:
        """Gets the number of parameters"""
        pass

    def __getitem__(self, idx: int) -> Declaration:
        """Gets a parameter"""
        pass

    def __iter__(self) -> Iterator[Declaration]:
        """Gets iterator of parameters"""
        pass


class FunctionDefinition (Declaration):
    """Function definition
    """
    #: Declaration of the function definition
    decl: FunctionDecl

    #: Block of the function
    block: BlockStatement


class StructDecl (Declaration):
    """Struct declaration
    """

    def __len__(self) -> int:
        """Gets the number of struct members"""
        pass

    def __getitem__(self, idx: int) -> Declaration:
        """Gets a struct member declaration"""
        pass

    def __iter__(self) -> Iterator[Declaration]:
        """Gets iterator of statements"""
        pass


class VarDecl (Declaration):
    """Variable declaration
    """
    #: An optional expression to initialize declaration
    init: Optional[Expression]


class DeclList (Declaration):
    def __len__(self) -> int:
        """Gets the number of declarations"""
        pass

    def __getitem__(self, idx: int) -> Declaration:
        """Gets a declaration"""
        pass

    def __iter__(self) -> Iterator[Declaration]:
        """Gets iterator of statements"""
        pass


def load(script: str, ctx: Context,
         is_command_script: bool = False) -> Nss:
    """Fully loads a script from resman"""
    pass
