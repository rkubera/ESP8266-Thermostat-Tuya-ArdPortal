#!/usr/bin/env python3
"""Remove the previous firmware's retained HA definitions (MQTT 3.1.1/QoS 1)."""
import argparse
import getpass
import socket
import ssl
import struct

ENTITIES = {
    'climate': ['climate'],
    'sensor': ['schedule', 'schedule_draft_status', 'temperature', 'power_sensor',
               'energy_total', 'airtemperature', 'floortemperature', 'humidity'],
    'select': ['program_mode', 'tempselector'],
    'number': ['temperature_offset', 'power_consumption', 'airtemperature_offset',
               'floortemperature_offset'],
    'switch': ['lock'],
    'button': ['schedule_save', 'schedule_reload'],
    'text': [f'schedule_point_{i}' for i in range(1, 9)],
}

def topics(device):
    return [f'homeassistant/{kind}/{device}_{entity}/config'
            for kind, entities in ENTITIES.items() for entity in entities]

def mqtt_string(value):
    data = value.encode('utf-8')
    return struct.pack('!H', len(data)) + data

def packet(kind, body):
    remaining = len(body)
    encoded = bytearray([kind])
    while True:
        byte = remaining % 128
        remaining //= 128
        encoded.append(byte | (128 if remaining else 0))
        if not remaining:
            return bytes(encoded) + body

def exact(connection, length):
    result = b''
    while len(result) < length:
        chunk = connection.recv(length - len(result))
        if not chunk:
            raise ConnectionError('Broker closed the connection')
        result += chunk
    return result

def response(connection):
    kind = exact(connection, 1)[0]
    length = 0
    for shift in range(0, 28, 7):
        byte = exact(connection, 1)[0]
        length |= (byte & 127) << shift
        if not byte & 128:
            return kind, exact(connection, length)
    raise ValueError('Invalid MQTT remaining length')

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True)
    parser.add_argument('--device', required=True, help='OLD MQTT hostname / HA identifier prefix')
    parser.add_argument('--port', type=int)
    parser.add_argument('--user')
    parser.add_argument('--tls', action='store_true')
    parser.add_argument('--ca', help='PEM CA file; system trust store is used otherwise')
    parser.add_argument('--dry-run', action='store_true')
    args = parser.parse_args()
    removals = topics(args.device)
    if args.dry_run:
        print('\n'.join(removals))
        return
    password = getpass.getpass('MQTT password: ') if args.user else ''
    connection = socket.create_connection((args.host, args.port or (8883 if args.tls else 1883)), timeout=10)
    if args.tls:
        connection = ssl.create_default_context(cafile=args.ca).wrap_socket(connection, server_hostname=args.host)
    with connection:
        flags = 2 | (0xc0 if args.user else 0)
        body = mqtt_string('MQTT') + bytes([4, flags]) + struct.pack('!H', 30)
        body += mqtt_string('thermostat-discovery-cleanup')
        if args.user:
            body += mqtt_string(args.user) + mqtt_string(password)
        connection.sendall(packet(0x10, body))
        if response(connection) != (0x20, b'\0\0'):
            raise ConnectionError('Broker refused MQTT CONNECT')
        for identifier, topic in enumerate(removals, 1):
            # Retained QoS 1 PUBLISH with an empty payload deletes the definition.
            connection.sendall(packet(0x33, mqtt_string(topic) + struct.pack('!H', identifier)))
            if response(connection) != (0x40, struct.pack('!H', identifier)):
                raise ConnectionError(f'Missing PUBACK for {topic}')
        connection.sendall(b'\xe0\0')
    print(f'Removed {len(removals)} retained discovery definitions for {args.device}.')

if __name__ == '__main__':
    main()
