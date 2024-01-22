import rollnw


def test_twoda_cache():
    p1 = rollnw.kernel.twodas().get('placeables')
    assert p1
    p2 = rollnw.kernel.twodas().get('placeables')
    assert p1 == p2
