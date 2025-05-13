import enum
from enum import auto, IntFlag
from typing import NewType, Tuple, List, ClassVar, Optional, ByteString, DefaultDict


# Math ########################################################################
###############################################################################


class IVector4:
    x: int
    y: int
    z: int
    w: int


class Vector2:
    x: float
    y: float


class Vector3:
    x: float
    y: float
    z: float


class Vector4:
    x: float
    y: float
    z: float
    W: float

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
    def to_base_id(id: int) -> Tuple[LanguageID, bool]:
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

    def set_strref(self, strref: int):
        """Sets string reference"""
        pass

    def size(self) -> int:
        """Gets number of localized strings"""
        pass

    def strref(self) -> int:
        """Gets string reference"""
        pass

    def to_dict(self) -> DefaultDict:
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


# Platform ####################################################################
###############################################################################


class GameVersion(enum.Enum):
    """Game versions"""
    v1_69 = auto()
    vEE = auto()
    nwn2 = auto()


# Components ##################################################################
###############################################################################

class CreatureColors(enum.Enum):
    hair = auto()
    skin = auto()
    tatoo1 = auto()
    tatoo2 = auto()


class CreatureAppearance:
    """Class containing creature's appearance
    """
    #: body_parts
    body_parts: "BodyParts"
    #: Creature Body Part Colors
    colors: List[int]
    #: Index into ``appearance.2da``
    id: int
    #: phenotype
    phenotype: int
    #: Index into ``portraits.2da``
    portrait_id: int
    #: tail
    tail: int
    #: wings
    wings: int


class BodyParts:
    """Class containing references to creature's body parts
    """
    belt: int
    bicep_left: int
    bicep_right: int
    foot_left: int
    foot_right: int
    forearm_left: int
    forearm_right: int
    hand_left: int
    hand_right: int
    head: int
    neck: int
    pelvis: int
    shin_left: int
    shin_right: int
    shoulder_left: int
    shoulder_right: int
    thigh_left: int
    thigh_right: int


class CombatInfo:
    """Class containing combat related data
    """
    ac_natural_bonus: int
    ac_armor_base: int
    ac_shield_base: int
    combat_mode: int
    target_state: int
    size_ab_modifier: int
    size_ac_modifier: int


class Common:
    """Class containing attributes common to all objects
    """
    resref: str
    tag: str
    name: "LocString"
    locals: "LocalData"
    location: "Location"
    comment: str
    palette_id: int


class CreatureScripts:
    """A class containing a creature's script set.
    """
    on_attacked: str
    on_blocked: str
    on_conversation: str
    on_damaged: str
    on_death: str
    on_disturbed: str
    on_endround: str
    on_heartbeat: str
    on_perceived: str
    on_rested: str
    on_spawn: str
    on_spell_cast_at: str
    on_user_defined: str


class CreatureStats:
    """Implementation of a creature's general attributes and stats"""

    def add_feat(self, feat: int) -> bool:
        """Attempts to add a feat to a creature, returning true if successful
        """
        return True

    def get_ability_score(self, id: int):
        """Gets an ability score"""
        pass

    def get_skill_rank(self, id: int):
        """Gets a skill rank"""
        pass

    def has_feat(self, feat: int) -> bool:
        """Determines if creature has feat
        """
        pass

    def remove_feat(self, feat: int):
        """Removes a feat
        """

    def set_ability_score(self, id: int,  value: int) -> bool:
        """Sets an ability score, returning true if successful"""
        pass

    def set_skill_rank(self, id: int,  value: int) -> bool:
        """Sets a skill rank, returning true if successful"""
        pass


