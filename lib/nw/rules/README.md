# Rules

The `Rules` module presents some difficulties in the sense that if one was to sit down and design a system for rules capable of expressing relatively arbitrary sets of rules and modifiers, it would not look much like NWN(:EE).

Warnings:
* This is massively incomplete
* This only operational at the most base level, it takes in to account no modifiers.

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
if(str && *str > 20) {
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

Example: Some thing a has requirement of level 4, wisdom between [12, 20], and a minimum appraise skill of
6.
```cpp
auto ability_wisdom = nw::rules().get_constant("ABILITY_WISDOM");
auto skill_appraise = nw::rules().get_constant("SKILL_APPRAISE");

auto req = nw::Requirement{
    nw::qualifier::level(4),
    nw::qualifier::ability(ability_wisdom, 12, 20), // Min, Max
    nw::qualifier::skill(skill_appraise, 6),
};
// ...
if(req.met(creature)) {
    // ...
}
```

