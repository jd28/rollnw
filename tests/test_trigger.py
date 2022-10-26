import rollnw
from rollnw import Trigger
import json
import pytest


def test_Trigger_default_construct():
    obj = Trigger()
    assert obj.handle().id == rollnw.OBJECT_INVALID


def test_Trigger_gff_construct():
    obj = Trigger.from_file(
        "tests/test_data/user/development/newtransition001.utt")
    assert obj.common.resref == "newtransition001"
