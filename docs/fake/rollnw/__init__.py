import enum
from enum import auto

# Platform ####################################################################
###############################################################################


class GameVersion(enum.Enum):
    """Game versions"""
    v1_69 = auto()
    vEE = auto()


class PathAlias(enum.Enum):
    """Path aliases, some of these are EE only."""
    ambient = auto()
    cache = auto()
    currentgame = auto()
    database = auto()
    development = auto()
    dmvault = auto()
    hak = auto()
    hd0 = auto()
    localvault = auto()
    logs = auto()
    modelcompiler = auto()
    modules = auto()
    movies = auto()
    music = auto()
    nwsync = auto()
    oldservervault = auto()
    override = auto()
    patch = auto()
    portraits = auto()
    saves = auto()
    screenshots = auto()
    servervault = auto()
    temp = auto()
    tempclient = auto()
    tlk = auto()
    user = hd0

# Components ##################################################################
###############################################################################


class Appearance:
    """Class containing creature's appearance

    Attributes:
        body_parts (BodyParts): body_parts
        hair (int): hair
        id (int): Index into ``appearance.2da``
        phenotype (int): phenotype
        portrait_id (int): Index into ``portraits.2da``
        skin (int): skin
        tail (int): tail
        tattoo1 (int): tattoo1
        tattoo2 (int): tattoo2
        wings (int): wings"""
    pass


class BodyParts:
    """Class containing references to creature's body parts

    Attributes:
        belt (int): body part
        bicep_left (int): body part
        bicep_right (int): body part
        foot_left (int): body part
        foot_right (int): body part
        forearm_left (int): body part
        forearm_right (int): body part
        hand_left (int): body part
        hand_right (int): body part
        head (int): body part
        neck (int): body part
        pelvis (int): body part
        shin_left (int): body part
        shin_right (int): body part
        shoulder_left (int): body part
        shoulder_right (int): body part
        thigh_left (int): body part
        thigh_right (int): body part"""
    pass


class CombatInfo:
    """Class containing combat related data

    Attributes:
        ac_natural_bonus (int)
        ac_armor_base (int)
        ac_shield_base (int)
        combat_mode (int)
        target_state (int)
        size_ab_modifier (int)
        size_ac_modifier (int)
    """
    pass


class Common:
    """Class containing attributes common to all objects

    Attributes:
        resref (str): resref
        tag (str): tag
        name (LocString): name
        locals (LocalData): locals
        location (Location): location
        comment (str): comment
        palette_id (int): palette_id
    """
    pass


class CreatureScripts:
    """A class containing a creature's script set.

    Attributes:
        on_attacked (str): A script
        on_blocked (str): A script
        on_conversation (str): A script
        on_damaged (str): A script
        on_death (str): A script
        on_disturbed (str): A script
        on_endround (str): A script
        on_heartbeat (str): A script
        on_perceived (str): A script
        on_rested (str): A script
        on_spawn (str): A script
        on_spell_cast_at (str): A script
        on_user_defined (str): A script
    """
    pass


class CreatureStats:
    """Implementation of a creature's general attributes and stats"""

    def add_feat(self, feat) -> bool:
        """Attempts to add a feat to a creature, returning true if successful
        """
        pass

    def get_ability_score(self, id: int):
        """Gets an ability score"""
        pass

    def get_skill_rank(self, id: int):
        """Gets a skill rank"""
        pass

    def has_feat(self, feat) -> bool:
        """Determines if creature has feat
        """
        pass

    def set_ability_score(self, id: int,  value: int) -> bool:
        """Sets an ability score, returning true if successful"""
        pass

    def set_skill_rank(self, id: int,  value: int) -> bool:
        """Sets a skill rank, returning true if successful"""
        pass


class DoorScripts:
    """Door's scripts

    Attributes:
        on_click (str)
        on_closed (str)
        on_damaged (str)
        on_death (str)
        on_disarm (str)
        on_heartbeat (str)
        on_lock (str)
        on_melee_attacked (str)
        on_open_failure (str)
        on_open (str)
        on_spell_cast_at (str)
        on_trap_triggered (str)
        on_unlock (str)
        on_user_defined (str)
    """
    pass


class EquipSlot(enum.Flag):
    """Equipment slot flags
    """
    head = (1 << 0)
    chest = (1 << 1)
    boots = (1 << 2)
    arms = (1 << 3)
    righthand = (1 << 4)
    lefthand = (1 << 5)
    cloak = (1 << 6)
    leftring = (1 << 7)
    rightring = (1 << 8)
    neck = (1 << 9)
    belt = (1 << 10)
    arrows = (1 << 11)
    bullets = (1 << 12)
    bolts = (1 << 13)
    creature_left = (1 << 14)
    creature_right = (1 << 15)
    creature_bite = (1 << 16)
    creature_skin = (1 << 17)


