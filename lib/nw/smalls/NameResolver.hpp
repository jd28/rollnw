#pragma once

#include "AstResolver.hpp"
#include "NullVisitor.hpp"

namespace nw::smalls {

struct NameResolver : NullVisitor {
    explicit NameResolver(AstResolver& ctx);

    void visit(Ast* script) override;
    void visit(AliasedImportDecl* decl) override;
    void visit(SelectiveImportDecl* decl) override;

    AstResolver& ctx;
};

} // namespace nw::smalls