class DoorScripts:
    """Door's scripts
    """
    on_click: str
    on_closed: str
    on_damaged: str
    on_death: str
    on_disarm: str
    on_heartbeat: str
    on_lock: str
    on_melee_attacked: str
    on_open_failure: str
    on_open: str
    on_spell_cast_at: str
    on_trap_triggered: str
    on_unlock: str
    on_user_defined: str


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
    """

    #: Note: len(equips) == 18
    equips: "List[str | Item]"

    def instantiate(self):
        """Instantiates equipment by loading contained items from the resource manager"""
        pass


class InventoryItem:
    """An inventory item
    """
    #: Only applicable to stores
    infinite: bool
    x: int
    y: int
    item: "str | Item"


class Inventory:
    """An Object's inventory
    """
    items: List[InventoryItem]
    owner: "ObjectBase"

    def __len__(self) -> int:
        """Gets number of items in inventory"""
        pass

    def instantiate(self):
        """Instantiates inventory by loading contained items from the resource manager"""
        pass

    def add_item(self, item: "Item") -> bool:
        """Adds an item to an inventory."""
        pass

    def add_page(self) -> bool:
        """Adds a page to the inventory if it is growable (i.e. Stores)"""

    def can_add_item(self, item: "Item") -> bool:
        """Determines if an item can be added to an inventory."""
        pass

    def debug(self) -> str:
        """Creates a string debug grid of the inventory"""
        pass

    def items(self) -> List["Item"]:
        """Gets all instantiated items"""
        pass

    def remove_item(self, item: "Item") -> bool:
        """Removes an item from an inventory."""
        pass

    def size(self) -> int:
        """Gets number of items in the inventory"""
        pass


class ClassEntry:
    """Class level data
    """
    id: int
    level: int
    spells: "SpellBook"


class LevelUp:
    """Level up data
    """
    #: Class the level was taken as
    class_: int
    #: Ability score that was raised, if any.  -1 if none
    ability: int
    #: ``True`` if level is an epic level
    epic: bool
    #: Added feats
    feats: List[int]
    #: Hitpoints gained.
    hitpoints: int
    #: Level, Spell pair for gained spells
    known_spells: List[Tuple[int, int]]
    #: Roll over skill points
    skillpoints: int
    #: Skill and the amount increased
    skills: List[Tuple[int, int]]


class LevelHistory:
    """Implements a creatures levelup history
    """
    #: Entries for levels
    entries: List[LevelUp]


class LevelStats:
    """Implements a creatures level related stats
    """
    #: Entries for levels
    entries: List[ClassEntry]

    def level(self) -> int:
        """Gets total level"""
        pass

    def level_by_class(self, class_: int) -> int:
        """Gets level by class"""
        pass


class Location:
    """Class representing an objects location
    """
    area: int  # [TODO] Fix this..
    orientation: Vector3
    position: Vector3


class LocalData:
    def clear(self, varname: str, type: int):
        """Clears local variable by type"""
        pass

    def clear_all(self, type: int):
        """Clears all variables by type"""
        pass

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


class Lock:
    """Class representing a lock on an object
    """
    key_name: str
    key_required: bool
    lockable: bool
    locked: bool
    lock_dc: int
    unlock_dc: int
    remove_key: bool


class Saves:
    """An objects saves
    """
    fort: int
    reflex: int
    will: int


class SpellFlags(enum.Flag):
    none = 0x0
    readied = 0x01
    spontaneous = 0x02
    unlimited = 0x04


class SpellEntry:
    """An entry in a spellbook
    """
    spell: int
    meta: int
    flags: SpellFlags


class SpellBook:
    """Implements a spell casters spellbook"""

    def add_known_spell(self, level: int,  entry: SpellEntry):
        """Adds a known spell at level"""
        pass

    def add_memorized_spell(self, level: int, entry: SpellEntry):
        """Adds a memorized spell at level"""
        pass

    def clear_memorized_spell(self, level: int, spell: int) -> None:
        """Clears memorized spell"""
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


class SpecialAbility:
    """Special Ability
    """
    spell: int
    level: int
    flags: SpellFlags


class Trap:
    """Class representing a trap on an object
    """
    detect_dc: int
    detectable: bool
    disarm_dc: int
    disarmable: bool
    is_trapped: bool
    one_shot: bool
    type: int


# Formats #####################################################################
###############################################################################

class DialogNodeType(enum.IntEnum):
    entry = 0
    reply = 1


class DialogAnimation(enum.IntEnum):
    default = 0
    taunt = 28
    greeting = 29
    listen = 30
    worship = 33
    salute = 34
    bow = 35
    steal = 37
    talk_normal = 38
    talk_pleading = 39
    talk_forceful = 40
    talk_laugh = 41
    victory_1 = 44
    victory_2 = 45
    victory_3 = 46
    look_far = 48
    drink = 70
    read = 71
    none = 88


class DialogPtr:
    parent: "Dialog"
    type: DialogNodeType
    node: "DialogNode"
    script_appears: str

    is_start: bool
    is_link: bool
    comment: str

    def add_ptr(self, ptr: "DialogPtr",  is_link: bool = False) -> "DialogPtr":
        """Adds Dialog Pointer, if `is_link` is false no new pointer or node is created.
        if `is_link` is true a new pointer will created with the node copied from input pointer.
        """
        pass

    def add_string(self, value: str, lang: "LanguageID" = LanguageID.english, feminine: bool = False) -> "DialogPtr":
        """ Adds Dialog Pointer and Node with string value set
        """
        pass

    def add(self) -> "DialogPtr":
        """Adds empty Dialog Pointer and Node"""
        pass

    def copy(self) -> "DialogPtr":
        """Copies dialog pointer and all sub-nodes"""
        pass

    def get_condition_param(self, key: str) -> Optional[str]:
        """Gets condition parameter by key"""
        pass

    def remove_condition_param(self, key: str):
        """Removes condition parameter by key"""

    def remove_ptr(self, ptr: "DialogPtr"):
        """Removes Dialog Ptr from underlying node"""
        pass

    def set_condition_param(self, key: str, value: str):
        """Sets condition parameter, if key does not exist key and value are appended"""
        pass


class DialogNode:
    parent: "Dialog"
    type: DialogNodeType

    comment: str
    quest: str
    speaker: str
    quest_entry: int = -1
    script_action: str
    sound: str
    text: LocString
    animation: DialogAnimation = DialogAnimation.default
    delay: int = -1
    pointers: List[DialogPtr]

    def copy(self) -> "DialogNode":
        """Copies a Node"""
        pass

    def get_action_param(self, key: str) -> Optional[str]:
        """Gets action parameter if it exists"""
        pass

    def remove_action_param(self, key: str):
        """Removes action parameter by key"""

    def set_action_param(self, key: str, value: str):
        """Sets action parameter, if key does not exist key and value are appended"""


class Dialog:
    json_archive_version: ClassVar[int]
    restype: ClassVar["ObjectType"]

    script_abort: str
    script_end: str
    delay_entry: int = 0
    delay_reply: int = 0
    word_count: int = 0
    prevent_zoom: bool = False

    def __init__(self):
        pass

    def __len__(self) -> int:
        pass

    def __getitem__(self, index: int) -> DialogPtr:
        pass

    def add(self) -> DialogPtr:
        """Adds empty Dialog Pointer and Node
        """

    def add_ptr(self, ptr: DialogPtr,  is_link: bool = False) -> DialogPtr:
        """Adds Dialog Pointer, if `is_link` is false no new pointer or node is created.
        if `is_link` is true a new pointer will created with the node copied from input pointer.
        """

    def add_string(self, value: str, lang: LanguageID = LanguageID.english, feminine: bool = False) -> DialogPtr:
        """Adds Dialog Pointer and Node with string value set
        """

    def delete_ptr(self, ptr: DialogPtr):
        """Deletes a dialog pointer
        @warning ``ptr`` should be removed from / not added to a dialog prior to deletion
        """

    def remove_ptr(self, ptr:  DialogPtr):
        """Removes Dialog Ptr from underlying node
        """

    def save(self, path: str):
        """Saves a dialog to file, valid extentions are ".dlg" and ".dlg.json"
        """

    def valid(self) -> bool:
        """Checks id dialog was successfully parsed
        """

    @staticmethod
    def from_file(path: str) -> "Dialog":
        """Creates a dialog from a GFF or rollnw JSON file
        """


class Image:
    """Loads an image

    Args:
        filename (str): image file to load
    """

    def __init__(self, filename: str):
        pass

    def channels(self) -> int:
        """Gets BPP
        """
        pass

    def data(self) -> bytes:
        """Get raw data
        """
        pass

    def height(self) -> int:
        """Get height
        """
        pass

    def valid(self) -> bool:
        """Determine if successfully loaded.
        """
        pass

    def width(self) -> int:
        """Get width
        """
        pass

    def write_to(self) -> None:
        """Write Image to file
        """
        pass


class Ini:
    """Loads an ini

    Args:
        filename (str): ini file to load
    """

    def __init__(self, filename: str):
        pass

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


class PaletteNodeType(enum.IntEnum):
    branch = auto()
    category = auto()
    blueprint = auto()


class PaletteTreeNode:
    """A node in a palette"""

    def clear(self) -> None:
        """Clears node returning it to default constructed state"""

    type: PaletteNodeType
    id: int

    #: Very rarely used, controls the visibility of category nodes.
    #: 0 - Display only if not empty.
    #: 1 - Never display (in Toolset)
    #: 2 - Display in a custom blueprint palette, when creating a blueprint,
    #: and assigning a palette category
    display: int

    name: str
    strref: int

    #: Extra data dependent on palette resource type,
    #: the game appears to handle this with inheritance, but no thanks.
    #: Note that in the case of Tilesets, the node ID of the category,
    #: determines what the resref is referring to.  There are a couple
    #: special tileset nodes that will not have a resref at all: Eraser
    #: and Raise/Lower
    resref: str

    #: Only if restype == ResourceType:: utc
    cr: float
    faction: str

    children: List["PaletteTreeNode"]


class Palette:
    """Abstracts NWNW Palettes (ITP)"""

    #: Resource type of the palette, note this is only set in skeletons
    resource_type: "ResourceType"

    children: List[PaletteTreeNode]

    #: Tileset resref if restype is set
    tileset: str

    def is_skeleton(self) -> bool:
        """Determines if palette is skeleton"""

    def save(self, path: str, format: str = "json") -> None:
        """Saves palette to path in 'gff' or 'json' format"""
        pass

    def to_dict(self) -> dict:
        """Converts palette to python dict, same layout as json"""

    def valid(self) -> bool:
        """Determines if palette was loaded properly"""
        pass

    @staticmethod
    def from_dict(value: dict):
        """Constructs palette from python dict.
        """
        pass

    @staticmethod
    def from_file(path: str):
        """Constructs palette from file.  The file can be JSON or Gff.
        """
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

    Notes:
        This would be the colors that a player would select
    """
    colors: List[int]


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


