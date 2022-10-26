import rollnw
from rollnw import Door
import json
import pytest


def test_door_default_construct():
    obj = Door()
    assert obj.handle().id == rollnw.OBJECT_INVALID


def test_door_gff_construct():
    obj = Door.from_file("tests/test_data/user/development/door_ttr_002.utd")
    assert obj.common.resref == "door_ttr_002"
