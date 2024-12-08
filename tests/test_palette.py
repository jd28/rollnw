from rollnw import Palette
import rollnw


def test_palette_skel_construct_gff():
    p1 = Palette.from_file("tests/test_data/user/scratch/creaturepal.itp")
    assert p1.valid()
    assert p1.is_skeleton()
    assert len(p1.children) == 7
    assert p1.resource_type == rollnw.ResourceType.utc


def test_palette_skel_construct_json():
    p1 = Palette.from_file("tests/test_data/user/scratch/creaturepal.itp.json")
    assert p1.valid()
    assert p1.is_skeleton()
    assert len(p1.children) == 7
    assert p1.resource_type == rollnw.ResourceType.utc


def test_palette_construct_gff():
    p1 = Palette.from_file("tests/test_data/user/scratch/creaturepalcus.itp")
    assert p1.valid()
    assert not p1.is_skeleton()
    assert len(p1.children) == 4


def test_palette_construct_json():
    p1 = Palette.from_file(
        "tests/test_data/user/scratch/creaturepalstd.itp.json")
    assert p1.valid()
    assert not p1.is_skeleton()
    assert len(p1.children) == 3
