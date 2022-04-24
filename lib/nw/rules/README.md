# Rules

The `Rules` module presents some difficulties in the sense that if one was to sit down and design a system for rules capable of expressing relatively arbitrary sets of rules and modifiers, it would not look much like NWN(:EE).

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
> header: nw/rules/system.hpp

A `Selector` gets some piece of information from a creature using a type and maybe a constant (loaded from 2das) without needing to worry about the exact layout of the data.

One could do this with lambdas, but it seems unnecessary.  Regardless of what attributes a creature has, what those attributes mean, how they affect the rules of the game, and so on; a creature will always have some set of abilities, some set of skills, some set of armor classes, etc, etc.

Example:

```cpp
auto ability_strength = nw::rules().get_constant("ABILITY_STRENGHT");
// ...
auto s = nw::select::ability(ability_strength);
// ...
auto str = s.select(creature);
if(str && *str > 20) {
    // ...
}
```