class EquipIndex(enum.IntEnum):
    head = 0
    chest = 1
    boots = 2
    arms = 3
    righthand = 4
    lefthand = 5
    cloak = 6
    leftring = 7
    rightring = 8
    neck = 9
    belt = 10
    arrows = 11
    bullets = 12
    bolts = 13
    creature_left = 14
    creature_right = 15
    creature_bite = 16
    creature_skin = 17

    invalid = 0xffffffff


class Equips:
    """Creature's equipment

    Attributes:
        equips
    """

    def instantiate(self):
        """Instantiates equipment by loading contained items from the resource manager"""
        pass


class InventoryItem:
    """An inventory item

    Attributes:
        infinite (bool): Only applicable to stores
        x (int)
        y (int)
        item (str | Item)
    """
    pass


class Inventory:
    """An Object's inventory

    Attributes:
        items ([rollnw.InventoryItem])
        owner
    """

    def instantiate(self):
        """Instantiates inventory by loading contained items from the resource manager"""
        pass


class ClassEntry:
    """Class level data

    Attributes:
        id
        level
        spells
    """
    pass


class LevelUp:
    """Level up data

    Attributes:
        class_ (int): Class the level was taken as
        ability (int): Ability score that was raised, if any.
        epic (bool): ``True`` if level is an epic level
        feats ([int]): Added feats
        hitpoints (int): Hitpoints gained.
        known_spells ((int, int)): Level, Spell pair for gained spells
        skillpoints (int): Roll over skill points
        skills ([(int, int)]): Skill and the amount increased
    """
    pass


class LevelHistory:
    """Implements a creatures levelup history

    Attributes:
        entries ([LevelUp]): Entries for levels
    """
    pass


class LevelStats:
    """Implements a creatures level related stats

    Attributes:
        entries ([ClassEntry]): Entries for levels
    """

    def level(self) -> int:
        """Gets total level"""
        pass

    def level_by_class(self, class_: int) -> int:
        """Gets level by class"""
        pass


class Location:
    """Class representing an objects location

    Attributes:
        area
        orientation
        position
    """
    pass


class LocalData:
    def delete_float(self, varname: str):
        """Deletes float variable"""
        pass

    def delete_int(self, varname: str):
        """Deletes int variable"""
        pass

    def delete_object(self, varname: str):
        """Deletes object variable"""
        pass

    def delete_string(self, varname: str):
        """Deletes string variable"""
        pass

    def delete_location(self, varname: str):
        """Deletes location variable"""
        pass

    def get_float(self, varname: str):
        """Gets float variable"""
        pass

    def get_int(self, varname: str) -> int:
        """Gets int variable"""
        pass

    def get_object(self, varname: str):
        """Gets object variable"""
        pass

    def get_string(self, varname: str) -> str:
        """Gets string variable"""
        pass

    def get_location(self, varname: str):
        """Gets location variable"""
        pass

    def set_float(self, varname: str, value: float):
        """Sets float variable"""
        pass

    def set_int(self, varname: str, value: int):
        """Sets int variable"""
        pass

    def set_object(self, varname: str, value: 'ObjectHandle'):
        """Sets object variable"""
        pass

    def set_string(self, varname: str, value: str):
        """Sets string variable"""
        pass

    def set_location(self, varname: str, value: Location):
        """Sets location variable"""
        pass

    def size(self):
        """Gets number of variables"""
        pass
    pass


class Lock:
    """Class representing a lock on an object

    Attributes:
        key_name (str)
        key_required (bool)
        lockable (bool)
        locked (bool)
        lock_dc (int)
        unlock_dc (int)
        remove_key (bool)
    """
    pass


class Saves:
    """An objects saves

    Attributes:
        fort (int)
        reflex (int)
        will (int)
    """


class SpellFlags(enum.Flag):
    none = 0x0
    readied = 0x01
    spontaneous = 0x02
    unlimited = 0x04


class SpellMetaMagic(enum.Flag):
    none = 0x00,
    empower = 0x01,
    extend = 0x02,
    maximize = 0x04,
    quicken = 0x08,
    silent = 0x10,
    still = 0x20,


class SpellEntry:
    """An entry in a spellbook

    Attributes:
        spell
        meta
        flags
    """
    pass