def decode_plt_color(plt: Plt, colors: PltColors, x: int, y: int) -> int:
    """Decodes PLT and user selected colors to RBGA
    """
    pass


class TwoDA:
    """Implementation of 2da file format

    Args:
        filename (str): 2da file to load
    """

    def __init__(self):
        """Loads default constructed 2da"""
        pass

    def __init__(self, filename: str):
        """Loads 2da from `filename`"""
        pass

    def add_column(self, name: str) -> bool:
        """Adds a new column to the 2da, if not already extent, and initializes with ``****``"""
        pass

    def column_names(self) -> List[str]:
        """Gets column names"""
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

    def get_raw(self, row: int, column: int) -> str:
        """Gets raw 2da entry included unspecified ('****') values"""
        pass

    def set(self, row: int, column: int | str, value: int | float | str):
        """Sets a TwoDA value

        Args:
            row (int): Row number
            column (int | str): Column number or label
            value (int | float | str): New value
        """
        pass

    @staticmethod
    def from_string(string: str) -> 'TwoDA':
        pass


class StaticTwoDA:
    """Implementation of 2da file format, but faster

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

    @staticmethod
    def from_string(string: str) -> "StaticTwoDA":
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
    """
    #: index into object array
    id: int
    #: object type
    type: ObjectType


