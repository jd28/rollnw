import rollnw
from rollnw import Creature, EquipIndex
import json
import pytest


@pytest.fixture
def json_file():
    with open("tests/test_data/user/development/drorry.utc.json") as f:
        return json.load(f)


def test_creature_default_construct():
    w = rollnw.Creature()
    assert w.handle().id == rollnw.OBJECT_INVALID
    assert w.json_archive_version == 1


def test_creature_dict_construct(json_file):
    cre = rollnw.Creature.from_dict(json_file)
    assert cre.stats.get_ability_score(0) == 18


def test_creature_gff_construct():
    cre = Creature.from_file("tests/test_data/user/development/nw_chicken.utc")
    assert cre.common.resref == "nw_chicken"


def test_creature_json_construct():
    cre = Creature.from_file(
        "tests/test_data/user/development/drorry2.utc")
    assert cre.stats.get_ability_score(0) == 18


def test_creature_stats():
    cre = Creature.from_file("tests/test_data/user/development/drorry.utc")

    # Abilities
    assert cre.stats.get_ability_score(0) == 18
    assert cre.stats.set_ability_score(0, 41)
    assert cre.stats.get_ability_score(0) == 41

    # Feats
    assert cre.stats.has_feat(105)
    assert not cre.stats.add_feat(105)
    assert cre.stats.add_feat(1)
    assert cre.stats.has_feat(1)

    # Saves
    assert cre.stats.save_bonus.fort == 0

    # Skills
    assert cre.stats.get_skill_rank(4) == 1
    assert cre.stats.set_skill_rank(4, 12)
    assert cre.stats.get_skill_rank(4) == 12


def test_creature_scripts():
    cre = Creature.from_file("tests/test_data/user/development/nw_chicken.utc")
    if cre.scripts.on_attacked == "nw_c2_default5":
        cre.scripts.on_attacked = "nw_shakenbake"

    assert cre.scripts.on_attacked == "nw_shakenbake"


def test_creature_inventory():
    cre = Creature.from_file(
        "tests/test_data/user/development/drorry.utc")
    assert cre.inventory.owner == cre
    assert len(cre.inventory) == 5


def test_creature_equips():
    cre = Creature.from_file(
        "tests/test_data/user/development/drorry.utc")
    assert rollnw.nwn1.get_equipped_item(cre, EquipIndex.head) is None
    assert rollnw.nwn1.get_equipped_item(
        cre, EquipIndex.chest).common.resref == "nw_aarcl002"


def test_creature_level_stats():
    cre = Creature.from_file(
        "tests/test_data/user/development/drorry.utc")

    assert cre.levels.entries[0].id == 4
    assert cre.levels.entries[0].level == 10
    assert cre.levels.entries[0].spells.get_known_spell_count(1) == 0


def test_creature_casting():
    cre = Creature.from_file(
        "tests/test_data/user/development/wizard_pm.utc")

    assert rollnw.nwn1.get_caster_level(cre, 10) == 18  # Wizard


def test_objects_kernel_service():
    mod = rollnw.kernel.load_module(
        "tests/test_data/user/modules/DockerDemo.mod")

    cre = rollnw.kernel.objects().creature('nw_chicken')
    assert cre
    cre2 = rollnw.kernel.objects().get(cre.handle())
    assert cre == cre2
