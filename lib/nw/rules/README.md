# Rules

The `Rules` module presents some difficulties in the sense that if one was to sit down and design a system capable of expressing relatively arbitrary sets of rules and modifiers, it probably would not look much like NWN.  Enhanced Edition's approach largely was to unhardcode *values*, but not systems[^1].

### An Historical Example of a Rule Problem

- In NWN1 weapon feats (Weapon Focus, etc) were all hardcoded.  New weapon baseitem types could be added but were not fully functional
- Acaos created a [nwnx2](https://github.com/NWNX/nwnx2-linux) plugin, nwnx_weapons, which allowed programmatically setting weapon feats for abitrary baseitems and some custom feats specific to Higher Ground.
- While the above solution was good, why not [rewrite the whole thing in Lua](https://github.com/jd28/Solstice/blob/develop/src/solstice/rules/weapons.lua) and let people do whatever it is they please.
- The Community Patch Project later took a configuration approach to the problem adding new columns to `baseitems.2da` for each default weapon feat.
- NWN:EE is released and its Techincal Director disregarded all the above.  So, someone implemented an [NWNX:EE](https://github.com/nwnxee/unified) plugin, Weapon, which took a programmatic approach but limited only default weapon feats[^2].
- Years on NWN:EE adopted the Community Patch Project approach.

What does this bit of boring history lead to?  The goals.

### Goals

- Rules must be overrideable, expandable, removable either through configuration (2da) or at the very least programmitically.  Nothing should be hardcoded.
- The rules system must be queryable.  Example: Given one creature attacking one chair in one bar of Chicago, what are all the modifiers that affect this particular situation?

### Warnings
* This is massively incomplete
* This only operational at the most base level, it takes in to account no modifiers.

[^1]: There are some exceptions, parts of the custom spellcaster system.
[^2]: No offense to the author of this plugin, it's hard to understate what a gigantic step backward NWNX:EE was at release.

-----------

## System

The foundation of system is just three types: `int`, `float`, strings.

### **Constant**
> header: nw/rules/system.hpp

A `Constant` is an interned string and an `int`, `float`, or `std::string` value.

It is necessary to check what the `Constant`s type is before accessing its value, i.e.:
```cpp
Constant c = ...;
if(c->is<int32_t>()) {
    int v = c->as<int32_t>();
}
```

### **Selector**
> header: nw/rules/system.hpp<br>
> tests: nw/tests/rules_selector.cpp

A `Selector` gets some piece of information from a creature using a type and maybe a constant (loaded from 2das) without needing to worry about the exact layout of the data.

Example:

```cpp
auto ability_strength = nw::rules().get_constant("ABILITY_STRENGTH");
// ...
auto s = nw::select::ability(ability_strength);
// ...
auto str = s.select(creature);
if(str.is<int32_t>() && str.as<int32_t>() > 20) {
    // ...
}
```

### **Qualifier**
> header: nw/rules/system.hpp<br>
> tests: nw/tests/rules_qualifier.cpp

A `Qualifier` is a `Selector` with some constraints thereupon.  In the example below any creature with an unmodified strength between [20, 40] inclusive would match.

```cpp
auto q = nw::qualifier::ability(ability_strength, 20, 40);
// ...
if(q.match(creature)) {
    // ...
}
```

### **Requirement**
> header: nw/rules/system.hpp<br>
> tests: nw/tests/rules_requirement.cpp

A `Requirement` is just a set of one or more `Qualifier`s.

Example: Some thing a has requirement of level 4, wisdom between [12, 20], and a minimum appraise skill of 6.

```cpp
auto ability_wisdom = nw::rules().get_constant("ABILITY_WISDOM");
auto skill_appraise = nw::rules().get_constant("SKILL_APPRAISE");

auto req = nw::Requirement{{
    nw::qualifier::level(4),
    nw::qualifier::ability(ability_wisdom, 12, 20), // Min, Max
    nw::qualifier::skill(skill_appraise, 6),
}};
// ...
if(req.met(creature)) {
    // ...
}
```

By default a `Requirement` uses logical conjunction, to use disjunction pass `false` at construction.

```cpp
auto req = nw::Requirement{{
    // Qualifiers ...
}, false};
```
