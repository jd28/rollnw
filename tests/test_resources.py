import rollnw


def test_resource_construction():
    r = rollnw.Resource("hello", rollnw.ResourceType.twoda)
    assert r.filename() == "hello.2da"