class AreaFlags(enum.Flag):
    none = 0
    interior = 0x0001
    underground = 0x0002
    natural = 0x0004


class Area:
    """Area object
    """
    json_archive_version: ClassVar[int]
    object_type: ClassVar[int]

    comments: str
    creator_id: int
    creatures: "List[Creature]"
    doors: "List[Door]"
    encounters: "List[Encounter]"
    flags: AreaFlags
    height: int
    id: int
    items: "List[Item]"
    listen_check_mod: int
    loadscreen: int
    name: LocString
    no_rest: int
    placeables: "List[Placeable]"
    pvp: int
    scripts: "AreaScripts"
    shadow_opacity: int
    skybox: int
    sounds: "List[Sound]"
    spot_check_mod: int
    stores: "List[Store]"
    tiles: "List[AreaTile]"
    tileset_resref: str
    triggers: "List[Trigger]"
    version: int
    waypoints: "List[Waypoint]"
    weather: "AreaWeather"
    width: int


class AreaScripts:
    """Area's scripts
    """
    on_enter: str
    on_exit: str
    on_heartbeat: str
    on_user_defined: str


class AreaWeather:
    """Area's weather
    """
    chance_lightning: int
    chance_rain: int
    chance_snow: int
    color_moon_ambient: int
    color_moon_diffuse: int
    color_moon_fog: int
    color_sun_ambient: int
    color_sun_diffuse: int
    color_sun_fog: int
    fog_clip_distance: int
    wind_power: int
    day_night_cycle: int
    is_night: int
    lighting_scheme: int
    fog_moon_amount: int
    moon_shadows: int
    fog_sun_amount: int
    sun_shadows: int


