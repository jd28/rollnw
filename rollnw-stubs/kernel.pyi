from typing import Any, Tuple, Optional, overload, List

import rollnw
from rollnw import (
    Creature,
    Effect,
    Module,
    ObjectBase,
    ResourceData,
    ResourceType,
    Image,
)


class ConfigOptions:
    include_install: bool
    include_nwsync: bool
    include_user: bool


class Config:
    def initialize(self, options: ConfigOptions): ...
    def install_path(self) -> str: ...
    def options(self) -> ConfigOptions: ...
    def set_paths(self, install: str, usr: str): ...
    def set_version(self, version: rollnw.GameVersion): ...
    def user_path(self) -> str: ...
    def version(self) -> rollnw.GameVersion: ...


class EffectSystem:
    def __init__(self, *args, **kwargs): ...
    def add_effect(self, arg0: int, arg1, arg2) -> bool: ...
    def add_itemprop(self, arg0, arg1) -> bool: ...
    def apply(self, arg0: ObjectBase, arg1: Effect) -> bool: ...
    def create(self, arg0: int) -> Effect: ...
    def destroy(self, arg0: Effect): ...
    def effect_limits_ability(self) -> Tuple[int, int]: ...
    def effect_limits_armor_class(self) -> Tuple[int, int]: ...
    def effect_limits_attack(self) -> Tuple[int, int]: ...
    def effect_limits_skill(self) -> Tuple[int, int]: ...
    def ip_cost_table(self, arg0: int) -> rollnw.TwoDA: ...
    def ip_definition(self, *args, **kwargs) -> Any: ...
    def ip_param_table(self, arg0: int) -> rollnw.TwoDA: ...
    def remove(self, object: ObjectBase, effect: Effect) -> bool: ...
    def stats(self) -> EffectSystemStats: ...


class EffectSystemStats:
    def __init__(self, *args, **kwargs): ...
    @property
    def free_list_size(self) -> int: ...
    @property
    def pool_size(self) -> int: ...


class Objects:
    def __init__(self, *args, **kwargs): ...
    def area(self, resref: str) -> rollnw.Area: ...
    def creature(self, resref: str) -> Creature: ...
    def destroy(self, arg0: rollnw.ObjectHandle): ...
    def door(self, resref: str) -> rollnw.Door: ...
    def encounter(self, resref: str) -> rollnw.Encounter: ...
    def get(self, handle: rollnw.ObjectHandle) -> ObjectBase: ...
    def get_by_tag(self, tag: str, nth: int = 0) -> Optional[ObjectBase]: ...
    def item(self, resref: str) -> rollnw.Item: ...
    def placeable(self, resref: str) -> rollnw.Placeable: ...
    def store(self, resref: str) -> rollnw.Store: ...
    def trigger(self, resref: str) -> rollnw.Trigger: ...
    def valid(self, arg0: rollnw.ObjectHandle) -> bool: ...
    def waypoint(self, resref: str) -> rollnw.Waypoint: ...


class Resources(rollnw.Container):
    def __init__(self, parent: Optional[rollnw.kernel.Resources]): ...

    def demand_any(self, resref: str,
                   restypes) -> ResourceData: ...

    def demand_in_order(self, resref: str,
                        restypes: List[ResourceType]) -> ResourceData: ...

    def demand_server_vault(self, cdkey: str, resref: str) -> ResourceData: ...

    def texture(self, resref: str): ...


class Rules:
    def __init__(self, *args, **kwargs): ...


class Strings:
    def __init__(self, *args, **kwargs): ...


class TwoDACache:
    def __init__(self, *args, **kwargs): ...
    @ overload
    def get(self, arg0: str) -> rollnw.TwoDA: ...
    @ overload
    def get(self, arg0: rollnw.Resource) -> rollnw.TwoDA: ...


def config() -> Config: ...
def effects() -> EffectSystem: ...
def load_module(path: str, instantiate: Optional[bool] = True) -> Module: ...


def objects() -> Objects: ...
def resman() -> Resources: ...
def rules() -> Rules: ...
def start(options: Optional[ConfigOptions] = None): ...
def strings() -> Strings: ...
def twodas() -> TwoDACache: ...
def unload_module(): ...