class SpellBook:
    """Implements a spell casters spellbook"""

    def add_known_spell(self, level: int,  entry: SpellEntry):
        """Adds a known spell at level"""
        pass

    def add_memorized_spell(self, level: int, entry: SpellEntry):
        """Adds a memorized spell at level"""
        pass

    def get_known_spell_count(self, level: int):
        """Gets the number of known at a given level"""
        pass

    def get_memorized_spell_count(self, level: int):
        """Gets the number of memorized at a given level"""
        pass

    def get_known_spell(self, level: int, index: int):
        """Gets a known spell entry"""
        pass

    def get_memorized_spell(self, level: int, index: int):
        """Gets a memorized spell entry"""
        pass

    def remove_known_spell(self, level: int, entry: SpellEntry):
        """Removes a known spell entry"""
        pass

    def remove_memorized_spell(self, level: int, entry: SpellEntry):
        """Removes a memorized spell entry"""
        pass


class SpecialAbility:
    """Special Ability

    Attributes:
        spell
        level
        flags (SpellFlags)
    """


class Trap:
    """Class representing a trap on an object

    Attributes:
        detect_dc (int)
        detectable (bool)
        disarm_dc (int)
        disarmable (bool)
        is_trapped (bool)
        one_shot (bool)
        type
    """
    pass


# Formats #####################################################################
###############################################################################

class Image:
    """Loads an image

    Args:
        filename (str): image file to load
    """

    def channels(self):
        """Gets BPP
        """
        pass

    def data(self):
        """Get raw data
        """
        pass

    def height(self):
        """Get height
        """
        pass

    def valid(self):
        """Determine if successfully loaded.
        """
        pass

    def width(self):
        """Get width
        """
        pass

    def write_to(self):
        """Write Image to file
        """
        pass


class Ini:
    """Loads an ini

    Args:
        filename (str): ini file to load
    """

    def get_int(self, key: str) -> int | None:
        """Gets an INI value"""
        pass

    def get_float(self, key: str) -> float | None:
        """Gets an INI value"""
        pass

    def get_str(self, key: str) -> str | None:
        """Gets an INI value"""
        pass

    def valid(self):
        """Deterimes if Ini file was successfully parsed"""
        pass


class PltLayer(enum.IntEnum):
    """Plt layers"""
    plt_layer_skin = 0
    plt_layer_hair = 1
    plt_layer_metal1 = 2
    plt_layer_metal2 = 3
    plt_layer_cloth1 = 4
    plt_layer_cloth2 = 5
    plt_layer_leather1 = 6
    plt_layer_leather2 = 7
    plt_layer_tattoo1 = 8
    plt_layer_tattoo2 = 9


class PltPixel:
    """Plt pixel

    Attributes:
        color
        layer
    """
    pass


class PltColors:
    """Plt Color Array

    Attributes:
        colors

    Notes:
        This would be the colors that a player would select"""
    pass


class Plt:
    """Implementation of PLT file format"""

    def height(self):
        pass

    def pixels(self):
        pass

    def valid(self):
        pass

    def width(self):
        pass


def decode_plt_color(plt: Plt, colors, x: int, y: int) -> list[int]:
    """Decodes PLT and user selected colors to RBGA
    """
    pass


class TwoDA:
    """Implementation of 2da file format

    Args:
        filename (str): 2da file to load
    """

    def __init__(self, filename: str):
        """Loads 2da from `filename`"""
        pass

    def get(self, row: int, column: int | str):
        """Gets a TwoDA value

        Args:
            row (int): Row number
            column (int | str): Column number or label

        Returns:
            An int | float | string depending on the underlying value
        """
        pass

    def set(self, row: int, column: int | str, value: int | float | str):
        """Sets a TwoDA value

        Args:
            row (int): Row number
            column (int | str): Column number or label
            value (int | float | str): New value
        """
        pass


# i18n ########################################################################
###############################################################################

class LanguageID(enum.IntEnum):
    invalid = -1
    english = 0
    french = 1
    german = 2
    italian = 3
    spanish = 4
    polish = 5
    korean = 128
    chinese_traditional = 129
    chinese_simplified = 130
    japanese = 131