class Creature(ObjectBase):
    """Class that represents a Creature object
    """
    json_archive_version: ClassVar[int]
    object_type: ClassVar[int]

    appearance: CreatureAppearance
    bodybag: int
    chunk_death: int
    # [TODO] combat_info: CombatInfo
    common: Common
    #: Dialog resref
    conversation: str
    cr: float
    cr_adjust: int
    decay_time: int
    deity: str
    description: LocString
    disarmable: int
    faction_id: int
    gender: int
    good_evil: int
    hp: int
    hp_current: int
    hp_max: int
    immortal: int
    interruptable: int
    lawful_chaotic: int
    levels: LevelStats
    lootable: int
    name_first: LocString
    name_last: LocString
    pc: int
    perception_range: float
    plot: int
    race: int
    scripts: CreatureScripts
    soundset: str
    starting_package: int
    #: Offensive and defensive stats.
    stats: CreatureStats
    subrace: str
    walkrate: int

    @property
    def equipment(self) -> Equips:
        """Gets creatures equipped items
        """
        pass

    @property
    def history(self) -> LevelHistory:
        """Gets creatures level history
        """
        pass

    @property
    def inventory(self) -> Inventory:
        """Gets creatures inventory
        """
        pass

    def save(self, path: str, format: str = "json") -> None:
        """Saves object to path in 'gff' or 'json' format"""
        pass

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
    """
    json_archive_version: ClassVar[int]
    object_type: ClassVar[int]

    animation_state: DoorAnimationState
    appearance: int
    #: Door's conversation resref
    conversation: str
    description: LocString
    faction: int
    generic_type: int
    hardness: int
    hp: int
    hp_current: int
    interruptable: int
    linked_to: str
    linked_to_flags: int
    loadscreen: int
    lock: Lock
    plot: int
    portrait_id: int
    saves: Saves
    scripts: DoorScripts
    trap: Trap

    def save(self, path: str, format: str = "json") -> None:
        """Saves object to path in 'gff' or 'json' format"""
        pass

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
    """
    on_entered: str
    on_exhausted: str
    on_exit: str
    on_heartbeat: str
    on_user_defined: str


class SpawnCreature:
    """Encounter creature spawn
    """
    appearance: int
    cr: int
    resref: str
    single_spawn: bool


class SpawnPoint:
    """A spawn point
    """
    orientation: Vector3
    position: Vector3


