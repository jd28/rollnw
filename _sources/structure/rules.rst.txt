rules
=====

The ``Rules`` module presents some difficulties in the sense that if one
was to sit down and design a system capable of expressing relatively
arbitrary sets of rules and modifiers, it probably would not look much
like NWN. Enhanced Edition's approach largely was to unhardcode
*values*, but not systems [1]_.

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

      // Definition of an attribute in nw/rules/attributes.hpp
      DECLARE_RULE_TYPE(ArmorClass)

      // Somewhere else in a rule profile that uses the concept of armor class
      constexpr nw::ArmorClass ac_dodge = nw::ArmorClass::make(0);
      constexpr nw::ArmorClass ac_natural = nw::ArmorClass::make(1);
      constexpr nw::ArmorClass ac_armor = nw::ArmorClass::make(2);
      constexpr nw::ArmorClass ac_shield = nw::ArmorClass::make(3);
      constexpr nw::ArmorClass ac_deflection = nw::ArmorClass::make(4);

      // Then it's possible to refer to them as some opaque value for type safety:
      auto res = get_armor_class(object, ac_shield);

      // Or as their underlying value:
      switch(*ac_type) {
      case *ac_dodge: // ..
      case *ac_natural: // ..
      case *ac_armor: // ..
      case *ac_shield: // ..
      case *ac_deflection: // ..
      }

      // Or if it makes logical sense to think of a particular type as an index:
      obj->ac_bonuses[ac_dodge.idx()]

**Flag**
   :cpp:struct:`nw::RuleFlag` provides a mechanism for making flags out of rule types.

-------------------------------------------------------------------------------

Modifiers
---------

The foundation of the modifier system is just three types: ``int32_t``, ``float``, strings.  It builds
on the following abstractions to provide a dynamic, modifiable, queryable system.  Modifiers are stored
in a global table in :cpp:struct:`nw::kernel::Rules`. Note that Master Feat modifiers are special cased
below.

The approach here is inspired by `Solstice <https://github.com/jd28/Solstice>`__ and Orth's NWNX:EE plugins
`Race <https://github.com/nwnxee/unified/tree/master/Plugins/Race>`__,
`SkillRank <https://github.com/nwnxee/unified/tree/master/Plugins/SkillRanks>`__,
and `Feat <https://github.com/nwnxee/unified/tree/master/Plugins/Feat>`__.

Note that the examples below are designed for simplicity, not things that should necessarily be done.

Definitions
~~~~~~~~~~~

**Modifier Type**
   A modifier type is a rule type that is used to determine how to process the outputs of a modifier.

**Modifier Source**
   A modifier source indicates the attribute of an object that modifier is associated with.

**Modifier Input**
   An input is an ``int``, ``float``, or a version of a :cpp:struct:`ModifierFunction` [2]_.

**Modifier Output**
   In the basic cases, an output is the input passed directly without modification.  When a function
   is the modifier input, it is called and its result is the output

   The output is then passed to a callback provided to one of the ``nw::kernel::resolve_modifier``
   function overloads.

   The meaning of these outputs are determined by the modifier type.  The number of output parameters is limited
   to one.  They currently have to be integer, floating point types, or :cpp:struct:`nw::DamageRoll`.

   In most cases using ``nw::kernel::sum_modifier`` or ``nw::kernel::max_modifier`` can avoid having to deal
   with passing callbacks.

**Example - Adding a Modifier**:

.. code:: cpp

   // This is just an example, see "profiles/nwn1/modifiers.[ch]pp for real implementations of rules.
   auto mod2 = nwn1::mod::hitpoints(
      20, // Modifier value, if the below requirement is met
      "dnd-3.0-epic-toughness-01",
      nw::ModifierSource::feat
      { nwn1::qual::feat(nwn1::feat_epic_toughness_1) },
   );

   // Add it to the global modifier table
   nw::kernel::rules().modifier.add(mod2);

**Example - Pale Master Armor Class Bonus**:

