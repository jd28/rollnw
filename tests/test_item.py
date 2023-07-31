import rollnw
from rollnw import Item
import json
import pytest


def test_Item_default_construct():
    obj = Item()
    assert obj.handle().id == rollnw.OBJECT_INVALID


def test_Item_gff_construct():
    obj = Item.from_file("tests/test_data/user/development/wduersc004.uti")
    assert obj.common.resref == "wduersc004"
