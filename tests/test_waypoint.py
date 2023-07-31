import rollnw
import json
import pytest


@pytest.fixture
def json_file():
    with open('tests/test_data/user/development/wp_behexit001.utw.json') as f:
        return json.load(f)


def test_waypoint_default_construct():
    w = rollnw.Waypoint()
    assert w.handle().id == rollnw.OBJECT_INVALID


def test_waypoint_dict_construct(json_file):
    w = rollnw.Waypoint.from_dict(json_file)
    assert not w.has_map_note
    assert str(w.common.resref) == 'wp_behexit001'


def test_waypoint_gff_construct():
    w = rollnw.Waypoint.from_file(
        "tests/test_data/user/development/wp_behexit001.utw")
    assert not w.has_map_note
    assert str(w.common.resref) == 'wp_behexit001'


def test_waypoint_json_construct():
    w = rollnw.Waypoint.from_file(
        "tests/test_data/user/development/wp_behexit001.utw.json")
    assert not w.has_map_note
    assert str(w.common.resref) == 'wp_behexit001'