class Encounter:
    """Class that represents an Encounter object
    """
    active: bool
    creatures: List[SpawnCreature]
    creatures_max: int
    creatures_recommended: int
    difficulty: int
    difficulty_index: int
    faction: int
    geometry: List[Vector3]
    player_only: bool
    reset: bool
    reset_time: int
    respawns: int
    scripts: EncounterScripts
    spawn_option: int
    spawn_points: List[SpawnPoint]

    def save(self, path: str, format: str = "json") -> None:
        """Saves object to path in 'gff' or 'json' format"""
        pass

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
    """
    type: int
    subtype: int
    cost_table: int
    cost_value: int
    param_table: int
    param_value: int


class Item:
    """Class that represents an Item object
    """
    additional_cost: int
    baseitem: int
    charges: int
    cost: int
    cursed: bool
    #: Description
    description: LocString
    #: Description after being identified.
    description_id: LocString
    identified: bool
    inventory: Inventory
    model_colors: List[int]
    model_parts: List[int]
    model_type: ItemModelType
    #: Is a plot item.
    plot: bool
    properties: List[ItemProperty]
    stacksize: int
    stolen: bool

    def model_to_plt_colors(self) -> PltColors:
        """Converts model colors to PLT colors"""
        pass

    def get_icon_by_part(self, part: ItemModelParts = ItemModelParts.model1, female: bool = False) -> Image:
        """Generates image of icon by model part, constructing from PLT textures as necessary.

        Note: Python owns the lifetime of the resulting image. Merging icons is also the responsibility of the caller.
        """

    def save(self, path: str, format: str = "json") -> None:
        """Saves object to path in 'gff' or 'json' format"""
        pass

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


class Module:
    """Class that represents a Module object
    """
    creator: int
    dawn_hour: int
    description: LocString
    dusk_hour: int
    entry_area: str
    entry_orientation: Vector3
    entry_position: Vector3
    expansion_pack: int
    haks: List[str]
    id: ByteString
    is_save_game: bool
    locals: LocalData
    min_game_version: int
    minutes_per_hour: int
    name: LocString
    scripts: "ModuleScripts"
    start_day: int
    start_hour: int
    start_month: int
    start_movie: str
    start_year: int
    tag: str
    tlk: str
    version: int
    xpscale: int

    def __iter__(self):
        """Get iterator of areas in the module"""
        pass

    def __len__(self):
        """Get the number of areas in the module"""
        pass

    def area_count(self) -> int:
        """Gets number of areas in module"""
        pass

    def get_area(self, index: int) -> Optional[Area]:
        """Gets number of areas in module"""
        pass

    @property
    def uuid(self) -> str:
        """Gets modules UUID"""
        return ""


class ModuleScripts:
    """Module Scripts
    """
    on_client_enter: str
    on_client_leave: str
    on_cutsnabort: str
    on_heartbeat: str
    on_item_acquire: str
    on_item_activate: str
    on_item_unaquire: str
    on_load: str
    on_player_chat: str
    on_player_death: str
    on_player_dying: str
    on_player_equip: str
    on_player_level_up: str
    on_player_rest: str
    on_player_uneqiup: str
    on_spawnbtndn: str
    on_start: str
    on_user_defined: str


class PlaceableAnimationState(enum.Enum):
    none = auto()
    open = auto()
    closed = auto()
    destroyed = auto()
    activated = auto()
    deactivated = auto()


class PlaceableScripts:
    """Placeable's scripts
    """
    on_click: str
    on_closed: str
    on_damaged: str
    on_death: str
    on_disarm: str
    on_heartbeat: str
    on_inventory_disturbed: str
    on_lock: str
    on_melee_attacked: str
    on_open: str
    on_spell_cast_at: str
    on_trap_triggered: str
    on_unlock: str
    on_used: str
    on_user_defined: str


class Placeable(ObjectBase):
    """Class that represents a Placeable object
    """
    json_archive_version: ClassVar[int]
    object_type: ClassVar[int]

    animation_state: PlaceableAnimationState
    appearance: int
    bodybag: int
    common: Common
    conversation: str
    description: LocString
    faction: int
    hardness: int
    has_inventory: bool
    hp: int
    hp_current: int
    interruptable: bool
    inventory: Inventory
    lock: Lock
    plot: bool
    portrait_id: int
    saves: Saves
    scripts: PlaceableScripts
    static: bool
    trap: Trap
    useable: bool

    def save(self, path: str, format: str = "json") -> None:
        """Saves object to path in 'gff' or 'json' format"""
        pass

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

    def save(self, path: str, format: str = "json") -> None:
        """Saves object to path in 'gff' or 'json' format"""
        pass


class Sound(ObjectBase):
    """Class that represents a Sound object
    """
    json_archive_version: ClassVar[int]
    object_type: ClassVar[int]

    active: bool
    common: Common
    continuous: bool
    distance_max: float
    distance_min: float
    elevation: float
    generated_type: int
    hours: int
    interval: int
    interval_variation: int
    looping: bool
    pitch_variation: float
    positional: bool
    priority: int
    random: bool
    random_position: bool
    random_x: float
    random_y: float
    sounds: List[str]
    times: int
    volume: int
    volume_variation: int

    def save(self, path: str, format: str = "json") -> None:
        """Saves object to path in 'gff' or 'json' format"""
        pass

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
    """
    on_closed: str
    on_opened: str


class Store(ObjectBase):
    """Class that represents a Store object
    """
    json_archive_version: ClassVar[int]
    object_type: ClassVar[int]

    armor: Inventory
    blackmarket: bool
    blackmarket_markdown: int
    gold: int
    identify_price: int
    markdown: int
    markup: int
    max_price: int
    miscellaneous: Inventory
    potions: Inventory
    rings: Inventory
    scripts: StoreScripts
    weapons: Inventory

    def save(self, path: str, format: str = "json") -> None:
        """Saves object to path in 'gff' or 'json' format"""
        pass

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


