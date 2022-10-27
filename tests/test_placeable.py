import rollnw
from rollnw import Placeable


def test_Placeable_default_construct():
    obj = Placeable()
    assert obj.handle().id == rollnw.OBJECT_INVALID


def test_Placeable_gff_construct():
    obj = Placeable().from_file("tests/test_data/user/development/arrowcorpse001.utp")
    assert obj.common.resref == "arrowcorpse001"
