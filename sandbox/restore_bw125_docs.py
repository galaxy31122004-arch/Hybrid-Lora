from pathlib import Path

root = Path(__file__).resolve().parents[1]
p = root / 'firmware/v0.5.3/README.md'
s = p.read_text(encoding='utf-8')
s = s.replace('SF12 / 250 kHz', 'SF12 / 125 kHz').replace('16.384 ms', '32.768 ms')
s = s.replace('0.414 s', '0.827 s').replace('**SF12** / 250 kHz', '**SF12** / 125 kHz')
s = s.replace('125 kHz builds cannot communicate with these builds.', '250 kHz builds cannot communicate with these builds.')
s = s.replace('uplink ≈ 2.3% (≈ 95%', 'uplink ≈ 4.6% (≈ 91%').replace('≈ 0.35% plus commands', '≈ 0.7% plus commands')
s = s.replace('~2.8% air time', '~5.5% air time')
p.write_text(s, encoding='utf-8', newline='\n')
p = root / 'firmware/README.md'
s = p.read_text(encoding='utf-8').replace('SF12 / 250 kHz', 'SF12 / 125 kHz')
p.write_text(s, encoding='utf-8', newline='\n')
print('Current firmware documentation restored to BW 125 kHz.')