class Language:
    @staticmethod
    def encoding(language: LanguageID) -> str:
        """Gets the encoding for a particular language
        """
        pass

    @staticmethod
    def from_string(string: str) -> LanguageID:
        """Converts string (short or long form) to ID
        """
        pass

    @staticmethod
    def has_feminine(language: LanguageID) -> bool:
        """Determines if language has feminine translations
        """
        pass

    @staticmethod
    def to_base_id(id: int) -> tuple[LanguageID, bool]:
        """Convert runtime language identifier to base language and bool indicating masc/fem.
        """
        pass

    @staticmethod
    def to_runtime_id(language: LanguageID, feminine: bool = False) -> int:
        """Convert language ID to runtime identifier.
        """
        pass

    @staticmethod
    def to_string(language: LanguageID, long_name: bool = False) -> str:
        """Converts language to string form
        """
        pass


class LocString:
    """Implements a localized string

    Args:
        strref (int): String reference.  (default -1)
    """

    def __init__(self, strref: int = -1):
        pass

    def __getitem__(self, language: LanguageID) -> str:
        """Gets an item.  Only useful for languages without gender, so English.
        """

    def add(self, language: LanguageID, string: str, feminine: bool = False):
        """Adds a localized string"""
        pass

    def contains(self, language: LanguageID, feminine: bool = False):
        """Checks if a localized string is contained"""
        pass

    def get(self, language: LanguageID, feminine: bool = False):
        """Gets a localized string"""
        pass

    def remove(self, language: LanguageID, feminine: bool = False):
        """Removes a localized string"""
        pass

    def size(self):
        """Gets number of localized strings"""
        pass

    def strref(self):
        """Gets string reference"""
        pass

    def to_dict(self) -> dict:
        """Converts ``LocString`` to python ``dict``"""
        pass

    @staticmethod
    def from_dict(data: dict):
        """Converts python ``dict`` to ``LocString``"""


class Tlk:
    """Implementation of the TLK file format

    Args:
        init (str | LanguageID): if passed a string, ``init`` will be treated as a path to a TLK file,
            if passed a LanguageID, default constructs with the TLKs language set to ``init``.
    """

    def __init__(self, init: str | LanguageID):
        pass

    def __getitem__(self, strref: int) -> str:
        """Gets a tlk entry."""
        pass

    def __setitem__(self, strref: int, string: str):
        """Sets a tlk entry."""
        pass

    def __len__(self):
        """Gets the highest set TLK entry"""

    def get(self, strref: int) -> str:
        """Gets a tlk entry."""
        pass

    def language_id(self):
        """Gets the language ID"""
        pass

    def modified(self):
        """Is Tlk modfied"""
        pass

    def save(self):
        """Writes TLK to file"""
        pass

    def save_as(self, path: str):
        """Writes TLK to file"""
        pass

    def set(self, strref: int, string: str):
        """Sets a localized string"""
        pass

    def size(self):
        """Gets the highest set strref"""
        pass

    def valid(self):
        """Gets if successfully parsed"""
        pass


# Objects #####################################################################
###############################################################################


class ObjectBase:
    def handle(self):
        """Gets object handle"""
        pass
    pass


class ObjectType(enum.Enum):
    """Object types"""
    invalid = auto()
    gui = auto()
    tile = auto()
    module = auto()
    area = auto()
    creature = auto()
    item = auto()
    trigger = auto()
    projectile = auto()
    placeable = auto()
    door = auto()
    areaofeffect = auto()
    waypoint = auto()
    encounter = auto()
    store = auto()
    portal = auto()
    sound = auto()


class ObjectHandle:
    """Object handle

    Attributes:
        id (int): index into object array
        version (int): object index version
        type (ObjectType): object type
    """

    def valid(self):
        """Determines if handle is valid"""
        pass


class AreaFlags(enum.Flag):
    none = 0
    interior = 0x0001
    underground = 0x0002
    natural = 0x0004


class Area:
    """Area object

    Attributes:
        comments
        creator_id
        creatures
        doors
        encounters
        flags
        height
        id
        items
        listen_check_mod
        loadscreen
        name
        no_rest
        placeables
        pvp
        scripts
        shadow_opacity
        skybox
        sounds
        spot_check_mod
        stores
        tiles
        tileset
        triggers
        version
        waypoints
        weather
        width
    """
    pass


class AreaScripts:
    """Area's scripts

    Attributes:
        on_enter(str)
        on_exit(str)
        on_heartbeat(str)
        on_user_defined(str)
    """
    pass


class AreaWeather:
    """Area's weather

    Attributes:
        chance_lightning
        chance_rain
        chance_snow
        color_moon_ambient
        color_moon_diffuse
        color_moon_fog
        color_sun_ambient
        color_sun_diffuse
        color_sun_fog
        fog_clip_distance
        wind_power
        day_night_cycle
        is_night
        lighting_scheme
        fog_moon_amount
        moon_shadows
        fog_sun_amount
        sun_shadows
    """
    pass


