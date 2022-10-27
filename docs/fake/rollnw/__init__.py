import enum

# Config ######################################################################
###############################################################################


# Components ##################################################################
###############################################################################


class Appearance:
    """Class containing creature's appearance

    Attributes:
        body_parts (rollnw.BodyParts): body_parts
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
    """Class containing combat related data"""

    pass


class Common:
    """Class containing attributes common to all objects

    Attributes:
        resref (str): resref
        tag (str): tag
        name (rollnw.LocString): name
        locals (rollnw.LocalData): locals
        location (rollnw.Location): location
        comment (str): comment
        palette_id (int): palette_id
    """

    pass


class CreatureStats:
    pass


class Equips:
    pass


class Inventory:
    pass


class LevelStats:
    pass


class LocalData:
    def delete_float(varname: str):
        """Deletes float variable"""
        pass

    def delete_int(varname: str):
        """Deletes int variable"""
        pass

    def delete_object(varname: str):
        """Deletes object variable"""
        pass

    def delete_string(varname: str):
        """Deletes string variable"""
        pass

    def delete_location(varname: str):
        """Deletes location variable"""
        pass

    def get_float(varname: str):
        """Gets float variable"""
        pass

    def get_int(varname: str) -> int:
        """Gets int variable"""
        pass

    def get_object(varname: str):
        """Gets object variable"""
        pass

    def get_string(varname: str) -> str:
        """Gets string variable"""
        pass

    def get_location(varname: str):
        """Gets location variable"""
        pass

    def set_float(varname: str, value: float):
        """Sets float variable"""
        pass

    def set_int(varname: str, value: int):
        """Sets int variable"""
        pass

    def set_object(varname: str):
        """Sets object variable"""
        pass

    def set_string(varname: str, value: str):
        """Sets string variable"""
        pass

    def set_location(varname: str):
        """Sets location variable"""
        pass

    def size():
        """Gets number of variables"""
        pass

    pass


class Location:
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


class SpellBook:
    pass


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

    def strref():
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

    def __getitem__(self, strref: int, string: str):
        """Sets a tlk entry."""
        pass

    def __len__(self):
        """Gets the highest set TLK entry"""

    def get(self, strref: int) -> str:
        """Gets a tlk entry."""
        pass

    def get(self, strref: int):
        """Gets a localized string"""
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

# Model #######################################################################
###############################################################################


class MdlNode:
    """Base Model Node

    Attributes:
       name (str): name
       type: type
       inheritcolor (bool): inheritcolor
       parent (rollnw.MdlNode): Parent node
       children ([rollnw.MdlNode]): Children nodes
       controller_keys: controller_keys
       controller_data: controller_data
    """

    pass


class MdlGeometry:
    """Class containing model geometry

    Attributes:
       name (str): name
       type: type
       nodes ([rollnw.MdlNode]): Model nodes
    """

    pass


class MdlAnimation(MdlGeometry):
    """Class containing model animation"""

    pass


class MdlModel(MdlGeometry):
    """Class containing model

    Attributes:
        bmax (vec3): bmax
        bmin (vec3): bmin
        classification: classification
        ignorefog: ignorefog
        animations: animations
        supermodel: supermodel
        radius: radius
        animationscale: animationscale
        supermodel_name (str): supermodel_name
        file_dependency: file_dependency
    """

    pass


class Mdl:
    """Implementation of ASCII Mdl file format

    Attributes:
        model (rollnw.MdlModel): The parsed model
    """

    def valid():
        """Determines if file was successfully parsed"""
        pass

    @staticmethod
    def from_file(path):
        """Loads mdl file from file path"""
        pass

    pass


# Objects #####################################################################
###############################################################################


class ObjectBase:
    def handle(self):
        """Gets object handle"""
        pass

    pass


class Area:
    pass


class Creature(ObjectBase):
    """Class that represents a Creature object

    Attributes:
        appearance
        bodybag
        chunk_death
        combat_info
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
        stats (rollnw.CreatureStats): Offensive and defensive stats.
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
        lock
        plot
        portrait_id
        saves
        scripts
        trap
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

    pass


