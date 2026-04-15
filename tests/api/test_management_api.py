import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SAMPLE = ROOT / 'tests' / 'api' / 'sample-config.json'


def test_sample_config_has_api_block():
    data = json.loads(SAMPLE.read_text())
    assert data['api']['enabled'] is True
    assert data['api']['bind'] == '127.0.0.1'
    assert data['api']['port'] == 8765


def test_sample_config_has_required_core_fields():
    data = json.loads(SAMPLE.read_text())
    assert data['ver'] >= 2
    assert isinstance(data['sources'], list) and data['sources']
    assert isinstance(data['systems'], list) and data['systems']
