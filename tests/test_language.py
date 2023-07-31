import rollnw
from rollnw import Language, LanguageID


def test_lanuage():
    assert Language.encoding(LanguageID.english) == "CP1252"
    assert Language.has_feminine(LanguageID.french)
    assert Language.from_string("PL") == LanguageID.polish
    assert Language.to_runtime_id(LanguageID.english, True) == 1
    lang, fem = Language.to_base_id(1)
    assert lang == LanguageID.english and fem
