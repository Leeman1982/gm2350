#pragma once
// ============================================================================
//  Medusa GM  --  model.h
//  Pure data structures for the song / pattern / track / step hierarchy.
//  No behaviour here -- the engine and sequencer act on these.
// ============================================================================
#include <Arduino.h>
#include "config.h"

// ─── One step in a track's 16-step row ──────────────────────────────────────
struct Step {
    uint8_t note = 60;     // MIDI note (melodic). Ignored for drum tracks.
    uint8_t vel  = 100;    // 1..127
    uint8_t flags = 0;     // see bits below
};
enum StepFlag : uint8_t {
    STEP_ON     = 1 << 0,  // step plays
    STEP_ACCENT = 1 << 1,  // +velocity
    STEP_TIE    = 1 << 2,  // hold/slide into next step (melodic legato)
};
static inline bool stepOn(const Step& s)     { return s.flags & STEP_ON; }
static inline bool stepAccent(const Step& s) { return s.flags & STEP_ACCENT; }
static inline bool stepTie(const Step& s)    { return s.flags & STEP_TIE; }

// ─── Per-track instrument configuration (shared across patterns) ────────────
enum TrackKind : uint8_t { TK_GM_MELODIC, TK_GM_DRUM, TK_SYNTH };

struct TrackCfg {
    TrackKind kind   = TK_GM_MELODIC;
    uint8_t   program = 0;     // GM program 0..127 (melodic)
    uint8_t   drumNote = 36;   // GM drum note (which drum sample) when TK_GM_DRUM
    uint8_t   volume  = 100;   // 0..127 track level
    uint8_t   pan     = 64;    // 0=hard L .. 64=center .. 127=hard R (stereo)
    uint8_t   reverb  = 20;    // 0=dry, 1..63=room, 64..127=hall (when FX on)
    bool      mute    = false;
};

// ─── AMY synth voice parameters (the one synth track) ───────────────────────
enum SynthWave : uint8_t { SW_SAW = 0, SW_SQUARE, SW_TRI, SW_SINE, SW_PULSE, SW_NUM };
static const char* const SYNTH_WAVE_NAMES[SW_NUM] =
    { "Saw", "Square", "Tri", "Sine", "Pulse" };

struct SynthParams {
    uint8_t  wave    = SW_SAW;
    uint16_t cutoff  = 4000;   // filter cutoff Hz
    uint8_t  reso    = 40;     // 0..100 -> resonance
    uint16_t attack  = 4;      // ms
    uint16_t decay   = 180;    // ms
    uint8_t  sustain = 70;     // 0..100 %
    uint16_t release = 220;    // ms
    int8_t   octave  = 0;      // -3..+3 transpose
};

// ─── A pattern: all 17 tracks' 16 steps + the synth-track params snapshot ────
struct Pattern {
    Step        step[NUM_TRACKS][NUM_STEPS];
    uint8_t     length = NUM_STEPS;   // active steps (1..16)
    uint8_t     swing  = 0;           // 0..SWING_MAX %
};

// ─── The whole song held in RAM ─────────────────────────────────────────────
struct Song {
    TrackCfg    track[NUM_TRACKS];
    SynthParams synth;
    Pattern     pattern[NUM_PATTERNS];
    uint16_t    bpm = BPM_DEFAULT;
    // Song chain: ordered list of pattern indices (0xFF = end).
    uint8_t     chain[NUM_PATTERNS];
    uint8_t     chainLen = 1;         // 1 = single pattern loop
};

// Set up a sensible default song (a basic kit + a few melodic voices).
void songInitDefault(Song& s);
