"""Run MCU bridge regressions and ArdPortal schema checks."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix='thermostat-tests-') as directory:
    time_headers=Path(directory)/'time-headers';time_headers.mkdir()
    (time_headers/'TimeLib.h').write_text((root.parent/'libraries/Time/TimeLib.h').read_text())
    for name in ['clock_timezone', 'optimistic_state', 'tuya_parser', 'thermostat_control', 'energy_calibration', 'schedule', 'schedule_preview', 'schedule_days', 'connection_indicator', 'bridge_integration', 'portal_schema']:
        binary = Path(directory) / name
        cmd = ['clang++', '-std=c++11', '-fsanitize=address,undefined', '-g']
        if name in ['clock_timezone', 'bridge_integration', 'portal_schema']:
            cmd += ['-I', str(root / 'tests/host')]
        if name == 'portal_schema':
            cmd += ['-std=c++17', '-I', str(root.parent / 'libraries/ArdPortal/src')]
        if name in ['clock_timezone','bridge_integration']:
            cmd += ['-DARDUINO=10800','-I',str(root.parent/'libraries/Timezone/src'),'-D__time_t_defined','-I',str(time_headers),str(root.parent/'libraries/Timezone/src/Timezone.cpp'),str(root.parent/'libraries/Time/Time.cpp')]
        cmd += [str(root / f'tests/{name}_test.cpp'), '-o', str(binary)]
        subprocess.run(cmd, check=True)
        subprocess.run([str(binary)], check=True)
subprocess.run(['python3', str(root / 'tests/discovery_test.py')], check=True)

subprocess.run(["python3", str(root / "tests/cleanup_test.py")], check=True)

subprocess.run(['python3', str(root / 'tests/mqtt_command_test.py')], check=True)

subprocess.run(['python3', str(root / 'tests/restart_energy_test.py')], check=True)

subprocess.run(['python3', str(root / 'tests/portal_registration_test.py')], check=True)
