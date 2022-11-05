import rollnw

rollnw.kernel.start()


def test_twoda_cache():
    p1 = rollnw.kernel.twodas().get('placeables')
    assert p1
    p2 = rollnw.kernel.twodas().get('placeables')
    assert p1 == p2


def test_parsed_script_cache():
    p1 = rollnw.kernel.parsed_scripts().get('nwscript')
    assert p1
    p2 = rollnw.kernel.parsed_scripts().get('nwscript')
    assert p1 == p2