class Creature(ObjectBase):
    """Class that represents a Creature object

    Attributes:
        appearance (Appearance)
        bodybag
        chunk_death
        combat_info
        common (Common)
        conversation (str): Dialog resref
        cr
        cr_adjust
        decay_time
        deity
        description
        disarmable
        faction_id
        gender
        good_evil
        history (LevelHistory)
        hp
        hp_current
        hp_max
        immortal
        interruptable
        lawful_chaotic
        levels
        lootable
        name_first
        name_last
        pc
        perception_range
        plot
        race
        scripts
        soundset
        starting_package
        stats (CreatureStats): Offensive and defensive stats.
        subrace (str): Subrace
        walkrate
    """

    @staticmethod
    def from_dict(value: dict):
        """Constructs object from python dict.
        """
        pass

    @staticmethod
    def from_file(path: str):
        """Constructs object from file.  The file can be JSON or Gff.
        """
        pass


class DoorAnimationState(enum.Enum):
    """Door animation states"""
    closed = auto()
    opened1 = auto()
    opened2 = auto()


class Door:
    """Class that represents a Door object

    Attributes:
        animation_state
        appearance
        conversation (str): Door's conversation resref
        description
        faction
        generic_type
        hardness
        hp
        hp_current
        interruptable
        linked_to
        linked_to_flags
        loadscreen
        lock (Lock)
        plot
        portrait_id
        saves
        scripts (DoorScripts)
        trap (Trap)
    """
    @staticmethod
    def from_dict(value: dict):
        """Constructs object from python dict.
        """
        pass

    @staticmethod
    def from_file(path: str):
        """Constructs object from file.  The file can be JSON or Gff.
        """
        pass
    pass


class EncounterScripts:
    """Encounter's scripts

    Attributes:
        on_entered (str)
        on_exhausted (str)
        on_exit (str)
        on_heartbeat (str)
        on_user_defined (str)
    """


class SpawnCreature:
    """Encounter creature spawn

    Attributes:
        appearance
        cr
        resref
        single_spawn
    """


class SpawnPoint:
    """A spawn point

    Attributes:
        orientation (vec3)
        position (vec3)
    """
    pass


class Encounter:
    """Class that represents an Encounter object

    Attributes:
        active (bool)
        creatures
        creatures_max (int)
        creatures_recommended
        difficulty
        difficulty_index
        faction
        geometry
        player_only
        reset
        reset_time
        respawns
        scripts
        spawn_option
        spawn_points
    """
    @staticmethod
    def from_dict(value: dict):
        """Constructs object from python dict.
        """
        pass

    @staticmethod
    def from_file(path: str):
        """Constructs object from file.  The file can be JSON or Gff.
        """
        pass


class ItemModelType(enum.Enum):
    simple = auto()
    layered = auto()
    composite = auto()
    armor = auto()


class ItemColors(enum.Enum):
    cloth1 = auto()
    cloth2 = auto()
    leather1 = auto()
    leather2 = auto()
    metal1 = auto()
    metal2 = auto()


class ItemModelParts(enum.Enum):
    model1 = auto()
    model2 = auto()
    model3 = auto()
    armor_belt = auto()
    armor_lbicep = auto()
    armor_lfarm = auto()
    armor_lfoot = auto()
    armor_lhand = auto()
    armor_lshin = auto()
    armor_lshoul = auto()
    armor_lthigh = auto()
    armor_neck = auto()
    armor_pelvis = auto()
    armor_rbicep = auto()
    armor_rfarm = auto()
    armor_rfoot = auto()
    armor_rhand = auto()
    armor_robe = auto()
    armor_rshin = auto()
    armor_rshoul = auto()
    armor_rthigh = auto()
    armor_torso = auto()


class ItemProperty:
    """An item property

    Attributes:
        type
        subtype
        cost_table
        cost_value
        param_table
        param_value
    """
    pass


class Item:
    """Class that represents an Item object

    Attributes:
        additional_cost
        baseitem
        charges
        cost
        cursed
        description (LocString): Description
        description_id (LocString): Description after being identified.
        identified
        inventory
        model_colors
        model_parts
        model_type
        plot (bool): Is a plot item.
        properties
        stacksize
        stolen
    """
    @staticmethod
    def from_dict(value: dict):
        """Constructs object from python dict.
        """
        pass

    @staticmethod
    def from_file(path: str):
        """Constructs object from file.  The file can be JSON or Gff.
        """
        pass
    pass


