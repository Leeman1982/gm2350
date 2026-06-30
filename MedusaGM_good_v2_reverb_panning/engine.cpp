#include "engine.h"
#include "medusa_gm.h"

extern "C" {
#include <amy.h>
}

// AMY exposes pcm_samples = number of baked presets in the linked PCM bank.
extern "C" const uint16_t pcm_samples;

// Stereo bus map.  AMY applies reverb/chorus per *bus*, so the per-track reverb
// amount picks one of three tiers:
//   0       -> dry  bus (no FX)
//   1..63   -> room bus (moderate reverb + a touch of chorus)
//   64..127 -> hall bus (long reverb)
static const uint8_t BUS_DRY  = 1;
static const uint8_t BUS_ROOM = 0;   // bus 0 = default voices land here
static const uint8_t BUS_HALL = 2;
static inline uint8_t busFor(uint8_t reverbSend) {
#if MEDUSA_FX
    if (reverbSend == 0) return BUS_DRY;
    return reverbSend < 64 ? BUS_ROOM : BUS_HALL;
#else
    (void)reverbSend; return 0;       // FX off: everything dry on one bus
#endif
}
static inline float panf(uint8_t p) { return p / 127.0f; }   // 0..1, 0.5 = center

bool Engine::bankOK() const {
    return pcm_samples >= GM_NUM_PRESETS;
}

// Configure the room + hall reverb buses (+ a subtle chorus on the room bus).
// Compiled out when MEDUSA_FX is 0 so the reverb DSP isn't even linked.
void Engine::setupEffects() {
#if MEDUSA_FX
    config_reverb(BUS_ROOM, 1.3f, REVERB_DEFAULT_LIVENESS, REVERB_DEFAULT_DAMPING,
                  REVERB_DEFAULT_XOVER_HZ);
    config_chorus(BUS_ROOM, 0.28f, CHORUS_DEFAULT_MAX_DELAY, CHORUS_DEFAULT_LFO_FREQ,
                  CHORUS_DEFAULT_MOD_DEPTH);
    config_reverb(BUS_HALL, 2.1f, 0.92f, REVERB_DEFAULT_DAMPING,
                  REVERB_DEFAULT_XOVER_HZ);
#endif
}

// ─── helpers ────────────────────────────────────────────────────────────────
static inline float velf(uint8_t v, bool accent) {
    float f = v / 127.0f;
    if (accent) f = fminf(1.0f, f + 0.22f);
    return fmaxf(0.02f, f);
}

static inline uint8_t synthWaveToAmy(uint8_t w) {
    switch (w) {
        case SW_SAW:    return SAW_DOWN;
        case SW_SQUARE: return PULSE;     // 50% duty
        case SW_TRI:    return TRIANGLE;
        case SW_SINE:   return SINE;
        case SW_PULSE:  return PULSE;
        default:        return SAW_DOWN;
    }
}

void Engine::begin() {
    for (int i = 0; i < POLYPHONY; i++) {
        _v[i].osc = i;            // oscillators 0..POLYPHONY-1 are ours
        _v[i].active = _v[i].released = false;
        _v[i].track = 0xFF;
    }
    setMasterVolume(0.9f);
}

void Engine::setMasterVolume(float v) {
    amy_event e = amy_default_event();
    for (int b = 0; b < AMY_NUM_BUSES; b++) e.volume[b] = v;
    amy_add_event(&e);
}

// Prefer: a free slot, else the oldest released (tail) slot, else steal the
// oldest active voice.
int Engine::allocVoice() {
    int freeIdx = -1, relIdx = -1, actIdx = -1;
    uint32_t relOld = 0xFFFFFFFF, actOld = 0xFFFFFFFF;
    for (int i = 0; i < POLYPHONY; i++) {
        if (!_v[i].active && !_v[i].released) { freeIdx = i; break; }
        if (_v[i].released && _v[i].stamp < relOld) { relOld = _v[i].stamp; relIdx = i; }
        if (_v[i].active   && _v[i].stamp < actOld) { actOld = _v[i].stamp; actIdx = i; }
    }
    if (freeIdx >= 0) return freeIdx;
    if (relIdx  >= 0) return relIdx;
    if (actIdx  < 0)  actIdx = 0;   // unreachable, but never deref -1
    // steal: silence it first so the reused osc doesn't click
    sendOff(_v[actIdx].osc);
    return actIdx;
}

// ─── per-voice AMY configuration ────────────────────────────────────────────
// Melodic GM: a ROM PCM oscillator pitched by the played note relative to the
// sample's recorded root (AMY does that automatically from pcm_map).
void Engine::configMelodic(uint16_t osc, uint8_t program, uint8_t note,
                           uint8_t vel, uint8_t pan, uint8_t bus) {
    uint8_t preset = pgm_read_byte(&GM_PROGRAM_PRESET[program & 0x7F]);
    amy_event e = amy_default_event();
    e.osc       = osc;
    e.wave      = PCM;
    e.preset    = preset;
    e.bus       = bus;                         // stereo FX routing
    e.pan_coefs[COEF_CONST] = panf(pan);       // stereo placement
    e.filter_type = FILTER_NONE;  // PCM voices are unfiltered (clear stale state)
    e.midi_note = note;
    // Enable the sustain loop only for looped presets; one-shots (xylophone etc.)
    // should play once and let their EG release fade them naturally.
    e.feedback  = pgm_read_byte(&GM_PRESET_LOOPED[preset]) ? 1 : 0;
    // Amp envelope: fast attack, full sustain held until note-off, then release.
    // All three breakpoints are written so a reused oscillator carries no stale
    // envelope state from a previous (e.g. synth) voice.
    e.eg0_times[0] = 2;    e.eg0_values[0] = 1.0f;   // attack
    e.eg0_times[1] = 1;    e.eg0_values[1] = 1.0f;   // (no decay) sustain = 1
    e.eg0_times[2] = 200;  e.eg0_values[2] = 0.0f;   // release
    e.velocity  = velf(vel, false);
    amy_add_event(&e);
}

