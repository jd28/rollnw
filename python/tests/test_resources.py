import pynwn


def test_resource_construction():
    r = pynwn.Resource("hello", pynwn.ResourceType.twoda)
    assert r.filename() == "hello.2da"
