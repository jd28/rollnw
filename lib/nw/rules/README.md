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

