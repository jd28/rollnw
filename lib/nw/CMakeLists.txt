find_package(Threads)

add_library(nw STATIC
    functions.cpp
    log.cpp

    formats/Dialog.cpp
    formats/Faction.cpp
    formats/Image.cpp
    formats/Ini.cpp
    formats/Journal.cpp
    formats/Palette.cpp
    formats/Plt.cpp
    formats/StaticTwoDA.cpp
    formats/Txi.cpp
    formats/TwoDA.cpp

    i18n/conversion.cpp
    i18n/Language.cpp
    i18n/LocString.cpp
    i18n/Tlk.cpp

    kernel/Kernel.cpp
    kernel/Config.cpp
    kernel/FactionSystem.cpp
    kernel/ModelCache.cpp
    kernel/EventSystem.cpp
    kernel/Memory.cpp
    kernel/Rules.cpp
    kernel/Strings.cpp
    kernel/TilesetRegistry.cpp
    kernel/TwoDACache.cpp

    model/Mdl.cpp
    model/MdlTextParser.cpp
    model/MdlBinaryParser.cpp
    model/mdl_particle_import.cpp

    render/particle_compile.cpp
    render/particle_json.cpp
    render/particle_render.cpp
    render/particle_system.cpp

    # Objects
    objects/Area.cpp
    objects/AreaTransforms.cpp
    objects/Creature.cpp
    objects/Door.cpp
    objects/Encounter.cpp
    objects/Item.cpp
    objects/Module.cpp
    objects/ObjectBase.cpp
    objects/ObjectComponentSystem.cpp
    objects/ObjectHandle.cpp
    objects/ObjectManager.cpp
    objects/Placeable.cpp
    objects/Player.cpp
    objects/Sound.cpp
    objects/Store.cpp
    objects/Trigger.cpp
    objects/Waypoint.cpp
    # Components
    objects/Equips.cpp
    objects/Inventory.cpp
    objects/LevelHistory.cpp
    objects/LocalData.cpp
    objects/Location.cpp

    profiles/nwn1/body_part_catalog.cpp
    profiles/nwn1/creature_ability_loadout.cpp
    profiles/nwn1/equipment_gff.cpp
    profiles/nwn1/gff_propset_component_json.cpp
    profiles/nwn1/geometry_component_gff.cpp
    profiles/nwn1/inventory_component_gff.cpp
    profiles/nwn1/item_materialization.cpp
    profiles/nwn1/legacy_gff_compat.cpp
    profiles/nwn1/legacy_spellbook_gff.cpp
    profiles/nwn1/object_name_preview.cpp
    profiles/nwn1/object_compat_fields.cpp
    profiles/nwn1/player_levelup_bridge.cpp
    profiles/nwn1/propset_gff_policy.cpp
    profiles/nwn1/propset_gff_importer.cpp
    profiles/nwn1/propset_gff_exporter.cpp
    profiles/nwn1/scriptbridge.cpp
    profiles/nwn1/store_inventory_gff.cpp

    resources/assets.cpp
    resources/Container.cpp
    resources/Erf.cpp
    resources/ResourceManifest.cpp
    resources/ResourceManager.cpp
    resources/StaticDirectory.cpp
    resources/StaticErf.cpp
    resources/StaticKey.cpp
    resources/StaticZip.cpp

    rules/attributes.cpp
    rules/Class.cpp
    rules/combat.cpp
    rules/combat_scheduler.cpp
    rules/creature_body_parts.cpp
    rules/Dice.cpp
    rules/effects.cpp
    rules/feats.cpp
    rules/items.cpp
    rules/RuntimeObject.cpp
    rules/Spell.cpp
    rules/system.cpp

    script/Ast.cpp
    script/Context.cpp
    script/Nss.cpp
    script/NssLexer.cpp
    script/NssParser.cpp

    serialization/gff_common.cpp
    serialization/gff_conversion.cpp
    serialization/component_propset_json.cpp
    serialization/Gff.cpp
    serialization/GffBuilder.cpp
    serialization/Serialization.cpp

    smalls/Array.cpp
    smalls/UnmanagedArray.cpp
    smalls/Ast.cpp
    smalls/AstCompiler.cpp
    smalls/AstResolver.cpp
    smalls/Bytecode.cpp
    smalls/BytecodeVerifier.cpp
    smalls/Context.cpp
    smalls/data_spec.cpp
    smalls/data_transform.cpp
    smalls/Diagnostic.cpp
    smalls/GarbageCollector.cpp
    smalls/Lexer.cpp
    smalls/NameResolver.cpp
    smalls/native/core_array.cpp
    smalls/native/core_area.cpp
    smalls/native/core_creature.cpp
    smalls/native/core_door.cpp
    smalls/native/core_combat.cpp
    smalls/native/core_effects.cpp
    smalls/native/core_item.cpp
    smalls/native/core_map.cpp
    smalls/native/core_math.cpp
    smalls/native/core_module.cpp
    smalls/native/core_object.cpp
    smalls/native/core_placeable.cpp
    smalls/native/core_player.cpp
    smalls/native/core_prelude.cpp
    smalls/native/core_string.cpp
    smalls/native/core_test.cpp
    smalls/native/core_types.cpp
    smalls/native/core_visual.cpp
    smalls/Parser.cpp
    smalls/PropsetPool.cpp
    smalls/propset_json.cpp
    smalls/runtime.cpp
    smalls/ScriptHeap.cpp
    smalls/Smalls.cpp
    smalls/TypeResolver.cpp
    smalls/types.cpp
    smalls/Validator.cpp
    smalls/VirtualMachine.cpp

    util/Allocator.cpp
    util/base64.cpp
    util/ByteArray.cpp
    util/compression.cpp
    util/error_context.cpp
    util/game_install.cpp
    util/HandlePool.cpp
    util/memory.cpp
    util/platform.cpp
    util/string.cpp
    util/Tokenizer.cpp

)

