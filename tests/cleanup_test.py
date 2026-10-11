"""Verify legacy discovery coverage and deletion packet framing without a broker."""
import importlib.util
import io
import json
from pathlib import Path
root = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('cleanup', root / 'tools/remove_legacy_discovery.py')
cleanup = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cleanup)
old = json.loads((root / 'tests/legacy_discovery_topics.json').read_text())
removals = set(cleanup.topics('thermostat'))
for value in old:
    assert value.replace('[DEVICE_NAME]', 'thermostat') in removals, value
class Socket:
    def __init__(self, data): self.stream = io.BytesIO(data)
    def recv(self, size): return self.stream.read(min(size, 2))
for topic in removals:
    packet = cleanup.packet(0x33, cleanup.mqtt_string(topic) + b'\0\1')
    kind, body = cleanup.response(Socket(packet))
    assert kind == 0x33  # Retained, QoS 1.
    assert body == cleanup.mqtt_string(topic) + b'\0\1'  # Empty payload.
# Multi-byte remaining-length framing and truncated replies.
assert cleanup.response(Socket(cleanup.packet(0x30, b'a' * 300))) == (0x30, b'a' * 300)
try:
    cleanup.response(Socket(b'\x40\x02\x00'))
    raise AssertionError('Truncation must fail')
except ConnectionError:
    pass
print('Legacy HA removal coverage, QoS 1 framing and truncated reply handling passed')