// Drum: same ROM PCM bank but played at the sample's native pitch (root note)
// and with a percussive one-shot envelope.
void Engine::configDrum(uint16_t osc, uint8_t drumNote, uint8_t vel,
                        uint8_t pan, uint8_t bus) {
    uint8_t preset = pgm_read_byte(&GM_DRUM_PRESET[drumNote & 0x7F]);
    if (preset == 255) preset = pgm_read_byte(&GM_DRUM_PRESET[36]); // fallback kick
    uint8_t root = pgm_read_byte(&GM_PRESET_ROOT[preset % GM_NUM_PRESETS]);
    amy_event e = amy_default_event();
    e.osc       = osc;
    e.wave      = PCM;
    e.preset    = preset;
    e.bus       = bus;
    e.pan_coefs[COEF_CONST] = panf(pan);
    e.filter_type = FILTER_NONE;
    e.midi_note = root;                 // native pitch
    e.feedback  = 0;                    // one-shot: no loop even on reused osc
    e.eg0_times[0] = 1;    e.eg0_values[0] = 1.0f;   // instant attack
    e.eg0_times[1] = 1;    e.eg0_values[1] = 1.0f;   // sustain = 1 (one-shot)
    e.eg0_times[2] = 350;  e.eg0_values[2] = 0.0f;   // ring out short gates
    e.velocity  = velf(vel, false);
    amy_add_event(&e);
}

// Synth track: classic subtractive voice (osc -> resonant low-pass + ADSR).
void Engine::configSynth(uint16_t osc, const SynthParams& syn, uint8_t note,
                         uint8_t vel, uint8_t pan, uint8_t bus) {
    amy_event e = amy_default_event();
    e.osc       = osc;
    e.wave      = synthWaveToAmy(syn.wave);
    e.bus       = bus;
    e.pan_coefs[COEF_CONST] = panf(pan);
    e.midi_note = note + syn.octave * 12;
    if (syn.wave == SW_PULSE) e.duty_coefs[COEF_CONST] = 0.25f;
    // Resonant low-pass filter.  COEF_CONST is a frequency in Hz (AMY converts
    // it to log internally via logfreq_of_freq).
    e.filter_type = FILTER_LPF;
    e.filter_freq_coefs[COEF_CONST] = (float)constrain((int)syn.cutoff, 60, 18000);
    e.resonance = 0.5f + (syn.reso / 100.0f) * 6.0f;
    // ADSR (times in ms, already clamped >=1 by the UI).
    e.eg0_times[0] = syn.attack  ? syn.attack  : 1;  e.eg0_values[0] = 1.0f;
    e.eg0_times[1] = syn.decay   ? syn.decay   : 1;  e.eg0_values[1] = syn.sustain / 100.0f;
    e.eg0_times[2] = syn.release ? syn.release : 1;  e.eg0_values[2] = 0.0f;
    e.velocity  = velf(vel, false);
    amy_add_event(&e);
}

void Engine::sendOff(uint16_t osc) {
    amy_event e = amy_default_event();
    e.osc      = osc;
    e.velocity = 0.0f;
    amy_add_event(&e);
}

// ─── public note API ────────────────────────────────────────────────────────
void Engine::noteOn(uint8_t track, const TrackCfg& cfg, const SynthParams& syn,
                    uint8_t note, uint8_t vel, bool accent) {
    if (cfg.mute) return;
    int idx = allocVoice();
    Voice& v = _v[idx];
    uint8_t vv = accent ? min(127, vel + 24) : vel;
    vv = (uint16_t)vv * cfg.volume / 127;        // track level

    uint8_t bus = busFor(cfg.reverb);
    switch (cfg.kind) {
        case TK_GM_DRUM:    configDrum(v.osc, cfg.drumNote, vv, cfg.pan, bus);   break;
        case TK_SYNTH:      configSynth(v.osc, syn, note, vv, cfg.pan, bus);     break;
        default:            configMelodic(v.osc, cfg.program, note, vv, cfg.pan, bus); break;
    }
    v.active = true; v.released = false;
    v.track = track; v.note = note; v.stamp = millis();
}

void Engine::noteOff(uint8_t track, uint8_t note) {
    for (int i = 0; i < POLYPHONY; i++) {
        if (_v[i].active && _v[i].track == track && _v[i].note == note) {
            sendOff(_v[i].osc);
            _v[i].active = false;
            _v[i].released = true;
            _v[i].stamp = millis();
        }
    }
}

void Engine::allNotesOff() {
    for (int i = 0; i < POLYPHONY; i++) {
        sendOff(_v[i].osc);
        _v[i].active = _v[i].released = false;
        _v[i].track = 0xFF;
    }
}

void Engine::preview(const TrackCfg& cfg, const SynthParams& syn, uint8_t note) {
    int idx = allocVoice();
    Voice& v = _v[idx];
    uint8_t bus = busFor(cfg.reverb);
    switch (cfg.kind) {
        case TK_GM_DRUM: configDrum(v.osc, cfg.drumNote, 110, cfg.pan, bus); break;
        case TK_SYNTH:   configSynth(v.osc, syn, note, 110, cfg.pan, bus);   break;
        default:         configMelodic(v.osc, cfg.program, note, 110, cfg.pan, bus); break;
    }
    v.active = false; v.released = true;   // let it ring, reclaimable
    v.track = 0xFE; v.note = note; v.stamp = millis();
}
