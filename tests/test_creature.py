import rollnw
from rollnw import Creature
import json
import pytest


@pytest.fixture
def json_file():
    with open("tests/test_data/user/development/pl_agent_001.utc.json") as f:
        return json.load(f)


def test_creature_default_construct():
    w = rollnw.Creature()
    assert w.handle().id == rollnw.OBJECT_INVALID


def test_creature_dict_construct(json_file):
    w = rollnw.Creature.from_dict(json_file)
    assert w.stats.abilities[0] == 40
    assert sum(w.stats.abilities) == 104


def test_creature_gff_construct():
    cre = Creature.from_file("tests/test_data/user/development/nw_chicken.utc")
    if cre.scripts.on_attacked == "nw_c2_default5":
        cre.scripts.on_attacked = "nw_shakenbake"

    assert cre.scripts.on_attacked == "nw_shakenbake"


def test_creature_json_construct():
    cre = Creature.from_file(
        "tests/test_data/user/development/pl_agent_001.utc.json")
    assert cre.stats.abilities[0] == 40
    assert sum(cre.stats.abilities) == 104
