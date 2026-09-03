rules
=====

This page records the runtime rules ownership boundary. Gameplay policy lives
in the selected package's SmallS modules. C++ retains engine storage, native
catalogs, and the narrow protocols consumed by native code.

The ``Rules`` module presents some difficulties in the sense that if one
was to sit down and design a system capable of expressing relatively
arbitrary sets of rules and modifiers, it probably would not look much
like NWN. Enhanced Edition's approach largely was to unhardcode
*values*, but not systems [1]_.

The Goals
---------

-  Rules must be overridable, expandable, removable either through
   configuration (2da) or at the very least programmatically. Nothing
   should be hardcoded.
-  The rules system must be queryable. Example: Given one creature
   attacking one chair with one handaxe in one bar of Chicago, what are
   all the modifiers that affect this particular situation?
-  Ideally, constants would be disassociated from 2da rows.  Say a UUID <-> integer map, but that's
   both a configuration and serialization problem.

-------------------------------------------------------------------------------

Definitions
-----------

**Attribute**
   A feature inherint to some object. I,e. a creature has set of ability scores.

**Profile**
   The explicitly selected package. Its SmallS modules own ruleset values and
   behavior; there is no C++ profile object.

**Type**
   A rule type is an attribute of the rule system, say a skill or an ability or a damage.  The rule system
   defines the type and its invalid case, but leaves valid cases up to the selected package.  An example,
   armor class:

   .. code:: text

      from core.types import { ArmorClass };

      const ac_dodge = ArmorClass(0);
      const ac_natural = ArmorClass(1);
      const ac_armor = ArmorClass(2);
      const ac_shield = ArmorClass(3);
      const ac_deflection = ArmorClass(4);

   Native storage and traversal accept the underlying integer at their narrow
   protocol boundary. C++ does not publish a parallel set of named NWN values.

**Flag**
   :cpp:struct:`nw::RuleFlag` provides a mechanism for making flags out of rule types.

-------------------------------------------------------------------------------

Rules Implementation
--------------------

Gameplay modifiers and master-feat associations are implemented by the
selected package's SmallS modules. C++ retains native storage and catalog
projections, while the mandatory profile matcher interprets the qualifier
protocol below.

Requirements
------------

**Qualifier**
   A qualifier is a constraint on some attribute. In
   the example below any creature with an unmodified strength between [20,
   40] inclusive would match.

   .. code:: cpp

      auto req = nw::Requirement{{
          nw::qualifier_ability(nw::Ability::make(0), 20),
          nw::qualifier_ability(nw::Ability::make(0), nw::QualifierMatch::lte, 40),
      }};
      // ...
      if(nw::kernel::rules().meets_requirement(req, creature)) {
         // ...
      }

**Requirement**
   A requirement is just a set of one or more Qualifiers.

   **Example**:

   Some thing a has requirement of level 4, wisdom between [12, 20], and a
   minimum appraise skill of 6.

   .. code:: cpp

      auto req = nw::Requirement{{
         nw::qualifier_level(4),
         nw::qualifier_ability(nw::Ability::make(4), 12),
         nw::qualifier_ability(nw::Ability::make(4), nw::QualifierMatch::lte, 20),
         nw::qualifier_skill(nw::Skill::make(0), 6),
      }};
      // ...
      if(nw::kernel::rules().meets_requirement(req, creature)) {
         // ...
      }

   By default a requirement uses logical conjunction, to use disjunction pass ``false`` at construction.

   .. code:: cpp

      auto req = nw::Requirement{{
         // Qualifiers ...
      }, false};

.. [1]
   There are some exceptions, parts of the custom spellcaster system.
