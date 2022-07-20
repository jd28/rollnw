rules
=====

The ``Rules`` module presents some difficulties in the sense that if one
was to sit down and design a system capable of expressing relatively
arbitrary sets of rules and modifiers, it probably would not look much
like NWN. Enhanced Edition's approach largely was to unhardcode
*values*, but not systems [1]_.

The approach here is inspired by `Solstice <https://github.com/jd28/Solstice>`__ and Orth's NWNX:EE plugins
`Race <https://github.com/nwnxee/unified/tree/master/Plugins/Race>`__,
`SkillRank <https://github.com/nwnxee/unified/tree/master/Plugins/SkillRanks>`__,
and `Feat <https://github.com/nwnxee/unified/tree/master/Plugins/Feat>`__.

rollNW has the elements of NWN's rule system builtin, which is itself an approximation of the Dungeons
and Dragons 3rd Edition ruleset.

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

**Profile**
   Profiles are a way of decoupling different rulesets from the rule system itself.

**Type**
   A rule type is an attribute of the rule system, say a skill or an ability or a damage.  The rule system
   defines the type and its invalid case, but leaves valid cases up to the rule profile.  An example,
   armor class:

   .. code:: cpp

      // Definition of an attribute in nw/rules/ArmorClass.hpp
      enum struct ArmorClass : int32_t {
         invalid = -1
      };
      constexpr ArmorClass make_armor_class(int32_t id) { return static_cast<ArmorClass>(id); }

      // Somewhere else in a rule profile that uses the concept of armor class
      constexpr nw::ArmorClass ac_dodge = nw::make_armor_class(0);
      constexpr nw::ArmorClass ac_natural = nw::make_armor_class(1);
      constexpr nw::ArmorClass ac_armor = nw::make_armor_class(2);
      constexpr nw::ArmorClass ac_shield = nw::make_armor_class(3);
      constexpr nw::ArmorClass ac_deflection = nw::make_armor_class(4);

**Flag**
   :cpp:struct:`nw::RuleFlag` provides a mechanism for making flags out of rule types.

-------------------------------------------------------------------------------

Modifiers
---------

The foundation of the modifier system is just three types: ``int32_t``, ``float``, strings.  It builds
on the following abstractions to provide a dynamic, modifiable, queryable system.  Note that Master Feat
modifiers are special cased below.

**Selector**
   A selector gets some piece of information from an entity.

   **Example**:

   .. code:: cpp

      auto s = nwn1::sel::ability(ability_strength);
      // ...
      auto str = nw::kernel::rules().select<int>(s, entity);
      // ...

**Qualifier**
   A qualifier is a selector with some constraints thereupon. In
   the example below any creature with an unmodified strength between [20,
   40] inclusive would match.

   .. code:: cpp

      auto q = nwn1::qual::ability(ability_strength, 20, 40);
      // ...
      if(nw::kernel::rules().match(q, creature)) {
         // ...
      }

**Requirement**
   A requirement is just a set of one or more Qualifiers.

   **Example**:

   Some thing a has requirement of level 4, wisdom between [12, 20], and a
   minimum appraise skill of 6.

   .. code:: cpp

      auto req = nw::Requirement{{
         nwn1::qual::level(4),
         nwn1::qual::ability(ability_wisdom, 12, 20), // Min, Max
         nwn1::qual::skill(skill_appraise, 6),
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

**Modifier**
   A :cpp:struct:`nw::Modifier` is

   -  an ``int``, a ``float``, or a function returning an ``int`` or ``float``. [2]_
   -  an optional interned string tag to facilitate searching, modifying, removing modifiers.
   -  an optional :cpp:struct:`nw::Requirement`
   -  an optional :cpp:struct:`nw::Versus`
   -  an optional source, e.g. whether this modifier from a feat, an ability, a skill, etc.

   Modifiers are stored in a global table in :cpp:struct:`nw::kernel::Rules`.

   **Example - Adding a Modifier**:

   .. code:: cpp

      // This is just an example, one would most likely do all epic toughness modifiers together.
      auto mod2 = nwn1::mod::hitpoints(
         20, // Modifier value, if the below requirement is met
         "dnd-3.0-epic-toughness-01",
         nw::ModifierSource::feat
         { nwn1::qual::feat(nwn1::feat_epic_toughness_1) },
      );

      // Add it to the global modifier table
      nw::kernel::rules().add(mod2);

   **Example - Pale Master Armor Class Bonus**:

   .. code:: cpp

      namespace nwk = nw::kernel;

      auto ent = // ...

      auto pm_ac = [](flecs::entity ent) -> nw::ModifierResult {
         auto stat = ent.get<nw::LevelStats>();
         if (!stat) { return 0; }
         auto pm_level = stat->level_by_class(nwn1::class_type_pale_master);
         return pm_level > 0 ? ((pm_level / 4) + 1) * 2 : 0;
      };

      auto mod2 = mod::armor_class(
         ac_natural,
         pm_ac,
         "dnd-3.0-palemaster-ac",
         nw::ModifierSource::class_);

      nw::kernel::rules().add(mod2);
      // RDD AC bonus ... etc, etc, etc

      // Calculate all bonuses in the Natural AC modifier category
      auto ac_natural_mod = nwk::rules().calculate<int>(ent, nwn1::mod_type_armor_class, nwn1::ac_natural);

      auto pm_ac_nerf = [](flecs::entity ent) -> nw::ModifierResult {
         auto stat = ent.get<nw::LevelStats>();
         if (!stat) { return 0; }
         auto pm_level = stat->level_by_class(nwn1::class_type_pale_master);
         return pm_level > 0 ? ((pm_level / 4) + 1) : 0;
      };

      // Set a nerf
      nwk::rules().replace("dnd-3.0-palemaster-ac", pm_ac_nerf);
      ac_natural_mod = nwk::rules().calculate<int>(ent, nwn1::mod_type_armor_class, nwn1::ac_natural);

      // Nerf wasn't enough, delete the whole thing
      nwk::rules().remove("dnd-3.0-palemaster-ac");

-------------------------------------------------------------------------------

Master Feats
------------

Master feats and associated bonuses are set in the :cpp:struct:`nw::MasterFeatRegistry`.  The master
feat registry associates a particular rule element, say, a skill with a master feat and a feat corresponding
to that skill.

**Example - (Epic) Skill Focus: Discipline**

.. code:: cpp

    auto mfr = nw::kernel::world().get_mut<nw::MasterFeatRegistry>();
    mfr->set_bonus(mfeat_skill_focus, 3);
    mfr->set_bonus(mfeat_skill_focus_epic, 10);

    mfr->add(skill_discipline, mfeat_skill_focus, feat_skill_focus_discipline);
    mfr->add(skill_discipline, mfeat_skill_focus_epic, feat_epic_skill_focus_discipline)

Multiple feats are able to be associated with a rule element and masterfeat.  Imagine in some universe,
there is a class that has access to a generic Weapon Focus: Martial feat which provides Weapon Focus
for all martial weapons.

**Example - Multiple Associated Feats**

.. code:: cpp

    auto mfr = nw::kernel::world().get_mut<nw::MasterFeatRegistry>();
    // Set up bonuses...
    mfr->add(baseitem_longsword, mfeat_weapon_focus, feat_weapon_focus_longsword);
    mfr->add(baseitem_longsword, mfeat_weapon_focus, feat_weapon_focus_martial);

.. [1]
   There are some exceptions, parts of the custom spellcaster system.
.. [2]
   One could imagine in a different context, say NWNX:EE, you could add a callback to
   nwnx_dotnet/lua/etc or a string for use with ``ExecuteScriptChunk``.
