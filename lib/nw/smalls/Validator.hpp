#pragma once

#include "AstResolver.hpp"
#include "NullVisitor.hpp"

namespace nw::smalls {

struct Validator : NullVisitor {
    explicit Validator(AstResolver& ctx);

    /// Declaring a subset of the overloads hides the rest of the base's set.
    using NullVisitor::visit;

    void visit(Ast* script) override;
    void visit(FunctionDefinition* decl) override;
    void visit(BlockStatement* stmt) override;
    void visit(IfStatement* stmt) override;
    void visit(ForStatement* stmt) override;
    void visit(ForEachStatement* stmt) override;
    void visit(SwitchStatement* stmt) override;
    void visit(JumpStatement* stmt) override;
    void visit(LabelStatement* stmt) override;

    AstResolver& ctx;

private:
    int loop_depth_ = 0;
    int switch_depth_ = 0;
    int function_depth_ = 0;

    /// Validates each lambda body in a function as its own function scope.
    /// Validator's own traversal never enters an expression, so without this a
    /// `break` inside a lambda is never checked.
    void validate_lambda_bodies(BlockStatement* block);

    void validate_operator_consistency();
    void validate_map_key_types();
    void validate_map_key_type(TypeID key_type, SourceRange range);
    void validate_sumtype_exhaustiveness(SwitchStatement* stmt);
    void validate_basic_switch_cases(SwitchStatement* stmt);
};

} // namespace nw::smalls