class ModuleScripts:
    """Module's Scripts

    Attributes:
        on_client_enter
        on_client_leave
        on_cutsnabort
        on_heartbeat
        on_item_acquire
        on_item_activate
        on_item_unaquire
        on_load
        on_player_chat
        on_player_death
        on_player_dying
        on_player_equip
        on_player_level_up
        on_player_rest
        on_player_uneqiup
        on_spawnbtndn
        on_start
        on_user_defined
    """
    pass


class Module:
    """Class that represents a Module object

    Attributes:
        creator
        dawn_hour
        description
        dusk_hour
        entry_area
        entry_orientation
        entry_position
        expansion_pack
        haks
        id
        is_save_game
        locals
        min_game_version
        minutes_per_hour
        name
        scripts
        start_day
        start_hour
        start_month
        start_movie
        start_year
        tag
        tlk
        version
        xpscale
    """

    def __iter__(self):
        """Get iterator of areas in the module"""
        pass

    def __len__(self):
        """Get the number of areas in the module"""
        pass


class ModuleScripts:
    """Module Scripts

    Attributes:
        on_client_enter
        on_client_leave
        on_cutsnabort
        on_heartbeat
        on_item_acquire
        on_item_activate
        on_item_unaquire
        on_load
        on_player_chat
        on_player_death
        on_player_dying
        on_player_equip
        on_player_level_up
        on_player_rest
        on_player_uneqiup
        on_spawnbtndn
        on_start
        on_user_defined
    """
    pass


class PlaceableAnimationState(enum.Enum):
    none = auto()
    open = auto()
    closed = auto()
    destroyed = auto()
    activated = auto()
    deactivated = auto()


class PlaceableScripts:
    """Placeable's scripts

    Attributes:
        on_click (str)
        on_closed (str)
        on_damaged (str)
        on_death (str)
        on_disarm (str)
        on_heartbeat (str)
        on_inventory_disturbed (str)
        on_lock (str)
        on_melee_attacked (str)
        on_open (str)
        on_spell_cast_at (str)
        on_trap_triggered (str)
        on_unlock (str)
        on_used (str)
        on_user_defined (str)
    """
    pass


class Placeable(ObjectBase):
    """Class that represents a Placeable object

    Attributes:
        animation_state
        appearance
        bodybag
        common
        conversation
        description
        faction
        hardness
        has_inventory
        hp
        hp_current
        interruptable
        inventory
        lock
        plot
        portrait_id
        saves
        scripts (PlaceableScripts):
        static
        trap
        useable
    """
    @staticmethod
    def from_dict(value: dict):
        """Constructs object from python dict.
        """
        pass

    @staticmethod
    def from_file(path: str):
        """Constructs object from file.  The file can be JSON or Gff.
        """
        pass
    pass


class Player(Creature):
    """Player character

    Warnings
    --------
        This is very incomplete
    """
    pass


class Sound(ObjectBase):
    """Class that represents a Sound object

    Attributes:
        active
        common (Common): Common object component
        continuous
        distance_max
        distance_min
        elevation
        generated_type
        hours
        interval
        interval_variation
        looping
        pitch_variation
        positional
        priority
        random
        random_position
        random_x
        random_y
        sounds ([rollnw.Resref]): Sounds
        times
        volume
        volume_variation
    """

    @staticmethod
    def from_dict(value: dict):
        """Constructs object from python dict.
        """
        pass

    @staticmethod
    def from_file(path: str):
        """Constructs object from file.  The file can be JSON or Gff.
        """
        pass
    pass


class StoreScripts:
    """A Store's scripts

    Attributes:
        on_closed (str)
        on_opened (str)
    """
    pass


class Store(ObjectBase):
    """Class that represents a Store object

    Attributes:
        armor
        blackmarket
        blackmarket_markdown
        gold
        identify_price
        markdown
        markup
        max_price
        miscellaneous
        potions
        rings
        scripts (StoreScripts)
        weapons

    //.def_readonly("will_not_buy", &nw::Store::will_not_buy)
    //.def_readonly("will_only_buy", &nw::Store::will_only_buy)
    """

    @staticmethod
    def from_dict(value: dict) -> 'Store':
        """Constructs object from python dict.
        """
        pass

    @staticmethod
    def from_file(path: str) -> 'Store':
        """Constructs object from file.  The file can be JSON or Gff.
        """
        pass
    pass


class Tile:
    """Area tile

    Attributes:
        id
        height
        orientation
        animloop1
        animloop2
        animloop3
        mainlight1
        mainlight2
        srclight1
        srclight2
    """
    pass


