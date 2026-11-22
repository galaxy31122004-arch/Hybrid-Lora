from pathlib import Path

root = Path(__file__).resolve().parents[1]
p = root / 'firmware/v0.5.3/README.md'
s = p.read_text(encoding='utf-8')
s = s.replace('firmware/v0.5.2/', 'firmware/v0.5.3/').replace('firmware\\v0.5.2', 'firmware\\v0.5.3')
s = s.replace('**SF12** / 125 kHz', '**SF12** / 250 kHz').replace('≈ 0.83 s', '≈ 0.414 s')
s = s.replace('uplink ≈ 4.6% (≈ 91%', 'uplink ≈ 2.3% (≈ 95%').replace('≈ 0.7% plus commands', '≈ 0.35% plus commands')
s = s.replace('~5.5% air time (0.83 s every 15 s)', '~2.8% air time (0.414 s every 15 s)')
s = s.replace('4. Set `MODE = LOW` on the gateway board and upload from the repository root:', '4. Upload the gateway build from the repository root (MODE is unused):')
s = s.replace('pio run -e esp32dev -t upload --upload-port COM3', 'powershell -ExecutionPolicy Bypass -File scripts/flash.ps1 -Env gateway -Port COM3')
s = s.replace('work. The current Node ID is fixed to 1 in `firmware/src/main.cpp`; assign\ndistinct IDs before deploying more nodes.', 'work. Set distinct NODE_ID build flags in platformio.ini before deploying\nmore nodes.')
s += '\nThe continuous `node-test` build differs from timed test mode: it sends back\nto back and is not suitable for verifying downlink commands. Use the normal\n`node` build for COMMAND/ACK tests. Cloud telemetry keeps only the latest\nsnapshot per node while disconnected; the dashboard is not a lossless packet\nrecorder. Use gateway Serial logs when every received packet must be retained.\n'
p.write_text(s, encoding='utf-8', newline='\n')
p = root / 'firmware/README.md'
s = p.read_text(encoding='utf-8').replace('**0.5.2 (current)**', '0.5.2').replace('| `esp32dev` |', '| `esp32dev-v0_5_2` |')
s = s.replace('| `v0.5.2/`', '| `v0.5.3/` | **0.5.3 (current)** | Build-time gateway/node role, single-radio gateway fallback, SF12 / 250 kHz; update both ends together. Continuous field-test builds | `gateway`, `node`, `gateway-test`, `node-test` |\n| `v0.5.2/`', 1)
s = s.replace('pio run -e esp32dev                 # current version (v0.5.2)', 'pio run -e gateway -e node         # current version (v0.5.3)')
s = s.replace('pio run -e esp32dev -t upload --upload-port COM3', 'powershell -ExecutionPolicy Bypass -File scripts/flash.ps1 -Env gateway -Port COM3')
p.write_text(s, encoding='utf-8', newline='\n')
p = root / 'documents/work-log.md'
p.write_text(p.read_text(encoding='utf-8').rstrip() + '\n', encoding='utf-8', newline='\n')
print('Updated current firmware documentation and firmware index.')
