#pragma once

#include "AstResolver.hpp"
#include "NullVisitor.hpp"

namespace nw::smalls {

struct NameResolver : NullVisitor {
    explicit NameResolver(AstResolver& ctx);

    /// Declaring a subset of the overloads hides the rest of the base's set.
    using NullVisitor::visit;

    void visit(Ast* script) override;
    void visit(AliasedImportDecl* decl) override;
    void visit(SelectiveImportDecl* decl) override;

    AstResolver& ctx;
};

} // namespace nw::smalls