class TriggerScripts:
    """A trigger's scripts

    Attributes:
        on_click (str)
        on_disarm (str)
        on_enter (str)
        on_exit (str)
        on_heartbeat (str)
        on_trap_triggered (str)
        on_user_defined (str)
    """
    pass


class Trigger(ObjectBase):
    """Class that represents a Trigger object

    Attributes:
        cursor
        faction
        geometry
        highlight_height
        linked_to
        linked_to_flags
        loadscreen
        portrait
        scripts (TriggerScripts)
        trap (Trap): A trap component
        type
    """
    pass


class Waypoint(ObjectBase):
    """Class that represents a Waypoint object

    Attributes:
        appearance
        description (LocString)
        has_map_note (bool): Has a map note
        linked_to (str): Tag of linked object
        map_note (LocString)
        map_note_enabled (bool)
    """
    @staticmethod
    def from_dict(value: dict):
        """Constructs object from python dict.
        """
        pass

    @staticmethod
    def from_file(path: str):
        """Constructs object from file.  The file can be JSON or Gff.
        """
        pass
    pass


# Resources ###################################################################
###############################################################################

class ResourceType(enum.Enum):
    invalid = auto()

    container = auto()
    gff_archive = auto()
    movie = auto()
    player = auto()
    sound = auto()
    texture = auto()
    json = auto()

    bmp = auto()
    mve = auto()
    tga = auto()
    wav = auto()
    plt = auto()
    ini = auto()
    bmu = auto()
    mpg = auto()
    txt = auto()
    plh = auto()
    tex = auto()
    mdl = auto()
    thg = auto()
    fnt = auto()
    lua = auto()
    slt = auto()
    nss = auto()
    ncs = auto()
    mod = auto()
    are = auto()
    set = auto()
    ifo = auto()
    bic = auto()
    wok = auto()
    twoda = auto()
    tlk = auto()
    txi = auto()
    git = auto()
    bti = auto()
    uti = auto()
    btc = auto()
    utc = auto()
    dlg = auto()
    itp = auto()
    btt = auto()
    utt = auto()
    dds = auto()
    bts = auto()
    uts = auto()
    ltr = auto()
    gff = auto()
    fac = auto()
    bte = auto()
    ute = auto()
    btd = auto()
    utd = auto()
    btp = auto()
    utp = auto()
    dft = auto()
    gic = auto()
    gui = auto()
    css = auto()
    ccs = auto()
    btm = auto()
    utm = auto()
    dwk = auto()
    pwk = auto()
    btg = auto()
    utg = auto()
    jrl = auto()
    sav = auto()
    utw = auto()
    fourpc = auto()
    ssf = auto()
    hak = auto()
    nwm = auto()
    bik = auto()
    ndb = auto()
    ptm = auto()
    ptt = auto()
    bak = auto()
    dat = auto()
    shd = auto()
    xbc = auto()
    wbm = auto()
    mtr = auto()
    ktx = auto()
    ttf = auto()
    sql = auto()
    tml = auto()
    sq3 = auto()
    lod = auto()
    gif = auto()
    png = auto()
    jpg = auto()
    caf = auto()
    ids = auto()
    erf = auto()
    bif = auto()
    key = auto()


class Resource:
    """Resource name

    Args:
        name (str): resref or filename
        type (ResourceType | None): (Default None)

    Notes:
        If a resource type is not passed ``name`` is assumed to be a file name,
        e.g. 'nw_chicken.utc'

    Attributes:
        resref (str)
        type (ResourceType)
    """

    def from_filename(filename: str) -> 'Resource':
        """Creates resource from file name"""
        pass

    def filename(self) -> str:
        """Returns resource as 'resref.ext'"""
        pass

    def valid(self) -> bool:
        """Determines if is valid resource name"""
        pass


def resmatch(res: Resource, pattern: str) -> bool:
    """Analog of fnmatch but for resource names

    Args:
        res (Resource): Resource name
        pattern (str): glob pattern
    """
    pass


class ResourceDescriptor:
    """Resource descriptor

    Attributes:
        name
        size
        mtime
        parent
    """
    pass


