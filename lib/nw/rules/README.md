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
if(c.is_int()) {
    int v = c.as_int();
}
```

### **Selector**
> header: nw/rules/system.hpp<br>
> tests: nw/tests/rules_selector.cpp

A `Selector` gets some piece of information from a creature using a type and maybe a constant (loaded from 2das) without needing to worry about the exact layout of the data.

One could do this with lambdas, but it seems unnecessary.  Regardless of what attributes a creature has, what those attributes mean, how they affect the rules of the game, and so on; a creature will always have some set of abilities, some set of skills, some set of armor classes, etc, etc.

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

