#!/usr/bin/env python3
# ============================================================================
#  sf2_to_amy.py  --  bake a General-MIDI SoundFont (.sf2) into AMY's ROM PCM
#                     bank and the Medusa GM preset map.
#
#  Outputs (relative to --amy-src / --sketch):
#    <amy-src>/pcm_tiny.h          -- map table + #defines
#    <amy-src>/pcm_samples_tiny.h  -- the int16 PCM pool (PROGMEM)
#    <sketch>/medusa_gm.h          -- GM program/drum -> preset maps + names
#
#  One representative sample is baked per GM program (taken at a mid key) and
#  per GM drum note.  Looped (sustaining) instruments are trimmed to their loop
#  end so the bank stays small while still sustaining indefinitely via the loop;
#  one-shots keep their full decay up to a cap.  Everything is resampled to a
#  single common rate (AMY plays the whole pool back at PCM_AMY_SAMPLE_RATE).
#
#  Usage:
#    python3 tools/sf2_to_amy.py inspect  font.sf2
#    python3 tools/sf2_to_amy.py generate font.sf2 \
#        --amy-src "AMY (1)/AMY/src" --sketch MedusaGM_good_v2_reverb_panning
# ============================================================================
import struct, sys, os, math, argparse
import numpy as np
from math import gcd
from scipy.signal import resample_poly

# ── tunables ────────────────────────────────────────────────────────────────
TARGET_SR        = 44100     # AMY ROM pool rate; matches AMY_SAMPLE_RATE so a
                             # note at the sample's root plays back 1:1 (no
                             # resampling) for maximum fidelity.
ONESHOT_MAX_SEC  = 3.5       # cap for non-looping samples (crashes/cymbals ring)
LOOP_TAIL        = 16        # samples kept past loop end (smooth wrap)
MELODIC_KEY      = 60        # pick each melodic program's sample near middle C
MELODIC_VEL      = 100
DRUM_BANK        = 128
DRUM_NOTE_LO     = 27
DRUM_NOTE_HI     = 87

# SF2 generator operator numbers we care about.
GEN_startAddrOff       = 0
GEN_endAddrOff         = 1
GEN_startLoopOff       = 2
GEN_endLoopOff         = 3
GEN_startAddrCoarse    = 4
GEN_endAddrCoarse      = 12
GEN_coarseTune         = 51
GEN_fineTune           = 52
GEN_sampleID           = 53
GEN_sampleModes        = 54
GEN_startLoopCoarse    = 45
GEN_endLoopCoarse      = 50
GEN_keyRange           = 43
GEN_velRange           = 44
GEN_instrument         = 41
GEN_overridingRootKey  = 58

