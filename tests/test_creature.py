import rollnw
from rollnw import Creature, EquipIndex
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
    cre = rollnw.Creature.from_dict(json_file)
    assert cre.stats.get_ability_score(0) == 40


def test_creature_gff_construct():
    cre = Creature.from_file("tests/test_data/user/development/nw_chicken.utc")
    assert cre.common.resref == "nw_chicken"


def test_creature_json_construct():
    cre = Creature.from_file(
        "tests/test_data/user/development/pl_agent_001.utc.json")
    assert cre.stats.get_ability_score(0) == 40


def test_creature_stats():
    cre = Creature.from_file(
        "tests/test_data/user/development/pl_agent_001.utc")

    # Abilities
    assert cre.stats.get_ability_score(0) == 40
    assert cre.stats.set_ability_score(0, 41)
    assert cre.stats.get_ability_score(0) == 41

    # Feats
    assert cre.stats.has_feat(41)
    assert not cre.stats.add_feat(41)
    assert cre.stats.add_feat(1)
    assert cre.stats.has_feat(1)

    # Saves
    assert cre.stats.save_bonus.fort == 9

    # Skills
    assert cre.stats.get_skill_rank(4) == 11
    assert cre.stats.set_skill_rank(4, 12)
    assert cre.stats.get_skill_rank(4) == 12


def test_creature_scripts():
    cre = Creature.from_file("tests/test_data/user/development/nw_chicken.utc")
    if cre.scripts.on_attacked == "nw_c2_default5":
        cre.scripts.on_attacked = "nw_shakenbake"

    assert cre.scripts.on_attacked == "nw_shakenbake"


def test_creature_inventory():
    cre = Creature.from_file(
        "tests/test_data/user/development/pl_agent_001.utc")
    assert cre.inventory.owner == cre
    assert len(cre.inventory.items) == 0


def test_creature_equips():
    cre = Creature.from_file(
        "tests/test_data/user/development/pl_agent_001.utc")
    assert cre.equipment.equips[EquipIndex.head] == ""
    assert cre.equipment.equips[EquipIndex.chest] == "dk_agent_thread2"


def test_creature_level_stats():
    cre = Creature.from_file(
        "tests/test_data/user/development/pl_agent_001.utc")

    assert cre.levels.entries[0].id == 4
    assert cre.levels.entries[0].level == 10
    assert cre.levels.entries[0].spells.get_known_spell_count(1) == 0


def test_objects_kernel_service():
    rollnw.kernel.start()
    cre = rollnw.kernel.objects().creature('nw_chicken.utc')
    assert cre
    cre2 = rollnw.kernel.objects().get(cre.handle())
    assert cre == cre2
