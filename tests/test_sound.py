import rollnw
from rollnw import Sound
import json
import pytest


def test_Placeable_default_construct():
    obj = Sound()
    assert obj.handle().id == rollnw.OBJECT_INVALID


def test_Placeable_gff_construct():
    obj = Sound().from_file("tests/test_data/user/development/blue_bell.uts")
    assert obj.common.resref == "blue_bell"
