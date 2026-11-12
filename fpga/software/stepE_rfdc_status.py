# read-only RFDC status dump (Claude 2026-11-10)
import xrfdc, json
from pynq import Overlay
ol = Overlay("helix_nlsg.bit", download=False); rf = ol.usp_rf_data_converter_0
st = rf.IPStatus
print("IPStatus State", st["State"])
for name, key, tiles in (("ADC", "ADCTileStatus", rf.adc_tiles), ("DAC", "DACTileStatus", rf.dac_tiles)):
    for i, t in enumerate(st[key]):
        t = dict(t); print(name, i, {k: t[k] for k in ("IsEnabled", "TileState", "BlockStatusMask", "PowerUpState", "PLLState")})
for name, t, b in (("ADC2.0", 2, 0),):
    blk = rf.adc_tiles[t].blocks[b]
    print(name, "Mixer", dict(blk.MixerSettings)); print(name, "QMC", dict(blk.QMCSettings)); print(name, "NyquistZone", blk.NyquistZone, "Decim", blk.DecimationFactor, "Status", dict(blk.BlockStatus))
blk = rf.dac_tiles[0].blocks[0]
print("DAC0.0 Mixer", dict(blk.MixerSettings)); print("DAC0.0 NyquistZone", blk.NyquistZone, "Interp", blk.InterpolationFactor, "Status", dict(blk.BlockStatus))
for a in ("OutputCurr", "DecoderMode", "InvSincFIR"):
    try: print("DAC0.0", a, getattr(blk, a))
    except Exception as e: print("DAC0.0", a, "n/a", e)