.. code:: cpp

   auto mod = nwk::load_module("test_data/user/modules/DockerDemo.mod");
   auto ent = nwk::objects().load<nw::Creature>(fs::path("some/palemaster.utc"));

   int res = 0;
   nwk::resolve_modifier(ent, nwn1::mod_type_armor_class, nwn1::ac_natural,
      [&res](int value) { res += value; });
   // res == 6

   auto pm_ac_nerf = [](const nw::ObjectBase* obj) -> nw::ModifierResult {
      auto cre = obj->as_creature();
      if (!cre) { return 0; }
      auto pm_level = cre->levels.level_by_class(nwn1::class_type_pale_master);
      return ((pm_level / 4) + 1);
   };

   // Get rid of any requirement
   nwk::rules().modifiers.replace("dnd-3.0-palemaster-ac", nw::Requirement{});
   // Set nerf
   nwk::rules().modifiers.replace("dnd-3.0-palemaster-ac", pm_ac_nerf);
   res = 0;
   REQUIRE(nwk::resolve_modifier(ent, nwn1::mod_type_armor_class, nwn1::ac_natural,
      [&res](int value) { res += value; }));
   // res == 3

   res = 0;
   nwk::resolve_modifier(ent, nwn1::mod_type_armor_class, nwn1::ac_natural,
      [&res](int value) { res += value; });
   // res == 0

-------------------------------------------------------------------------------

Master Feats
------------

Master feats and associated bonuses are set in the :cpp:struct:`nw::MasterFeatRegistry`.  The master
feat registry associates a particular rule element, say, a skill with a master feat and a feat corresponding
to that skill.

**Example - (Epic) Skill Focus: Discipline**

.. code:: cpp

    auto& mfr = nw::kernel::rules().master_feats();
    mfr->set_bonus(mfeat_skill_focus, 3);
    mfr->set_bonus(mfeat_skill_focus_epic, 10);

    mfr->add(skill_discipline, mfeat_skill_focus, feat_skill_focus_discipline);
    mfr->add(skill_discipline, mfeat_skill_focus_epic, feat_epic_skill_focus_discipline)

Multiple feats are able to be associated with a rule element and masterfeat.  Imagine in some universe,
there is a class that has access to a generic Weapon Focus: Martial feat which provides Weapon Focus
for all martial weapons.

**Example - Multiple Associated Feats**

.. code:: cpp

    auto& mfr = nw::kernel::rules().master_feats;
    // Set up bonuses...
    mfr->set_bonus(mfeat_weapon_focus, 1);
    mfr->set_bonus(mfeat_weapon_focus_epic, 2);

    // Register feats
    mfr.add(baseitem_longsword, mfeat_weapon_focus, feat_weapon_focus_longsword);
    mfr.add(baseitem_longsword, mfeat_weapon_focus, feat_weapon_focus_martial);
    mfr.add(baseitem_longsword, mfeat_weapon_focus_epic, feat_epic_weapon_focus_longsword);
    mfr.add(baseitem_longsword, mfeat_weapon_focus_epic, feat_epic_weapon_focus_martial);

    // Process
    auto callback = [](int value) { /* do something with value */ };
    nw::kernel::resolve_master_feats<int>(cre, baseitem, callback,
       mfeat_weapon_focus, mfeat_weapon_focus_epic);

    // Simple sums of master feat bonuses can be done as below.
    int value = nw::kernel::sum_master_feats<int>(cre, baseitem,
       mfeat_weapon_focus, mfeat_weapon_focus_epic);

    // If you are only interested in resolving one master feat you can get that result
    // directly:
    int value2 = nw::kernel::resolve_master_feat<int>(cre, baseitem, mfeat_weapon_focus);


-------------------------------------------------------------------------------

Requirements
------------

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

.. [1]
   There are some exceptions, parts of the custom spellcaster system.
.. [2]
   One could imagine in a different context, say NWNX:EE, you could add a callback to
   nwnx_dotnet/lua/etc or a string for use with ``ExecuteScriptChunk``.
