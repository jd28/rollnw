#include "smalls_fixtures.hpp"

#include <string_view>

namespace {

bool has_diagnostic_containing(const nw::smalls::Script& script, std::string_view needle)
{
    for (const auto& d : script.diagnostics()) {
        if (d.message.find(needle) != nw::String::npos) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_F(SmallsResolver, GenericInferenceTrail)
{
    auto script = make_script(R"(
fn max(a: $T, b: $T): $T {
    if (a > b) { return a; }
    return b;
}

fn main() {
    var x = max(1, 2.0);
}
)");

    script.parse();
    script.resolve();

    EXPECT_GT(script.errors(), 0u);
    EXPECT_TRUE(has_diagnostic_containing(script, "generic inference failed"));
    EXPECT_TRUE(has_diagnostic_containing(script, "inferred"));
    EXPECT_TRUE(has_diagnostic_containing(script, "arg 0"));
    EXPECT_TRUE(has_diagnostic_containing(script, "arg 1"));
}

TEST_F(SmallsResolver, GenericMissingInference)
{
    auto script = make_script(R"(
fn bad(x: int): $T {
    return 0;
}

fn main() {
    var x = bad(1);
}
)");

    script.parse();
    script.resolve();

    EXPECT_GT(script.errors(), 0u);
    EXPECT_TRUE(has_diagnostic_containing(script, "could not infer type"));
    EXPECT_TRUE(has_diagnostic_containing(script, "appears only in return type"));
}

TEST_F(SmallsResolver, ArgMismatchShowsCallContext)
{
    auto script = make_script(R"(
fn add(a: int, b: int): int {
    return a + b;
}

fn main() {
    var x = add(1, 2.0);
}
)");

    script.parse();
    script.resolve();

    EXPECT_GT(script.errors(), 0u);
    EXPECT_TRUE(has_diagnostic_containing(script, "call 'add'"));
    EXPECT_TRUE(has_diagnostic_containing(script, "arg 1"));
    EXPECT_TRUE(has_diagnostic_containing(script, "'b'"));
    EXPECT_TRUE(has_diagnostic_containing(script, "expected"));
    EXPECT_TRUE(has_diagnostic_containing(script, "got"));
}
