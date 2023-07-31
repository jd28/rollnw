import rollnw
from rollnw.model import *


def test_basic_ascii_loading():
    mdl = Mdl.from_file("tests/test_data/user/development/c_mindflayer.mdl")
    assert mdl.valid()

    i = 0
    for node in mdl.model.nodes():
        i += 1
    assert len(mdl.model) == i

    assert len(mdl.model[0].children) == 6

    node = mdl.model[5]
    assert isinstance(node, MdlTrimeshNode)

    assert node.name == "rthigh_g"
    assert len(node.vertices) == 14
    key, time, data = node.get_controller(MdlControllerType.position, False)
    assert key
    assert not key.is_key
    assert len(time) == 0
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
    key, time, data = anim_node.get_controller(
        MdlControllerType.orientation, True)
    assert key
    assert key.is_key
    assert len(data) / key.columns == 4
    assert key.name == "orientationkey"


def test_basic_binary_loading():
    mdl = Mdl.from_file("tests/test_data/user/development/c_bodak.mdl")
    assert mdl.valid()

    i = 0
    for node in mdl.model.nodes():
        i += 1
    assert len(mdl.model) == i

    assert mdl.model[0].name == "c_bodak"
    i = 0
    for node in mdl.model:
        i += len(node.children)

    assert i == 21

    node_set = set([n.name for n in mdl.model.supermodel.model])
    node_set2 = set(
        [n.name for n in mdl.model.supermodel.model.get_animation(0)])

    assert node_set-node_set2 == set(['handconjure', 'headconjure'])
