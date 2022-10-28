from re import I
import rollnw
from rollnw import Mdl


def test_basic_loading():
    mdl = Mdl.from_file("tests/test_data/user/development/c_mindflayer.mdl")
    assert mdl.valid()

    i = 0
    for node in mdl.model.nodes():
        i += 1
    assert len(mdl.model) == i

    i = 0
    for anim in mdl.model.animations():
        i += 1

    assert mdl.model.animation_count() == i

    anim = mdl.model.get_animation(2)
    assert len(anim) == 55

    node = mdl.model[5]
    assert isinstance(node, rollnw.MdlTrimeshNode)

    assert node.name == "rthigh_g"
    assert len(node.verts) == 14
    key, data = node.get_controller(rollnw.MdlControllerType.position)
    assert key
    assert len(data) == 3
