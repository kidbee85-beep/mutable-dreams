"""Validate proposed SMK surfaces without enabling a hardware target or opening USB."""
import json
from pathlib import Path
R=Path(__file__).resolve().parents[1]
for name in ['smk37-elite','smk37-pro']:
    p=json.loads((R/'docs/hardware'/f'{name}-surface-proposal.json').read_text())
    assert p['build_enabled'] is False
    assert p['key_count']==37 and p['first_midi_note']+36==84
    assert p['pads']==16 and p['faders']==4 and p['encoders']==8
    assert len(set(p['controls'].values()))==8
    assert set(p['buttons'])==set(['FX','SEL','ENV','LFO','EDIT','GLO','HOME','SAVE','ARP','SEQ','PLAY','REC','OCT-','OCT+'])
    assert len(set(p['buttons'].values()))==14
    assert all(len(b['targets'])==4 for b in p['fader_banks'])
    assert len(p['proposed_identity'])<=20 and p['installer_model_check_required']
    assert p['long_press_help'] and p['factory_calibration'].startswith('read-only')
print('PASS separate model identities, 37 keys, 8 encoder roles, 14 buttons, fader banks, pickup/help/calibration requirements; hardware build remains disabled.')
