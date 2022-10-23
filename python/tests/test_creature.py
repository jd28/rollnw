from itertools import count
import pynwn
import json
import pytest


@pytest.fixture
def json_file():
    with open("tests/data/pl_agent_001.utc.json") as f:
        return json.load(f)


def test_creature_default_construct():
    w = pynwn.Creature()
    assert w.handle().id == pynwn.OBJECT_INVALID


def test_creature_json(json_file):
    w = pynwn.Creature(json_file, pynwn.SerializationProfile.blueprint)
    assert w.handle().id == pynwn.OBJECT_INVALID
    assert w.stats.abilities[0] == 40
    assert sum(w.stats.abilities) == 104
