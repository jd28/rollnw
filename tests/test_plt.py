from rollnw import Plt
import rollnw


def test_plt_construct():
    plt = Plt("tests/test_data/user/development/pmh0_head001.plt")
    assert plt.valid()

    color = rollnw.decode_plt_color(plt, rollnw.PltColors(), 0, 0)
    color[0] == 139
    color[1] == 102
    color[2] == 81
