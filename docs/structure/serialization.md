# Serialization

## Definitions

* **profile** - NWN has three different (de)serialization profiles:

    * **blueprint** - UTC, UTT, etc, etc.  BIC is included here, though not a blueprint itself.
    * **instance** - Instances of blueprints stored in an area's GIT file.
    * **savegame** - All game and object state.  This is outside of the scope of this library.. for now.

* **type** - C++ types corresponding to GFF serialization types.
    * `uint8_t` - Also convertible to `bool`
    * `int8_t`
    * `uint16_t`
    * `int16_t`
    * `uint32_t`
    * `int32_t`
    * `uint64_t`
    * `int64_t`
    * `float`
    * `double`
    * `std::string`
    * `Resref`
    * `LocString`
    * `ByteArray`
    * Scoped Enumerations are convertible when their underlying type matches the GFF type.

The library may support the lifting of numeric types, i.e. reading a `int16_t` into `int16_t` or `int32_t` or `int64_t`.

* **struct** is a collection of key-value pairs, where the key is a 16 character string and the value is one of the above types (almost).

* **list** is a list solely of structs, this follows the GFF pattern.

* **gffjson** refers to the nwn-lib/neverwinter.nim json format that mimics GFF.  The extent to which this is supported by the library is an open issue.

* **json** refers specifically to libnw json serialization.  This very closely mimics the structure of a given object, such that if you load the JSON into another language, or a dynamic language that can construct arbitrary objects from JSON, the usage is identical or analogous to the C++ objects.

## Sample libnw JSON serialization format

```json
{
    "$type": "UTC",
    "$version": 1,
    "appearance": {
        "body_parts": {
            "belt": 0,
            "left_bicep": 1,
            "left_foot": 1,
            "left_forearm": 1,
            "left_hand": 1,
            "left_shin": 1,
            "left_shoulder": 0,
            "left_thigh": 1,
            "neck": 1,
            "pelvis": 1,
            "right_bicep": 1,
            "right_foot": 1,
            "right_forearm": 1,
            "right_hand": 1,
            "right_shin": 1,
            "right_shoulder": 0,
            "right_thigh": 1,
            "torso": 1
        },
        "hair": 167,
        "id": 6,
        "phenotype": 0,
        "portrait_id": 65,
        "skin": 3,
        "tail": 0,
        "tattoo1": 1,
        "tattoo2": 1,
        "wings": 0
    },
    "bodybag": 0,
    "chunk_death": 0,
    "combat_info": {
        "ac_natural": 0,
        "special_abilities": [
            {
                "flags": 1,
                "level": 15,
                "spell": 120
            }
        ]
    },
    "common": {
        "comment": "",
        "faction": 0,
        "local_data": {
            "DIPType": {
                "integer": 3
            },
            "DeflectionAC": {
                "integer": 6
            },
            "DodgeAC": {
                "integer": 6
            },
            "OtherImmunes": {
                "integer": 1001945111
            },
            "Soak": {
                "string": "15+5"
            },
            "VFXDur1": {
                "integer": 11
            },
            "rlgs_ss_1": {
                "string": "lt_agent_1"
            }
        },
        "object_type": 5,
        "palette_id": 0,
        "resref": "pl_agent_001",
        "tag": "pl_agent_001"
    },
    "conversation": "",
    "cr": 38.0,
    "cr_adjust": -36,
    "decay_time": 5000,
    "deity": "",
    "description": {
        "strings": [],
        "strref": 4294967295
    },
    "disarmable": 0,
    "equipment": {
        "arms": "handwish",
        "arrows": "",
        "belt": "",
        "bolts": "",
        "boots": "",
        "bullets": "",
        "chest": "dk_agent_thread2",
        "cloak": "",
        "creature_bite": "",
        "creature_left": "pl_slam_1d2",
        "creature_right": "",
        "creature_skin": "",
        "head": "",
        "lefthand": "",
        "leftring": "",
        "neck": "",
        "righthand": "",
        "rightring": ""
    },
    "faction_id": 1,
    "gender": 0,
    "good_evil": 100,
    "hp": 894,
    "hp_current": 894,
    "hp_max": 1014,
    "immortal": 0,
    "interruptable": 0,
    "inventory": [],
    "lawful_chaotic": 50,
    "lootable": 0,
    "name_first": {
        "strings": [
            {
                "lang": 0,
                "string": "Agent"
            }
        ],
        "strref": 4294967295
    },
    "name_last": {
        "strings": [],
        "strref": 4294967295
    },
    "pc": 0,
    "perception_range": 11,
    "plot": 0,
    "race": 6,
    "scripts": {
        "on_attacked": "mon_ai_5attacked",
        "on_blocked": "mon_ai_13blocked",
        "on_conversation": "mon_ai_4conv",
        "on_damaged": "mon_ai_6dmged",
        "on_death": "mon_ai_7death",
        "on_disturbed": "mon_ai_8disturb",
        "on_endround": "mon_ai_3ocre",
        "on_heartbeat": "mon_ai_1hb",
        "on_perceived": "mon_ai_2percep",
        "on_rested": "mon_ai_10rest",
        "on_spawn": "mon_ai_9spawn",
        "on_spell_cast_at": "mon_ai_11spcast",
        "on_user_defined": "mon_ai_12ud"
    },
    "soundset": 171,
    "starting_package": 4,
    "stats": {
        "abilities": [
            40,
            13,
            16,
            10,
            16,
            9
        ],
        "feats": [
            2,
            3,
            4,
            6,
            8,
            10,
            21,
            26,
            32,
            41,
            45,
            46,
            49,
            206,
            207,
            208,
            209,
            211,
            212,
            214,
            215,
            216,
            258,
            260,
            289,
            290,
            291,
            292,
            297,
            391,
            392,
            408,
            755,
            756,
            757,
            971,
            1089
        ],
        "save_bonus": {
            "fort": 9,
            "reflex": 15,
            "will": 13
        },
        "skills": [
            0,
            1,
            0,
            40,
            11,
            30,
            30,
            1,
            30,
            0,
            20,
            0,
            30,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            1,
            0,
            0,
            1,
            2,
            0
        ]
    },
    "subrace": "",
    "walkrate": 4
}
```