class Item:
    """Class that represents an Item object

    Attributes:
        additional_cost
        baseitem
        charges
        cost
        cursed
        description (rollnw.LocString): Description
        description_id (rollnw.LocString): Description after being identified.
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


class Placeable:
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
        scripts (rollnw.PlaceableScripts):
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


class Sound(ObjectBase):
    """Class that represents a Sound object

    Attributes:
        active
        common (rollnw.Common): Common object component
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
        scripts
        weapons

    //.def_readonly("will_not_buy", &nw::Store::will_not_buy)
    //.def_readonly("will_only_buy", &nw::Store::will_only_buy)
    """

    @staticmethod
    def from_dict(value: dict) -> Creature:
        """Constructs object from python dict.
        """
        pass

    @staticmethod
    def from_file(path: str):
        """Constructs object from file.  The file can be JSON or Gff.
        """
        pass

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
        scripts
        trap (rollnw.Trap): A trap component
        type
    """

    pass


class Waypoint:
    """Class that represents a Waypoint object

    Attributes:
        appearance
        description (rollnw.LocString)
        has_map_note (bool): Has a map note
        linked_to (str): Tag of linked object
        map_note (rollnw.LocString)
        map_note_enabled
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


class Resource:
    def filename(self):
        """Returns resource as 'resref.ext'"""
        pass

    pass


class Container:
    """Base container interface
    """

    def all(self):
        """Get all resources"""
        pass

    def contains(self, res: Resource):
        """Get if container contains resource"""
        pass

    def demand(self, res: Resource):
        """Reads resource data, empty ByteArray if no match."""
        pass

    def extract_by_glob(glob: str, output: str):
        """Extract elements from a container by glob pattern"""
        pass

    def extract(self, pattern, output):
        """Extract elements from a container by regex"""
        pass

    def name(self):
        """Equivalent to `basename path()`"""
        pass

    def path(self):
        """Path to container, for basic containers, should be canonical"""
        pass

    def size(self):
        """Gets the number of resources, if applicable, of the container"""
        pass

    def stat(self, res):
        """Get some general data about a resource"""
        pass

    def valid(self):
        """Return true if loaded, false if not."""
        pass

    def working_directory(self):
        """Get container working directory"""
        pass

    pass


class Directory(Container):
    """Implementation of a directory as a rollnw.Container

    Args:
        filename (str): Erf file to load
    """

    pass


class Erf(Container):
    """Implementation of Erf file format

    Args:
        filename (str): Erf file to load
    """

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

        @note Erf:: working_directory() will not change"""
        pass

    def save(self):
        """Saves Erf to Erf:: path()

        @note It's probably best to call Erf:: reload after save.
        """
        pass

    def save_as(self, path):
        """Saves Erf to different path

        @note Current Erf unmodified, to load Erf at new path a new Erf must
        be constructed."""
        pass

    pass


class Key(Container):
    """Implementation Key/Bif file format as a rollnw.Container
    """

    pass


class NWSync:
    """Implementation of NWSync file format

    Args:
        path (str): Path to NWSync repository
    """

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
        path (str): Path to NWSync repository
    """

    pass

# Rules #######################################################################
###############################################################################

# Serialization ###############################################################
###############################################################################

# Script ######################################################################
###############################################################################


class Nss:
    """Implementation of nwscript"""

    def __init__(self, path: str):
        """Constructs Nss object"""
        pass

    def errors(self):
        """Gets number of errors encountered while parsing"""
        pass

    def parse(self):
        """Parses the script"""
        pass

    def script(self):
        """Gets the parsed script"""
        pass

    def warnings(self):
        """Gets number of errors encountered while parsing"""
        pass


class Script:
    """Class containing a parsed script

    Attributes:
        defines ([str])
        includes ([str])
    """

    def __getitem__(self, index):
        """Gets a toplevel declaration"""
        pass

    def __iter__(self):
        """Gets an iterator of toplevel declarations"""
        pass

    def __len__(self):
        """Gets number of toplevel declarations"""
        pass


class NssLexer:
    """A nwscript lexer"""

    def __init__(self, script: str):
        """Constructs lexer from a string"""

    def current(self):
        """Gets next token"""

    def next(self):
        """Gets next token"""