class Container:
    """Base container interface
    """

    def all(self):
        """Get all resources"""
        pass

    def contains(self, res: Resource | str) -> bool:
        """Get if container contains resource"""
        pass

    def demand(self, res: Resource | str) -> bytes:
        """Reads resource data, empty ByteArray if no match."""
        pass

    def extract_by_glob(self, glob: str, output: str) -> int:
        """Extract elements from a container by glob pattern"""
        pass

    def extract(self, pattern, output) -> int:
        """Extract elements from a container by regex"""
        pass

    def name(self) -> str:
        """Equivalent to `basename path()`"""
        pass

    def path(self) -> str:
        """Path to container, for basic containers, should be canonical"""
        pass

    def size(self) -> int:
        """Gets the number of resources, if applicable, of the container"""
        pass

    def stat(self, res) -> ResourceDescriptor:
        """Get some general data about a resource"""
        pass

    def valid(self) -> bool:
        """Return true if loaded, false if not."""
        pass

    def working_directory(self) -> str:
        """Get container working directory"""
        pass
    pass


class Directory(Container):
    """Implementation of a directory as a rollnw.Container

    Args:
        path (str): Directory to load
    """

    def __init__(self, path: str):
        pass


class Erf(Container):
    """Implementation of Erf file format

    Args:
        path (str): Erf file to load
    """

    def __init__(self, path: str):
        pass

    def add(self, path):
        """Adds resources from path"""
        pass

    def erase(self, resource):
        """Removes resource"""
        pass

    def merge(self, container):
        """Merges the contents of another rollnw.Container"""
        pass

    def reload(self):
        """Reloads Erf

        Notes:
            Erf:: working_directory() will not change
        """
        pass

    def save(self):
        """Saves Erf to Erf:: path()

        Notes:
            It's probably best to call Erf:: reload after save.
        """
        pass

    def save_as(self, path):
        """Saves Erf to different path

        Notes:
            Current Erf unmodified, to load Erf at new path a new Erf must
            be constructed.
        """
        pass


class Key(Container):
    """Implementation Key/Bif file format as a rollnw.Container

    Args:
        path (str): Path to key file
    """

    def __init__(self, path: str):
        pass


class NWSync:
    """Implementation of NWSync file format

    Args:
        path (str): Path to NWSync repository
    """

    def __init__(self, path: str):
        pass

    def get(self, manifest):
        """Gets a particular manifest as a container"""

    def is_loaded(self):
        """Gets if NWSync was successfully loaded"""
        pass

    def manifests(self):
        """Get list of all manifests"""
        pass

    def shard_count(self):
        """Get the number of shards"""
        pass


class NWSyncManifest(Container):
    """Implementation of NWSync Manifest as a rollnw.Container
    """
    pass


class Zip(Container):
    """Implementation of Zip file format as a container

    Args:
        path (str): Path to zip file
    """

    def __init__(self, path: str):
        pass

# Rules #######################################################################
###############################################################################


class AttackData:
    """Class aggregating attack data

    Attributes:
        attacker
        target

        type
        result
        target_state

        target_is_creature
        is_ranged_attack
        nth_attack
        attack_roll
        attack_bonus
        armor_class
        iteration_penalty
        multiplier
        threat_range
        concealment
    """
    pass


class AttackResult(enum.IntEnum):
    """Attack Result Type
    """
    hit_by_auto_success = auto()
    hit_by_critical = auto()
    hit_by_roll = auto()
    miss_by_auto_fail = auto()
    miss_by_concealment = auto()
    miss_by_miss_chance = auto()
    miss_by_roll = auto()


class DiceRoll:
    """Dice roll

    Attributes:
        dice (int)
        sides (int)
        bonus (int)
    """
    pass


class EffectCategory(enum.IntEnum):
    """Effect category
    """
    magical = auto()
    extraordinary = auto()
    supernatural = auto()
    item = auto()
    innate = auto()


class EffectID:
    """Effect ID

    Attributes:
        version (int)
        index (int)
    """


class EffectHandle:
    """Effect Handle

    Attributes:
        type (int)
        subtype (int)
        creator (ObjectHandle)
        spell_id (int)
        category (EffectCategory)
        effect (Effect)
    """
    pass


class Effect:
    def clear(self):
        """Clears the effect such that it's as if default constructed"""
        pass

    def get_float(self, index):
        """Gets a floating point value"""
        pass

    def get_int(self, index):
        """Gets an integer point value"""
        pass

    def get_string(self, index):
        """Gets a string value"""
        pass

    def handle(self):
        """Gets the effect's handle"""
        pass

    def id(self):
        """Gets the effect's ID"""
        pass

    def set_float(self, index, value):
        """Sets a floating point value"""
        pass

    def set_int(self, index, value):
        """Sets an integer point value"""
        pass

    def set_string(self, index, value):
        """Sets a string value"""
        pass

    def set_versus(self, vs):
        """Sets the versus value"""
        pass

    def versus(self):
        """Gets the versus value"""
        pass

# Serialization ###############################################################
###############################################################################
