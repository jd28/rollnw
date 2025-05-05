import rollnw


def test_resource_construction():
    r = rollnw.Resource("hello", rollnw.ResourceType.twoda)
    assert r.filename() == "hello.2da"


def test_erf_construction():
    erf = rollnw.Erf("tests/test_data/user/hak/hak_with_description.hak")
    assert erf.name() == "hak_with_description.hak"
    assert erf.size() == 1
    assert erf.all()[0].name.filename() == "build.txt"


def test_kernel_service():
    mod = rollnw.kernel.load_module(
        "tests/test_data/user/modules/DockerDemo.mod")

    assert rollnw.kernel.resman().contains('nw_chicken.utc')

    data = rollnw.kernel.resman().demand('nw_chicken.utc')
    assert len(data.bytes) > 0
