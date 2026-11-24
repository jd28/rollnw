#include <nw/kernel/Kernel.hpp>
#include <nw/objects/Creature.hpp>
#include <nw/objects/ObjectManager.hpp>
#include <nw/profiles/nwn1/scriptbridge.hpp>
#include <nw/rules/Class.hpp>
#include <nw/rules/Spell.hpp>
#include <nw/smalls/runtime.hpp>

#include <gtest/gtest.h>

namespace {
using namespace nw;

// Exercises the ability dispatch contract from smalls, where the context and
// result structs can be inspected directly. Driving it from C++ would only see
// the scalar status the bridge returns.
class Nwn1AbilityPipeline : public ::testing::Test {
};

TEST_F(Nwn1AbilityPipeline, DispatchMatchesDomainAndAbilityAndPassesContext)
{
    auto& runtime = kernel::runtime();
    ASSERT_NE(runtime.load_module_from_source("test.ability_dispatch", R"(
import nwn1.ability as Ability;
import core.object as O;
from nwn1.ability import { AbilityUse, AbilityUseResult };

var calls: int = 0;
var seen_ability: int = 0;
var seen_dc: int = 0;
var seen_caster_level: int = 0;

fn _probe(use: AbilityUse): AbilityUseResult {
    calls = calls + 1;
    seen_ability = use.ability;
    seen_dc = use.save_dc;
    seen_caster_level = use.caster_level;
    var r = Ability.empty_result();
    r.handled = true;
    r.consume_use = true;
    return r;
}

fn _make_use(domain: int, ability: int, dc: int): AbilityUse {
    var affected: array!(object) = {};
    return AbilityUse {
        domain = domain,
        ability = ability,
        source = 9,
        tier = 1,
        modifier = 0,
        caster = O.invalid as Creature,
        target = O.invalid,
        has_point = false,
        point_x = 0.0,
        point_y = 0.0,
        point_z = 0.0,
        affected = affected,
        caster_level = 7,
        effective_level = 1,
        save_dc = dc,
        is_hostile = true,
    };
}

// A handler registered for one ability receives exactly that ability, and the
// context it is handed carries the caller's values rather than looking them up.
fn exact_match_receives_context(): bool {
    Ability.clear_handlers();
    Ability.register_handler("probe", Ability.domain_spell, 42, _probe);
    calls = 0;
    var r = Ability.resolve_use(_make_use(Ability.domain_spell, 42, 17));
    return r.handled && r.consume_use
        && calls == 1 && seen_ability == 42 && seen_dc == 17 && seen_caster_level == 7;
}

// A different ability in the same domain must not reach the handler.
fn other_ability_is_skipped(): bool {
    Ability.clear_handlers();
    Ability.register_handler("probe", Ability.domain_spell, 42, _probe);
    calls = 0;
    var r = Ability.resolve_use(_make_use(Ability.domain_spell, 43, 17));
    return !r.handled && !r.consume_use && calls == 0;
}

// Neither must the same ability id in a different domain: a feat and a spell
// that happen to share an id are unrelated.
fn other_domain_is_skipped(): bool {
    Ability.clear_handlers();
    Ability.register_handler("probe", Ability.domain_spell, 42, _probe);
    calls = 0;
    var r = Ability.resolve_use(_make_use(Ability.domain_feat, 42, 17));
    return !r.handled && !r.consume_use && calls == 0;
}

// -1 is the wildcard: any ability within the registered domain.
fn wildcard_matches_any_ability_in_domain(): bool {
    Ability.clear_handlers();
    Ability.register_handler("probe", Ability.domain_spell, -1, _probe);
    calls = 0;
    var first = Ability.resolve_use(_make_use(Ability.domain_spell, 1, 11));
    var second = Ability.resolve_use(_make_use(Ability.domain_spell, 2, 12));
    var skipped = Ability.resolve_use(_make_use(Ability.domain_feat, 3, 13));
    return first.handled && second.handled && !skipped.handled
        && calls == 2 && seen_ability == 2 && seen_dc == 12;
}

// Every matching handler runs and the results merge, as on_hit does.
fn multiple_handlers_merge(): bool {
    Ability.clear_handlers();
    Ability.register_handler("a", Ability.domain_spell, -1, _probe);
    Ability.register_handler("b", Ability.domain_spell, 42, _probe);
    calls = 0;
    var r = Ability.resolve_use(_make_use(Ability.domain_spell, 42, 5));
    return r.handled && calls == 2;
}

fn registry_counts(): bool {
    Ability.clear_handlers();
    if (Ability.handler_count() != 0 || Ability.has_handlers()) { return false; }
    Ability.register_default_handlers();
    if (Ability.handler_count() != 1 || !Ability.has_handlers()) { return false; }
    Ability.clear_handlers();
    return Ability.handler_count() == 0;
}
)"),
        nullptr);

    for (const auto* check : {"exact_match_receives_context",
             "other_ability_is_skipped",
             "other_domain_is_skipped",
             "wildcard_matches_any_ability_in_domain",
             "multiple_handlers_merge",
             "registry_counts"}) {
        auto result = runtime.execute_script("test.ability_dispatch", check);
        ASSERT_TRUE(result.ok()) << check << ": " << result.error_message;
        EXPECT_TRUE(result.value.data.bval) << check;
    }
}

// The bridge-facing entry point is callable with the argument shape the action
// layer will use, and gates on slot availability before any handler runs: a
// freshly made creature can pay for nothing.
TEST_F(Nwn1AbilityPipeline, BridgeEntryPointGatesOnAvailability)
{
    constexpr int32_t use_status_unavailable = -1;

    auto* creature = kernel::objects().make<Creature>();
    ASSERT_NE(creature, nullptr);

    const auto wizard = Class::make(10);         // CLASS_TYPE_WIZARD
    const auto magic_missile = Spell::make(97);  // SPELL_MAGIC_MISSILE

    Vector<smalls::Value> args;
    args.push_back(nwn1::bridge::make_object_arg(creature->handle()));
    args.push_back(smalls::Value::make_int(*wizard));
    args.push_back(smalls::Value::make_int(*magic_missile));
    args.push_back(smalls::Value::make_int(-1));
    args.push_back(nwn1::bridge::make_object_arg(ObjectHandle{}));
    args.push_back(smalls::Value::make_bool(true));

    const auto status = nwn1::bridge::call_nwn1_module_int("nwn1.ability", "resolve_spell_use", args);
    ASSERT_TRUE(status.has_value());
    EXPECT_EQ(*status, use_status_unavailable);

    kernel::objects().destroy(creature->handle());
}

} // namespace