class AreaTile:
    """Area tile
    """
    id: int
    height: int
    orientation: int
    animloop1: int
    animloop2: int
    animloop3: int
    mainlight1: int
    mainlight2: int
    srclight1: int
    srclight2: int


class TriggerScripts:
    """A trigger's scripts
    """
    on_click: str
    on_disarm: str
    on_enter: str
    on_exit: str
    on_heartbeat: str
    on_trap_triggered: str
    on_user_defined: str


class Trigger(ObjectBase):
    """Class that represents a Trigger object
    """
    cursor: int
    faction: int
    geometry: List[Vector3]
    highlight_height: float
    linked_to: str
    linked_to_flags: int
    loadscreen: int
    portrait: int
    scripts: TriggerScripts
    trap: Trap
    type: int

    def save(self, path: str, format: str = "json") -> None:
        """Saves object to path in 'gff' or 'json' format"""
        pass

    @staticmethod
    def from_dict(value: dict) -> 'Trigger':
        """Constructs object from python dict.
        """
        pass

    @staticmethod
    def from_file(path: str) -> 'Trigger':
        """Constructs object from file.  The file can be JSON or Gff.
        """
        pass


class Waypoint(ObjectBase):
    """Class that represents a Waypoint object
    """
    appearance: int
    description: LocString
    #: Has a map note
    has_map_note: bool
    #: Tag of linked object
    linked_to: str
    map_note: LocString
    map_note_enabled: bool

    def save(self, path: str, format: str = "json") -> None:
        """Saves object to path in 'gff' or 'json' format"""
        pass

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

class ResourceData:
    """Resource data

    Attributes:
        name (str)
        bytes (bytes)
    """


class Erf:
    """Implementation of Erf file format

    Args:
        path (str): Erf file to load
    """

    def __init__(self, path: str):
        pass

    def add(self, path):
        """Adds resources from path"""
        pass

    def extract_by_glob(self, glob: str, output: str) -> int:
        """Extract elements from a container by glob pattern"""
        pass

    def extract(self, pattern, output) -> int:
        """Extract elements from a container by regex"""
        pass

    def erase(self, resource):
        """Removes resource"""
        pass

    def merge(self, container: "Erf"):
        """Merges the contents of another rollnw.Erf"""
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

    def working_directory(self) -> str:
        """Get container working directory"""
        pass


# Rules #######################################################################
###############################################################################


class AttackData:
    """Class aggregating attack data

    Attributes:
    """
    attacker: Creature
    target: ObjectBase

    type: int
    result: "AttackResult"
    target_state: int

    target_is_creature: bool
    is_ranged_attack: bool
    nth_attack: int
    attack_roll: int
    attack_bonus: int
    armor_class: int
    iteration_penalty: int
    multiplier: int
    threat_range: int
    concealment: int


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


class DamageCategory(IntFlag):
    """Represents categories of damage with bitwise support."""
    none = 0
    penalty = 1 << 0
    critical = 1 << 1
    unblockable = 1 << 2


class DiceRoll:
    """Dice roll
    """
    dice: int
    sides: int
    bonus: int


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
    """
    version: int
    index: int


class EffectHandle:
    """Effect Handle
    """
    type: int
    subtype: int
    creator: ObjectHandle
    spell_id: int
    category: EffectCategory
    effect: "Effect"


class Effect:
    def clear(self):
        """Clears the effect such that it's as if default constructed"""
        pass

    def get_float(self, index: int) -> float:
        """Gets a floating point value"""
        pass

    def get_int(self, index: int) -> int:
        """Gets an integer point value"""
        pass

    def get_string(self, index: int) -> str:
        """Gets a string value"""
        pass

    def handle(self) -> EffectHandle:
        """Gets the effect's handle"""
        pass

    def id(self) -> EffectID:
        """Gets the effect's ID"""
        pass

    def set_float(self, index: int, value: float) -> None:
        """Sets a floating point value"""
        pass

    def set_int(self, index: int, value: int) -> None:
        """Sets an integer point value"""
        pass

    def set_string(self, index: int, value: str) -> None:
        """Sets a string value"""
        pass

    def set_versus(self, vs) -> None:
        """Sets the versus value"""
        pass

    def versus(self):
        """Gets the versus value"""
        pass

# Serialization ###############################################################
###############################################################################
