#pragma once

#include <nw/smalls/SourceLocation.hpp>

#include <string>
#include <vector>

namespace nw::smalls {
struct Script;
}

/// Providers computed from a resolved Smalls AST.
///
/// These return plain values rather than JSON so they can be tested without a
/// protocol round trip.  The server owns the mapping onto LSP payloads.
namespace smalls_lsp {

// == Semantic tokens =========================================================

/// Indices into the semantic token legend the server advertises.
/// The legend and this enum must stay in the same order.
enum class TokenType : int {
    namespace_ = 0,
    type = 1,
    struct_ = 2,
    enum_ = 3,
    enum_member = 4,
    type_parameter = 5,
    parameter = 6,
    variable = 7,
    property = 8,
    function = 9,
    string = 10,
    number = 11,
    decorator = 12,
    count
};

/// Bit positions in the semantic token modifier legend.
enum TokenModifier : int {
    modifier_none = 0,
    modifier_declaration = 1 << 0,
    modifier_definition = 1 << 1,
    modifier_readonly = 1 << 2,
    modifier_default_library = 1 << 3,
};

/// Names in legend order. Index i names `TokenType(i)`.
const std::vector<std::string>& semantic_token_type_names();

/// Names in legend bit order. Index i names bit `1 << i`.
const std::vector<std::string>& semantic_token_modifier_names();

struct SemanticToken {
    int line = 0;   ///< Zero-based line
    int start = 0;  ///< Zero-based UTF-8 byte column
    int length = 0; ///< Length in UTF-8 bytes
    int type = 0;
    int modifiers = 0;

    friend bool operator==(const SemanticToken&, const SemanticToken&) = default;
};

/// Collects semantic tokens for a resolved script.
///
/// Results are sorted by position and contain no overlapping ranges, which the
/// protocol requires. Tokens spanning more than one line are dropped.
std::vector<SemanticToken> collect_semantic_tokens(nw::smalls::Script& script);

// == Document symbols ========================================================

/// LSP `SymbolKind` values used by the document symbol provider.
enum class SymbolKind : int {
    module = 2,
    class_ = 5,
    field = 8,
    enum_ = 10,
    function = 12,
    variable = 13,
    constant = 14,
    enum_member = 22,
    struct_ = 23,
};

struct DocumentSymbol {
    std::string name;
    std::string detail;
    SymbolKind kind = SymbolKind::variable;
    /// Whole declaration including its body.
    nw::smalls::SourceRange range;
    /// The declared identifier alone. Always contained in `range`.
    nw::smalls::SourceRange selection_range;
    std::vector<DocumentSymbol> children;
};

/// Collects the hierarchical document outline. Function bodies contribute no
/// symbols; the outline describes the file's declared shape only.
std::vector<DocumentSymbol> collect_document_symbols(nw::smalls::Script& script);

// == Folding ranges ==========================================================

enum class FoldingKind {
    none,
    comment,
    imports,
};

struct FoldingRange {
    int start_line = 0; ///< Zero-based
    int end_line = 0;   ///< Zero-based, inclusive, before the closing token
    FoldingKind kind = FoldingKind::none;

    friend bool operator==(const FoldingRange&, const FoldingRange&) = default;
};

/// Collects folding regions. Single-line regions are omitted.
std::vector<FoldingRange> collect_folding_ranges(nw::smalls::Script& script);

// == Unused imports ==========================================================

/// An import whose alias or symbol is never referenced in the document.
struct UnusedImport {
    /// The identifier to report, so the diagnostic underlines the name.
    nw::smalls::SourceRange name_range;
    /// The whole import declaration, so a fix can delete it.
    nw::smalls::SourceRange declaration_range;
    std::string name;
};

/// Finds imports the document never uses.
///
/// The compiler has no opinion on unused imports, so this is derived here.
/// Returns nothing when the script has errors: unresolved references would
/// otherwise make a used import look unused, and deleting a used import on
/// that basis breaks the file.
std::vector<UnusedImport> collect_unused_imports(nw::smalls::Script& script);

// == Struct initializer completeness =========================================

/// A field a brace initializer leaves out.
struct MissingStructField {
    std::string name;
    /// A literal of the field's type, used as the inserted value.
    std::string default_value;
};

/// A brace initializer that omits declared fields.
struct IncompleteStructLiteral {
    /// The whole literal, used to match against a requested range.
    nw::smalls::SourceRange range;
    /// Just inside the closing brace, where the new fields go.
    nw::smalls::SourcePosition insert_at;
    /// True when the literal already lists a field, so a separator is needed.
    bool has_existing_fields = false;
    std::string type_name;
    std::vector<MissingStructField> missing;
};

/// Finds brace initializers that omit fields their struct declares.
///
/// The compiler reports too many initializers but not too few, so this is
/// derived here. Returns nothing when the script has errors, because an
/// unresolved type makes every field look missing.
std::vector<IncompleteStructLiteral> collect_incomplete_struct_literals(
    nw::smalls::Script& script);

// == Imports =================================================================

/// The zero-based line an `import` for `module_path` should be inserted at, so
/// the import block stays sorted. Returns 0 for a file with no imports.
int import_insert_line(nw::smalls::Script& script, std::string_view module_path);

// == Selection ranges ========================================================

/// Returns enclosing ranges around `position`, innermost first.
///
/// The result is strictly increasing: each range properly contains the one
/// before it, and duplicate spans are collapsed. An empty result means the
/// position is not inside any node.
std::vector<nw::smalls::SourceRange> collect_selection_range_chain(
    nw::smalls::Script& script, nw::smalls::SourcePosition position);

} // namespace smalls_lsp
