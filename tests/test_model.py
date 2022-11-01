import rollnw
from rollnw.model import *


def test_basic_loading():
    mdl = Mdl.from_file("tests/test_data/user/development/c_mindflayer.mdl")
    assert mdl.valid()

    i = 0
    for node in mdl.model.nodes():
        i += 1
    assert len(mdl.model) == i

    node = mdl.model[5]
    assert isinstance(node, MdlTrimeshNode)

    assert node.name == "rthigh_g"
    assert len(node.verts) == 14
    key, data = node.get_controller(MdlControllerType.position, False)
    assert key
    assert not key.is_key
    assert len(data) == 3
    assert key.name == "position"

    i = 0
    for anim in mdl.model.animations():
        i += 1

    assert mdl.model.animation_count() == i

    anim = mdl.model.get_animation(2)
    assert isinstance(anim, MdlAnimation)
    assert anim.name == "creadyr"
    assert len(anim) == 55
    anim_node = anim[5]
    assert anim_node.name == "rthigh_g"
    key, data = anim_node.get_controller(
        MdlControllerType.orientation, True)
    assert key
    assert key.is_key
    assert len(data) / key.columns == 4
    assert key.name == "orientationkey"
