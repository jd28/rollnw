import rollnw
from rollnw import Store
import json
import pytest


def test_Store_default_construct():
    obj = Store()
    assert obj.handle().id == rollnw.OBJECT_INVALID


def test_Store_gff_construct():
    obj = Store.from_file("tests/test_data/user/development/storethief002.utm")
    assert obj.common.resref == "storethief002"
