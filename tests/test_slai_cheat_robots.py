"""SLAI starter loadouts extend the existing cheat without replacing base records."""

import copy
from phantomcrash_research.slai.cheat_robots import add_cheat_robots


def fixture():
    garage = {'robots': [{'name': 'Original', 'parts': {'body': {'id': 0, 'level': 0},
                         'chip': {'id': 95, 'level': 0}}, 'paint': 0, 'emblem': 205}]}
    cheats = {'hangar_robots': [0], 'stock_size': 1024}
    registered = [{'model': 'S1B01H', 'category': 'body', 'part': 1},
                  {'model': 'S1L02H', 'category': 'leg', 'part': 2},
                  {'model': 'S1L01H', 'category': 'leg', 'part': 1},
                  {'model': 'S1A02H', 'category': 'arm', 'part': 2},
                  {'model': 'S1A01H', 'category': 'arm', 'part': 1},
                  {'model': 'S1S01H', 'category': 'shoulder', 'part': 1},
                  {'model': 'S9B01H', 'category': 'body', 'part': 2},
                  {'model': 'S9L01H', 'category': 'leg', 'part': 3},
                  {'model': 'S9A01H', 'category': 'arm', 'part': 3},
                  {'model': 'SAB01H', 'category': 'body', 'part': 3},
                  {'model': 'SAL01H', 'category': 'leg', 'part': 4},
                  {'model': 'SAS01H', 'category': 'shoulder', 'part': 2},
                  {'model': 'SAS02H', 'category': 'shoulder', 'part': 3}]
    parts = {'bodies': [{'name': 'Original'}, {'name': 'S.L.A.I. PROTON Body'},
                       {'name': 'S.L.A.I. DUSTER Body'}, {'name': 'S.L.A.I. SWEEPER Body'}]}
    return garage, cheats, registered, parts


def test_base_robots_first_with_matching_slai_parts_and_a_working_chip():
    inputs = fixture()
    original = copy.deepcopy(inputs)
    garage, cheats, indices = add_cheat_robots(*inputs)
    assert inputs == original
    assert garage['robots'][0] == original[0]['robots'][0]
    assert cheats['hangar_robots'] == [0, 1, 2, 3]
    assert indices == [1, 2, 3]
    proton = garage['robots'][1]
    assert proton['parts']['legs']['id'] == 1
    assert proton['parts']['right_arm']['id'] == 1
    assert proton['parts']['left_arm']['id'] == 2
    assert proton['parts']['chip'] == {'id': 95, 'level': 0}
    assert 'right_shoulder' in proton['parts']
    assert 'left_shoulder' not in proton['parts']
    assert 'left_arm' not in garage['robots'][2]['parts']
    assert 'right_shoulder' not in garage['robots'][2]['parts']
    assert 'right_arm' not in garage['robots'][3]['parts']
    assert 'left_shoulder' in garage['robots'][3]['parts']


def test_repeated_registration_updates_the_existing_records_instead_of_duplicating():
    garage, cheats, registered, parts = fixture()
    garage, cheats, first = add_cheat_robots(garage, cheats, registered, parts)
    parts['bodies'][1]['name'] = 'S.L.A.I. Revised Body'
    again, cheats, second = add_cheat_robots(garage, cheats, registered, parts)
    assert first == second
    assert len(again['robots']) == 4
    assert again['robots'][1]['name'] == 'SLAI Revised'
    assert cheats['hangar_robots'] == [0, 1, 2, 3]
