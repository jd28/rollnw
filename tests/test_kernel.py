import rollnw
from rollnw import kernel as nwk


def test_load_modulue():
    mod = nwk.load_module(
        "tests/test_data/user/modules/DockerDemo.mod")


def test_twoda_cache():
    mod = nwk.load_module(
        "tests/test_data/user/modules/DockerDemo.mod")
    p1 = nwk.twodas().get('placeables')
    assert p1
    p2 = nwk.twodas().get('placeables')
    assert p1 == p2


def test_object_system():
    mod = nwk.load_module(
        "tests/test_data/user/modules/DockerDemo.mod")
    cre = nwk.objects().creature("nw_chicken")
    assert cre
    cre2 = nwk.objects().get_by_tag("NW_CHICKEN")
    assert cre == cre2


def test_kernel_resources():
    mod = nwk.load_module(
        "tests/test_data/user/modules/DockerDemo.mod")
    tex1 = nwk.resman().texture("doesn'texist")
    assert tex1 is None

    tex2 = nwk.resman().texture("tno01_wtcliff01")
    assert tex2.valid()