target_compile_definitions(nw PUBLIC -DGLM_ENABLE_EXPERIMENTAL)

target_link_libraries(nw PUBLIC
    fmt::fmt
    absl::base
    absl::flat_hash_map
    absl::hash
    absl::hash_function_defaults
    absl::raw_hash_set
    absl::span
    absl::utility
    absl::strings
    zip
    zlibstatic
    nowide::nowide
    libzstd_static
    sqlite3
    Threads::Threads

    stb_image
    fzy
    inih
    offsetallocator

    coverage_config
)

if(ROLLNW_ENABLE_TRACY)
target_link_libraries(nw PUBLIC Tracy::TracyClient)
target_compile_definitions(nw PUBLIC ROLLNW_ENABLE_TRACY)
endif()

if(NOT LINUX)
target_link_libraries(nw PUBLIC
    iconv
)
endif()

if(ROLLNW_BUILD_WITH_MIMALLOC AND NOT APPLE)
target_link_libraries(nw PUBLIC
    mimalloc-static
)
endif()


include(../../cmake/ProjectSettings.cmake)

include(../../cmake/CompilerWarnings.cmake)
set_project_warnings(nw OWN_SOURCES)

if(NOT MSVC)
  # -Wuseless-cast resolves typedefs on the compiling platform, so what it calls
  # redundant here can be a real widening on Windows, where size_t is unsigned
  # long long rather than unsigned long. Enabling it would mean deciding it per
  # platform. See issues/nw-library-warnings-unchecked.md.
  #
  # The flag is GCC-only, so clang rejects -Wno-useless-cast as unknown.
  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(nw PRIVATE -Wno-useless-cast)
  endif()

  # The VM dispatches through a computed goto, which ISO C++ does not have. It
  # is a deliberate optimization behind a __GNUC__/__clang__ guard, with a
  # switch-based fallback for MSVC, so -Wpedantic reports the design rather than
  # a defect.
  set_source_files_properties(smalls/VirtualMachine.cpp
    PROPERTIES COMPILE_OPTIONS -Wno-pedantic)
endif()

include(../../cmake/Sanitizers.cmake)
enable_sanitizers(nw)

add_subdirectory(nav)
