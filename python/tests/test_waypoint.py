import pynwn
import json
import pytest


@pytest.fixture
def json_file():
    with open('tests/data/wp_behexit001.utw.json') as f:
        return json.load(f)


def test_waypoint_default_construct():
    w = pynwn.Waypoint()
    assert w.handle().id == pynwn.OBJECT_INVALID


def test_waypoint_json_construct(json_file):
    w = pynwn.Waypoint(json_file, pynwn.SerializationProfile.blueprint)
    assert not w.has_map_note
    assert w.common().resref.string() == 'wp_behexit001'
