find_package(Threads)

add_library(nw STATIC
    functions.cpp
    log.cpp
    scriptapi.cpp

    formats/Dialog.cpp
    formats/Faction.cpp
    formats/Image.cpp
    formats/Ini.cpp
    formats/Journal.cpp
    formats/Palette.cpp
    formats/Plt.cpp
    formats/StaticTwoDA.cpp
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

    # Objects
    objects/Area.cpp
    objects/Creature.cpp
    objects/Door.cpp
    objects/Encounter.cpp
    objects/Item.cpp
    objects/Module.cpp
    objects/ObjectBase.cpp
    objects/ObjectHandle.cpp
    objects/ObjectManager.cpp
    objects/Placeable.cpp
    objects/Player.cpp
    objects/Sound.cpp
    objects/Store.cpp
    objects/Trigger.cpp
    objects/Waypoint.cpp
    # Components
    objects/Appearance.cpp
    objects/CombatInfo.cpp
    objects/Common.cpp
    objects/CreatureStats.cpp
    objects/Equips.cpp
    objects/Inventory.cpp
    objects/LevelHistory.cpp
    objects/LevelStats.cpp
    objects/LocalData.cpp
    objects/Location.cpp
    objects/Lock.cpp
    objects/Saves.cpp
    objects/SpellBook.cpp
    objects/Trap.cpp
    objects/VisualTransform.cpp

    profiles/nwn1/constants.cpp
    profiles/nwn1/Profile.cpp
    profiles/nwn1/rules.cpp
    profiles/nwn1/scriptapi.cpp

    resources/assets.cpp
    resources/Container.cpp
    resources/Erf.cpp
    resources/ResourceManager.cpp
    resources/StaticDirectory.cpp
    resources/StaticErf.cpp
    resources/StaticKey.cpp
    resources/StaticZip.cpp

    rules/attributes.cpp
    rules/Class.cpp
    rules/combat.cpp
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
    serialization/Gff.cpp
    serialization/GffBuilder.cpp
    serialization/Serialization.cpp

    smalls/Array.cpp
    smalls/Ast.cpp
    smalls/AstCompiler.cpp
    smalls/AstResolver.cpp
    smalls/Bytecode.cpp
    smalls/BytecodeVerifier.cpp
    smalls/Context.cpp
    smalls/Diagnostic.cpp
    smalls/GarbageCollector.cpp
    smalls/Lexer.cpp
    smalls/NameResolver.cpp
    smalls/native/core_array.cpp
    smalls/native/core_area.cpp
    smalls/native/core_creature.cpp
    smalls/native/core_combat.cpp
    smalls/native/core_door.cpp
    smalls/native/core_encounter.cpp
    smalls/native/core_effects.cpp
    smalls/native/core_item.cpp
    smalls/native/core_map.cpp
    smalls/native/core_math.cpp
    smalls/native/core_module.cpp
    smalls/native/core_object.cpp
    smalls/native/core_placeable.cpp
    smalls/native/core_player.cpp
    smalls/native/core_progression.cpp
    smalls/native/core_prelude.cpp
    smalls/native/core_sound.cpp
    smalls/native/core_store.cpp
    smalls/native/core_string.cpp
    smalls/native/core_test.cpp
    smalls/native/core_trigger.cpp
    smalls/native/core_waypoint.cpp
    smalls/Parser.cpp
    smalls/PropsetPool.cpp
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
set_project_warnings(nw)

include(../../cmake/Sanitizers.cmake)
enable_sanitizers(nw)
