import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SAMPLE = ROOT / 'tests' / 'api' / 'sample-config.json'


def load_sample():
    return json.loads(SAMPLE.read_text())


def test_sample_config_has_api_block():
    data = load_sample()
    assert data['api']['enabled'] is True
    assert data['api']['bind'] == '127.0.0.1'
    assert data['api']['port'] == 8765


def test_sample_config_has_required_core_fields():
    data = load_sample()
    assert data['ver'] >= 2
    assert isinstance(data['sources'], list) and data['sources']
    assert isinstance(data['systems'], list) and data['systems']


def test_sample_config_helper_files_are_referenced():
    data = load_sample()
    system = data['systems'][0]
    assert system['talkgroupsFile'] == 'talkgroups.csv'
    assert system['unitTagsFile'] == 'unit-tags.csv'


def test_sample_config_source_driver_is_supported():
    data = load_sample()
    assert data['sources'][0]['driver'] in {'osmosdr', 'usrp', 'iqfile', 'sigmf', 'sigmffile'}
