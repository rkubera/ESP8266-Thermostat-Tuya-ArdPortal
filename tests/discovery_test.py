"""Validate the actual ArdPortal schema, dependency cascade and control profile."""
import json
import re
from pathlib import Path
root = Path(__file__).resolve().parents[1]
source = (root / 'portal_pages.h').read_text()
pages = [json.loads(s) for s in re.findall(r'R"JSON\((.*?)\)JSON"', source, re.S)]
assert [p['id'] for p in pages] == ['settings', 'schedule', 'clock']
fields = {}
for page in pages:
    rules = [page['visibleWhen']] if 'visibleWhen' in page else []
    for field in page['fields']:
        assert field['id'] not in fields
        if 'visibleWhen' in field:
            rules.append(field['visibleWhen'])
        for rule in rules:
            assert rule['field'] in fields, rule
        rules = [page['visibleWhen']] if 'visibleWhen' in page else []
        fields[field['id']] = field
assert fields['climate']['type'] == 'climate'
assert fields['climate']['modes'] == ['off', 'heat', 'auto']
assert fields['climate']['step'] == 1
assert 'fan_modes' not in fields['climate']
assert fields['program']['visibleWhen'] == {'field': 'climate', 'property': 'mode', 'equals': 'auto'}
assert 'visibleWhen' not in fields['climate']
for key in ['sensor', 'calibration', 'panel_lock']:
    assert 'visibleWhen' not in fields[key]
assert pages[1]['name']=='Schedule'
assert pages[1]['visibleWhen']=={'field':'climate','property':'mode','equals':'auto'}
for field in pages[1]['fields'][1:]:
    assert field['visibleWhen']['field']=='program'
    assert field['visibleWhen']['equals'] in ['5+2','6+1','7']
    assert field['type'] in ['time','slider']
    if field['type']=='slider':
        assert (field['min'],field['max'],field['step'])==(5,40,1)
flags = dict(re.findall(r'#define (ARDPORTAL_\w+) ([01])', (root / 'portal_features.h').read_text()))
assert flags['ARDPORTAL_ENABLE_CONTROLS'] == '0'
used = {f['type'].upper() for f in fields.values()}
enabled = {k.removeprefix('ARDPORTAL_ENABLE_CONTROL_') for k,v in flags.items() if k.startswith('ARDPORTAL_ENABLE_CONTROL_') and v == '1'}
assert used == enabled, (used, enabled)
for path in root.glob('*.ino'):
    assert not re.search(r'#include.*(ArduinoJson|LittleFS|PubSubClient|WiFiManager)', path.read_text()), path
print('ArdPortal schemas, offline control visibility, mode/day-group dependencies and minimal control profile passed')

assert pages[1]['fields'][0]['id'] == 'program'
assert 'program' not in {f['id'] for f in pages[0]['fields']}
assert [o['name'] for o in fields['program']['options']] == ['Mon–Fri / Weekend','Mon–Sat / Sunday','Every day']
for page in pages:
    assert not re.search(r'[ąćęłńóśźżĄĆĘŁŃÓŚŹŻ]', json.dumps(page, ensure_ascii=False))

assert set(fields["climate"]["default"]) == {"temperature", "current_temperature", "mode", "action"}
seed_ids = re.findall(r'"([a-z_0-9]+)"', (root / "portal_state_fields.h").read_text())
assert len(seed_ids) == len(set(seed_ids))
assert set(seed_ids) == {key for key, field in fields.items() if field["type"] != "button"}
assert len(seed_ids)<=96
assert all(f["name"].startswith("Schedule: ") for page in pages[1:2] for f in page["fields"] if f["type"] != "button" and f["id"] != "draft_status")
assert len(pages[1]["fields"])==45


assert "mcu_ready" not in fields and "bridge_status" not in fields


assert all(f["type"] != "button" for f in fields.values())

assert "draft_status" not in fields

assert pages[2]["id"]=="clock" and len(pages[2]["fields"])==14
for rule in [1,2]:
    assert fields[f"tz_rule{rule}_offset"]["default"]==0
    assert fields[f"tz_rule{rule}_offset"]["persist"]
    assert fields[f"tz_rule{rule}_offset"]["step"]==0.25
