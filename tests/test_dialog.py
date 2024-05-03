import rollnw
from rollnw import Dialog
import json
import pytest


def test_dialog_gff_construct():
    dlg = Dialog.from_file("tests/test_data/user/development/alue_ranger.dlg")
    assert len(dlg) == 2
    ptr = dlg.add_string("Hello there")
    assert len(dlg) == 3