# ── RIFF / SF2 parsing ──────────────────────────────────────────────────────
class SF2:
    def __init__(self, path):
        self.data = open(path, 'rb').read()
        d = self.data
        assert d[0:4] == b'RIFF' and d[8:12] == b'sfbk', "not an SF2 file"
        end = struct.unpack_from('<I', d, 4)[0] + 8
        self.smpl = None
        pdta = None
        pos = 12
        while pos < end:
            cid = d[pos:pos+4]; sz = struct.unpack_from('<I', d, pos+4)[0]
            if cid == b'LIST':
                lt = d[pos+8:pos+12]
                if lt == b'sdta':
                    self.smpl = self._find(pos+12, pos+8+sz, b'smpl')
                elif lt == b'pdta':
                    pdta = (pos+12, pos+8+sz)
            pos += 8 + sz + (sz & 1)
        assert self.smpl is not None and pdta is not None
        s_off, s_len = self.smpl
        self.samples = np.frombuffer(d, dtype='<i2', count=s_len // 2, offset=s_off)
        self._parse_pdta(*pdta)

    def _find(self, start, end, want):
        d = self.data; pos = start
        while pos < end:
            cid = d[pos:pos+4]; sz = struct.unpack_from('<I', d, pos+4)[0]
            if cid == want:
                return (pos+8, sz)
            pos += 8 + sz + (sz & 1)
        return None

    def _records(self, start, end):
        d = self.data; out = {}; pos = start
        while pos < end:
            cid = d[pos:pos+4]; sz = struct.unpack_from('<I', d, pos+4)[0]
            out[cid] = (pos+8, sz)
            pos += 8 + sz + (sz & 1)
        return out

    def _parse_pdta(self, start, end):
        d = self.data; r = self._records(start, end)
        def arr(cid, rec):
            o, s = r[cid]; return o, s // rec
        # phdr
        o, n = arr(b'phdr', 38); self.phdr = []
        for i in range(n):
            p = o + i*38
            name = d[p:p+20].split(b'\0')[0].decode('latin1')
            preset, bank, bagndx = struct.unpack_from('<HHH', d, p+20)
            self.phdr.append((name, preset, bank, bagndx))
        # pbag
        o, n = arr(b'pbag', 4); self.pbag = [struct.unpack_from('<HH', d, o+i*4) for i in range(n)]
        # pgen
        o, n = arr(b'pgen', 4); self.pgen = [struct.unpack_from('<HH', d, o+i*4) for i in range(n)]
        # inst
        o, n = arr(b'inst', 22); self.inst = []
        for i in range(n):
            p = o + i*22
            name = d[p:p+20].split(b'\0')[0].decode('latin1')
            bagndx = struct.unpack_from('<H', d, p+20)[0]
            self.inst.append((name, bagndx))
        # ibag
        o, n = arr(b'ibag', 4); self.ibag = [struct.unpack_from('<HH', d, o+i*4) for i in range(n)]
        # igen
        o, n = arr(b'igen', 4); self.igen = [struct.unpack_from('<HH', d, o+i*4) for i in range(n)]
        # shdr
        o, n = arr(b'shdr', 46); self.shdr = []
        for i in range(n):
            p = o + i*46
            name = d[p:p+20].split(b'\0')[0].decode('latin1')
            (start_, end_, sl, el, sr) = struct.unpack_from('<IIIII', d, p+20)
            (pitch, corr, link, stype) = struct.unpack_from('<BbHH', d, p+40)
            self.shdr.append(dict(name=name, start=start_, end=end_, sl=sl, el=el,
                                  sr=sr, pitch=pitch, corr=corr, link=link, stype=stype))

    # -- zone helpers -------------------------------------------------------
    def _zone_gens(self, gen_list, g0, g1):
        """Return ordered list of (oper, raw_amount) for one bag's gen span."""
        return [gen_list[i] for i in range(g0, g1)]

    def preset_zones(self, idx):
        """List of (gens_dict, ranges) for a preset's bags; gens_dict maps oper->amount."""
        _, _, _, bag0 = self.phdr[idx]
        bag1 = self.phdr[idx+1][3]
        zones = []
        for b in range(bag0, bag1):
            g0 = self.pbag[b][0]; g1 = self.pbag[b+1][0]
            zones.append(self._gens_to_dict(self.pgen, g0, g1))
        return zones

    def inst_zones(self, inst_idx):
        _, bag0 = self.inst[inst_idx]
        bag1 = self.inst[inst_idx+1][1]
        zones = []
        for b in range(bag0, bag1):
            g0 = self.ibag[b][0]; g1 = self.ibag[b+1][0]
            zones.append(self._gens_to_dict(self.igen, g0, g1))
        return zones

    def _gens_to_dict(self, gen_list, g0, g1):
        gens = {}; ranges = {}
        for i in range(g0, g1):
            oper, amount = gen_list[i]
            if oper in (GEN_keyRange, GEN_velRange):
                ranges[oper] = (amount & 0xFF, (amount >> 8) & 0xFF)
            else:
                gens[oper] = amount
        gens['_ranges'] = ranges
        return gens

# ── selection logic ─────────────────────────────────────────────────────────
def in_range(rng, v):
    return rng is None or (rng[0] <= v <= rng[1])

def signed(x):
    return x - 0x10000 if x >= 0x8000 else x

def find_preset(sf2, bank, preset):
    for i, (name, p, b, bag) in enumerate(sf2.phdr[:-1]):
        if b == bank and p == preset:
            return i, name
    return None, None

def pick_instrument(sf2, pidx, key, vel):
    """Walk preset zones -> instrument index covering (key,vel), with global gens."""
    zones = sf2.preset_zones(pidx)
    glob = {}
    if zones and GEN_instrument not in zones[0]:
        glob = zones[0]                      # global preset zone
    best = None
    for z in zones:
        if GEN_instrument not in z:
            continue
        kr = z['_ranges'].get(GEN_keyRange, glob['_ranges'].get(GEN_keyRange) if glob else None)
        vr = z['_ranges'].get(GEN_velRange, glob['_ranges'].get(GEN_velRange) if glob else None)
        if in_range(kr, key) and in_range(vr, vel):
            return z[GEN_instrument], z, glob
        if best is None:
            best = (z[GEN_instrument], z, glob)
    return best if best else (None, None, None)

def pick_sample_zone(sf2, inst_idx, key, vel):
    zones = sf2.inst_zones(inst_idx)
    glob = {}
    if zones and GEN_sampleID not in zones[0]:
        glob = zones[0]
    cand = None
    for z in zones:
        if GEN_sampleID not in z:
            continue
        kr = z['_ranges'].get(GEN_keyRange)
        vr = z['_ranges'].get(GEN_velRange)
        if in_range(kr, key) and in_range(vr, vel):
            return z, glob
        # remember closest-by-key zone as fallback
        if cand is None:
            cand = (z, glob)
        elif kr is not None:
            ck = cand[0]['_ranges'].get(GEN_keyRange)
            if ck is not None and abs((kr[0]+kr[1])/2 - key) < abs((ck[0]+ck[1])/2 - key):
                cand = (z, glob)
    return cand if cand else (None, None)

def gen_val(zone, glob, oper, default=0):
    if oper in zone:  return zone[oper]
    if glob and oper in glob: return glob[oper]
    return default

def extract_sample(sf2, zone, glob):
    sid = zone[GEN_sampleID]
    sh = sf2.shdr[sid]
    start = sh['start'] + signed(gen_val(zone, glob, GEN_startAddrOff)) \
            + 32768 * signed(gen_val(zone, glob, GEN_startAddrCoarse))
    end   = sh['end']   + signed(gen_val(zone, glob, GEN_endAddrOff)) \
            + 32768 * signed(gen_val(zone, glob, GEN_endAddrCoarse))
    sl    = sh['sl'] + signed(gen_val(zone, glob, GEN_startLoopOff)) \
            + 32768 * signed(gen_val(zone, glob, GEN_startLoopCoarse))
    el    = sh['el'] + signed(gen_val(zone, glob, GEN_endLoopOff)) \
            + 32768 * signed(gen_val(zone, glob, GEN_endLoopCoarse))
    start = max(0, start); end = min(len(sf2.samples), end)
    if end <= start:
        return None
    pcm = sf2.samples[start:end].astype(np.float32)
    loopstart = sl - start
    loopend   = el - start
    modes = gen_val(zone, glob, GEN_sampleModes, 0)
    looped = (modes & 1) == 1 and 0 <= loopstart < loopend <= len(pcm)

    # effective root note (override > shdr pitch), corrected for tuning gens
    root = gen_val(zone, glob, GEN_overridingRootKey, 0xFFFF)
    if root == 0xFFFF or root > 127:
        root = sh['pitch'] if sh['pitch'] <= 127 else 60
    coarse = signed(gen_val(zone, glob, GEN_coarseTune, 0))
    fine   = signed(gen_val(zone, glob, GEN_fineTune, 0))
    root = root - coarse - round(fine / 100.0)
    root = max(0, min(127, root))

    return dict(pcm=pcm, sr=sh['sr'], looped=looped,
                loopstart=loopstart, loopend=loopend, root=root, name=sh['name'])

def resample_and_trim(s):
    pcm, sr = s['pcm'], s['sr']
    if sr != TARGET_SR:
        g = gcd(TARGET_SR, sr)
        up, down = TARGET_SR // g, sr // g
        ratio = TARGET_SR / sr
        pcm = resample_poly(pcm, up, down).astype(np.float32)
        ls = int(round(s['loopstart'] * ratio))
        le = int(round(s['loopend']   * ratio))
    else:
        ls, le = s['loopstart'], s['loopend']
    if s['looped'] and 0 <= ls < le <= len(pcm):
        keep = min(len(pcm), le + LOOP_TAIL)
        pcm = pcm[:keep]
        loopstart, loopend = ls, le
    else:
        cap = int(ONESHOT_MAX_SEC * TARGET_SR)
        pcm = pcm[:cap]
        loopstart, loopend = 0, len(pcm)
        s['looped'] = False
    # clip to int16
    pcm = np.clip(np.round(pcm), -32768, 32767).astype(np.int16)
    return pcm, loopstart, loopend

# ── medusa_gm.h GM_NAMES (kept stable; read from existing file) ──────────────
def read_gm_names(medusa_path):
    txt = open(medusa_path).read()
    i = txt.index('GM_NAMES[128]')
    i = txt.index('{', i); j = txt.index('};', i)
    body = txt[i+1:j]
    names = []
    for tok in body.split('"'):
        pass
    # parse quoted strings
    import re
    names = re.findall(r'"((?:[^"\\]|\\.)*)"', body)
    assert len(names) == 128, f"expected 128 GM names, got {len(names)}"
    return names

# ── generation ───────────────────────────────────────────────────────────────
def generate(args):
    sf2 = SF2(args.font)
    medusa_path = os.path.join(args.sketch, 'medusa_gm.h')
    gm_names = read_gm_names(medusa_path)

    presets = []            # list of dict(pcm, loopstart, loopend, root, looped, label)
    prog_preset = [255]*128
    drum_preset = [255]*128

    def add_preset(s, label):
        pcm, ls, le = resample_and_trim(s)
        if len(pcm) < 8:
            return None
        idx = len(presets)
        presets.append(dict(pcm=pcm, loopstart=ls, loopend=le,
                            root=s['root'], looped=s['looped'], label=label))
        return idx

    # melodic programs 0..127, bank 0
    for prog in range(128):
        pidx, pname = find_preset(sf2, 0, prog)
        if pidx is None:
            continue
        inst_idx, pz, pglob = pick_instrument(sf2, pidx, MELODIC_KEY, MELODIC_VEL)
        if inst_idx is None:
            continue
        z, zglob = pick_sample_zone(sf2, inst_idx, MELODIC_KEY, MELODIC_VEL)
        if z is None:
            continue
        s = extract_sample(sf2, z, zglob)
        if s is None:
            continue
        i = add_preset(s, f"{gm_names[prog]} ({s['name']})")
        if i is not None:
            prog_preset[prog] = i

    # fill missing programs with nearest existing (or piano 0)
    fallback = prog_preset[0] if prog_preset[0] != 255 else 0
    for prog in range(128):
        if prog_preset[prog] == 255:
            prog_preset[prog] = fallback

    # drum kit: bank 128 preset 0 (fall back to any bank-128 preset)
    didx, dname = find_preset(sf2, DRUM_BANK, 0)
    if didx is None:
        for i,(name,p,b,bag) in enumerate(sf2.phdr[:-1]):
            if b == DRUM_BANK:
                didx = i; break
    if didx is not None:
        inst_idx, pz, pglob = pick_instrument(sf2, didx, 38, 100)  # snare-ish probe
        # the drum kit instrument is whichever the (global) preset zone points to
        zones = sf2.preset_zones(didx)
        drum_inst = None
        for zz in zones:
            if GEN_instrument in zz:
                drum_inst = zz[GEN_instrument]; break
        for note in range(DRUM_NOTE_LO, DRUM_NOTE_HI+1):
            if drum_inst is None:
                break
            z, zglob = pick_sample_zone(sf2, drum_inst, note, 100)
            if z is None:
                continue
            s = extract_sample(sf2, z, zglob)
            if s is None:
                continue
            # Drums are played one-shot by the engine (feedback=0): keep the
            # sample's full natural decay instead of trimming to its loop end,
            # so cymbals/toms ring out rather than getting chopped.
            s['looped'] = False
            i = add_preset(s, f"Drum {note} ({s['name']})")
            if i is not None:
                drum_preset[note] = i

    write_outputs(args, presets, prog_preset, drum_preset, gm_names)

def write_outputs(args, presets, prog_preset, drum_preset, gm_names):
    n = len(presets)
    total = sum(len(p['pcm']) for p in presets)
    print(f"presets={n}  total_samples={total}  ~{total*2/1e6:.2f} MB flash")

    # ----- pcm_samples_tiny.h -----
    samp_path = os.path.join(args.amy_src, 'pcm_samples_tiny.h')
    with open(samp_path, 'w') as f:
        f.write("// Generated by tools/sf2_to_amy.py from a GM SoundFont.\n")
        f.write("// AMY ROM PCM sample pool (mono int16 @ %d Hz).\n" % TARGET_SR)
        f.write('#include "amy.h"\n\n')
        f.write("const int16_t pcm[PCM_LENGTH] PROGMEM = {\n")
        col = 0
        for p in presets:
            for v in p['pcm']:
                f.write("%d," % int(v))
                col += 1
                if col == 16:
                    f.write("\n"); col = 0
        if col: f.write("\n")
        f.write("};\n")

    # ----- pcm_tiny.h -----
    map_path = os.path.join(args.amy_src, 'pcm_tiny.h')
    with open(map_path, 'w') as f:
        f.write("// Generated by tools/sf2_to_amy.py -- GM SoundFont ROM bank.\n")
        f.write("#ifndef __PCM_H\n#define __PCM_H\n")
        f.write("#define PCM_AMY_SAMPLE_RATE %d\n" % TARGET_SR)
        f.write("#define PCM_BASE_SAMPLES %d\n" % n)
        f.write("#define PCM_BASE_LENGTH %d\n" % total)
        f.write("#define PCM_WAVETABLE_BASE PCM_BASE_SAMPLES\n")
        f.write("#define PCM_WAVETABLE_SAMPLES 0\n")
        f.write("#define PCM_WAVETABLE_LEN 0\n")
        f.write("#define PCM_LENGTH PCM_BASE_LENGTH\n")
        f.write("#define PCM_MAP_ENTRIES PCM_BASE_SAMPLES\n")
        f.write('#include "pcm_samples_tiny.h"\n')
        f.write("const uint16_t pcm_samples = PCM_MAP_ENTRIES;\n")
        f.write("const uint16_t pcm_wavetable_base = PCM_WAVETABLE_BASE;\n")
        f.write("const uint16_t pcm_wavetable_samples = PCM_WAVETABLE_SAMPLES;\n")
        f.write("const uint32_t pcm_wavetable_len = PCM_WAVETABLE_LEN;\n")
        f.write("const pcm_map_t pcm_map[PCM_MAP_ENTRIES] PROGMEM = {\n")
        off = 0
        for i, p in enumerate(presets):
            ln = len(p['pcm'])
            f.write("    {%d, %d, %d, %d, %d}, /* [%d] %s */\n" %
                    (off, ln, p['loopstart'], p['loopend'], p['root'], i, p['label'][:40]))
            off += ln
        f.write("};\n#endif // __PCM_H\n")

    # ----- medusa_gm.h -----
    write_medusa(args, presets, prog_preset, drum_preset, gm_names)
    print(f"wrote {samp_path}\n      {map_path}")

def write_medusa(args, presets, prog_preset, drum_preset, gm_names):
    n = len(presets)
    path = os.path.join(args.sketch, 'medusa_gm.h')
    def fmt_table(vals):
        out = []
        for r in range(0, len(vals), 16):
            out.append("    " + " ".join("%3d," % v for v in vals[r:r+16]))
        return "\n".join(out)
    roots  = [p['root'] for p in presets]
    looped = [1 if p['looped'] else 0 for p in presets]
    with open(path, 'w') as f:
        f.write("// Generated by tools/sf2_to_amy.py -- GM program/drum -> AMY preset map.\n")
        f.write("#pragma once\n#include <Arduino.h>\n\n")
        f.write("// AMY PCM preset index for each GM program (0..127).\n")
        f.write("static const uint8_t GM_PROGRAM_PRESET[128] PROGMEM = {\n")
        f.write(fmt_table(prog_preset) + "\n};\n\n")
        f.write("// AMY PCM preset index for each GM drum note (255 = none).\n")
        f.write("static const uint8_t GM_DRUM_PRESET[128] PROGMEM = {\n")
        f.write(fmt_table(drum_preset) + "\n};\n\n")
        f.write("#define GM_NUM_PRESETS %d\n\n" % n)
        f.write("// Recorded root note of each preset's sample.\n")
        f.write("static const uint8_t GM_PRESET_ROOT[GM_NUM_PRESETS] PROGMEM = {\n")
        f.write(fmt_table(roots) + "\n};\n\n")
        f.write("// 1 = preset has a sustain loop; 0 = one-shot (percussive).\n")
        f.write("static const uint8_t GM_PRESET_LOOPED[GM_NUM_PRESETS] PROGMEM = {\n")
        f.write(fmt_table(looped) + "\n};\n\n")
        f.write("// GM program names (<=12 chars) for the UI.\n")
        f.write("static const char* const GM_NAMES[128] = {\n")
        for r in range(0, 128, 4):
            f.write("    " + " ".join('"%s",' % gm_names[k] for k in range(r, r+4)) + "\n")
        f.write("};\n")
    print(f"      {path}")

# ── inspect ──────────────────────────────────────────────────────────────────
def inspect(args):
    sf2 = SF2(args.font)
    print(f"samples pool: {len(sf2.samples)} int16   instruments: {len(sf2.inst)-1}")
    banks = {}
    for name, p, b, bag in sf2.phdr[:-1]:
        banks.setdefault(b, []).append((p, name))
    for b in sorted(banks):
        print(f"\n== bank {b} ==  ({len(banks[b])} presets)")
        for p, name in sorted(banks[b])[:8]:
            print(f"   {p:3d} {name}")
        if len(banks[b]) > 8:
            print("   ...")
    # probe a few melodic programs
    print("\n-- melodic probes (key 60) --")
    for prog in (0, 24, 40, 48, 56, 73):
        pidx, pname = find_preset(sf2, 0, prog)
        if pidx is None:
            print(f"   prog {prog}: MISSING"); continue
        inst_idx, pz, pg = pick_instrument(sf2, pidx, MELODIC_KEY, MELODIC_VEL)
        z, zg = pick_sample_zone(sf2, inst_idx, MELODIC_KEY, MELODIC_VEL) if inst_idx is not None else (None,None)
        if z is None:
            print(f"   prog {prog} {pname}: no sample"); continue
        s = extract_sample(sf2, z, zg)
        print(f"   prog {prog:3d} {pname:20.20s} -> {s['name']:18.18s} sr={s['sr']} "
              f"root={s['root']} looped={s['looped']} len={len(s['pcm'])}")

# ── cli ──────────────────────────────────────────────────────────────────────
if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    sub = ap.add_subparsers(dest='cmd', required=True)
    pi = sub.add_parser('inspect'); pi.add_argument('font')
    pg = sub.add_parser('generate'); pg.add_argument('font')
    pg.add_argument('--amy-src', required=True)
    pg.add_argument('--sketch', required=True)
    args = ap.parse_args()
    if args.cmd == 'inspect':
        inspect(args)
    else:
        generate(args)
