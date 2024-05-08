import rollnw
from rollnw import Dialog, LanguageID
import json
import pytest


def test_dialog_gff_construct():
    dlg = Dialog.from_file("tests/test_data/user/development/alue_ranger.dlg")
    assert len(dlg) == 2
    ptr = dlg.add_string("Hello there")
    assert len(dlg) == 3
    assert dlg[2].node.text.get(LanguageID.english) == "Hello there"
    dlg[2].node.text.add(LanguageID.english, "Goodbye there")
    assert dlg[2].node.text.get(LanguageID.english) == "Goodbye there"
    dlg.save("bin/alue_ranger.dlg")
    dlg.save("bin/alue_ranger.dlg.json")
