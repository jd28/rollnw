rules
=====

The ``Rules`` module presents some difficulties in the sense that if one
was to sit down and design a system capable of expressing relatively
arbitrary sets of rules and modifiers, it probably would not look much
like NWN. Enhanced Edition's approach largely was to unhardcode
*values*, but not systems [1]_.

The approach here is inspired by previous projects and Orth's
`Race <https://github.com/nwnxee/unified/tree/master/Plugins/Race>`__,
`SkillRank <https://github.com/nwnxee/unified/tree/master/Plugins/SkillRanks>`__,
and
`Feat <https://github.com/nwnxee/unified/tree/master/Plugins/Feat>`__.

Goals
-----

-  Rules must be overrideable, expandable, removable either through
   configuration (2da) or at the very least programmitically. Nothing
   should be hardcoded.
-  The rules system must be queryable. Example: Given one creature
   attacking one chair with one handaxe in one bar of Chicago, what are
   all the modifiers that affect this particular situation?

Warnings
~~~~~~~~

-  This is massively incomplete
-  This only operational at the most base level, it takes in to account
   no modifiers.

--------------

System
------

The foundation of system is just four types: ``int``, ``float``, strings
and ``Constant``\ s.

**Selector**
~~~~~~~~~~~~

A ``Selector`` gets some piece of information from a creature using a
type and maybe a constant (loaded from 2das or manually) without needing
to worry about the exact layout of the data.

**Example**:

.. code:: cpp

   auto s = nw::select::ability(ability_strength);
   // ...
   auto str = nw::kernel::rules().select(s, creature);
   if(str.is<int32_t>() && str.as<int32_t>() > 20) {
       // ...
   }

**Qualifier**
~~~~~~~~~~~~~

A ``Qualifier`` is a ``Selector`` with some constraints thereupon. In
the example below any creature with an unmodified strength between [20,
40] inclusive would match.

.. code:: cpp

   auto q = nw::qualifier::ability(ability_strength, 20, 40);
   // ...
   if(q.match(creature)) {
       // ...
   }

**Requirement**
~~~~~~~~~~~~~~~

A ``Requirement`` is just a set of one or more ``Qualifier``\ s.

**Example**:

Some thing a has requirement of level 4, wisdom between [12, 20], and a
minimum appraise skill of 6.

.. code:: cpp

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

By default a ``Requirement`` uses logical conjunction, to use
disjunction pass ``false`` at construction.

.. code:: cpp

   auto req = nw::Requirement{{
       // Qualifiers ...
   }, false};

.. [1]
   There are some exceptions, parts of the custom spellcaster system.
