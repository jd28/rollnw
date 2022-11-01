import enum
from enum import auto

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
    WHILE = auto()         # while


class Nss:
    """Implementation of nwscript"""

    def __init__(self, path: str):
        """Constructs Nss object"""
        pass

    def errors(self):
        """Gets number of errors encountered while parsing"""
        pass

    def parse(self):
        """Parses the script"""
        pass

    def script(self):
        """Gets the parsed script"""
        pass

    def warnings(self):
        """Gets number of errors encountered while parsing"""
        pass

    @staticmethod
    def from_string(string: str):
        """Loads Nss from string"""
        pass


class Script:
    """Class containing a parsed script

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


class NssLexer:
    """A nwscript lexer"""

    def __init__(self, script: str):
        """Constructs lexer from a string"""

    def current(self):
        """Gets next token"""

    def next(self):
        """Gets next token"""


class NssToken:
    """Nss token

    Attributes:
        type
        id
        line
        start
        end
    """
    pass


class Type:
    """A NWScript type

    Attributes:
        type_qualifier (NssToken)
        type_specifier (NssToken)
        struct_id (NssToken)
    """


class AstNode:
    """Base Ast class"""
    pass


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
    """Block statement"""
    pass

    def __len__(self):
        """Gets the number of statements"""
        pass

    def __getitem__(self, idx: int):
        """Gets a statement in the block"""
        pass


class DeclStatement (Statement):
    def __len__(self):
        """Gets the number of declarations"""
        pass

    def __getitem__(self, idx: int):
        """Gets a declaration"""
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
    pass


class FunctionDecl (Declaration):
    """Function declaration

    Attributes:
        identifier
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
        identifier
        init
    """
    pass
