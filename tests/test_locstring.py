import rollnw
from rollnw import LocString, LanguageID


def test_locstring_construction():
    ls1 = LocString()
    assert ls1.strref() == -1
    assert ls1.size() == 0

    ls2 = LocString(2)
    assert ls2.strref() == 2
    assert ls2.to_dict() == {"strref": 2, "strings": []}


def test_locstring_add_remove_strings():
    ls1 = LocString()
    ls1.add(LanguageID.english, "Hello World")
    ls1.get(LanguageID.english) == "Hello World"
    ls1[LanguageID.english] == "Hello World"
    ls1.remove(LanguageID.english) == "Hello World"
    ls1.get(LanguageID.english) == ""
