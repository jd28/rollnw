from .. import GameVersion, Module, PathAlias, Ini, Container, TwoDA, Resource
from .. import ObjectHandle, Area, Creature, Door, Encounter, Placeable, Store, Trigger, Waypoint

# Classes #####################################################################
###############################################################################


class ConfigOptions:
    """Configuration options

    Args:
        probe (bool): If true, probe for an NWN install.  (Default True)
        version (GameVersion): NWN version install to probe for.  (Default GameVersion.vEE)

    Attributes:
        version (GameVersion): Game version
        install (str): NWN root install directory
        user (str): NWN user directory
        include_install (bool): If true, load base game data.  (Default True)
        include_nwsync (bool): If true, load NWSync data.  (Default True)
    """
    def __init__(probe: bool = True, version: GameVersion = GameVersion.vEE):
        pass


class Config:
    """Configuration service
    """

    def alias_path(self, alias: PathAlias) -> str:
        """Gets alias path"""
        pass

    def nwn_ini(self) -> Ini:
        """Gets parsed nwn.ini
        """
        pass

    def nwnplayer_ini(self) -> Ini:
        """Gets parsed nwnplayer.ini
        """
        pass

    def options(self) -> ConfigOptions:
        """Gets config options
        """

    def resolve_alias(self, alias_path: str) -> str:
        """Resolves alias path"""
        pass

    def userpatch_ini(self) -> Ini:
        """Gets parsed userpatch.ini
        """
        pass


class Objects:
    """The object system creates, serializes, and deserializes entities
    """

    def area(self, resref: str) -> Area:
        pass

    def creature(self, resref: str) -> Creature:
        pass

    def destroy(self, obj: ObjectHandle) -> None:
        """Destroys an object and removes it from object system"""
        pass

    def door(self, resref: str) -> Door:
        pass

    def encounter(self, resref: str) -> Encounter:
        pass

    def get(self, handle: ObjectHandle):
        """Gets an object by its handle"""
        pass

    def placeable(self, resref: str) -> Placeable:
        pass

    def store(self, resref: str) -> Store:
        pass

    def trigger(self, resref: str) -> Trigger:
        pass

    def valid(self, handle: ObjectHandle) -> bool:
        """Checks if an object handle is still valid"""
        pass

    def waypoint(self, resref: str) -> Waypoint:
        pass


class Resources(Container):
    """Resources service
    """
    pass


class Rules:
    """Rules service
    """
    pass


class ScriptCache:
    """Script Cache service
    """
    pass


class Strings:
    """Strings service
    """
    pass


class TwoDACache:
    """2da cache
    """

    def get(self, name: str | Resource) -> TwoDA | None:
        """Gets a cached twoda"""
        pass

# Functions ###################################################################
###############################################################################


def config():
    """Gets config service"""
    pass


def load_module(mod: str, manifest: str) -> Module:
    """Loads a module

    Args:
        mod
        manifest (str): NWSynch manifest hash
    """
    pass


def objects():
    """Gets objects service"""
    pass


def resman():
    """Gets resman service"""
    pass


def rules():
    """Gets rules service"""
    pass


def start(config=None):
    """Starts kernel services

    Args:
        config (rollnw.ConfigOptions | None): Optionally pass in configuration.  Default behavior
            is to search for whatever NWN(:EE) install that it can find
    """
    pass


def strings():
    """Gets strings service"""
    pass


def unload_module() -> None:
    """Unloads the currently loaded module
    """
    pass
