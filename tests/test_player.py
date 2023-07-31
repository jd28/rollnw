import rollnw
from rollnw import Player


def test_creature_gff_construct():
    player = Player.from_file(
        "tests/test_data/user/servervault/CDKEY/testsorcpc1.bic")
    assert player
    assert len(player.history.entries[0].known_spells)
