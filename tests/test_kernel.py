import rollnw


def test_twoda_cache():
    p1 = rollnw.kernel.twodas().get('placeables')
    assert p1
    p2 = rollnw.kernel.twodas().get('placeables')
    assert p1 == p2


def test_object_system():
    cre = rollnw.kernel.objects().creature("nw_chicken")
    assert cre
    cre2 = rollnw.kernel.objects().get_by_tag("NW_CHICKEN")
    assert cre == cre2
