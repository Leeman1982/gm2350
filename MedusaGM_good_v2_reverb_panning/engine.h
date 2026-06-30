#pragma once
// ============================================================================
//  Medusa GM  --  engine.h
//  AMY voice engine: owns a pool of AMY oscillators and turns sequencer note
//  events into AMY PCM (GM / drum) or subtractive-synth voices.
//
//  We drive AMY oscillators directly (no synth/instrument layer) so we have
//  deterministic control of the pool and of voice stealing.
// ============================================================================
#include <Arduino.h>
#include "config.h"
#include "model.h"

class Engine {
public:
    void begin();
    bool bankOK() const;             // false if wrong AMY library (too few presets)
    void setMasterVolume(float v);   // 0..~2
    void setupEffects();             // global stereo reverb + chorus (if MEDUSA_FX)

    // Note lifecycle.  `track` indexes Song::track[], `cfg`/`syn` supply timbre.
    void noteOn(uint8_t track, const TrackCfg& cfg, const SynthParams& syn,
                uint8_t note, uint8_t vel, bool accent);
    void noteOff(uint8_t track, uint8_t note);
    void allNotesOff();

    // Audition a single preview note (used by the UI when browsing instruments).
    void preview(const TrackCfg& cfg, const SynthParams& syn, uint8_t note);

private:
    struct Voice {
        uint16_t osc;
        bool     active     = false;   // sounding (pre note-off)
        bool     released   = false;   // note-off sent, tail ringing
        uint8_t  track      = 0xFF;
        uint8_t  note       = 0;
        uint32_t stamp      = 0;       // millis() of last state change
    };
    Voice   _v[POLYPHONY];
    uint32_t _seq = 0;

    int  allocVoice();
    void configMelodic(uint16_t osc, uint8_t program, uint8_t note,
                       uint8_t vel, uint8_t pan, uint8_t bus);
    void configDrum(uint16_t osc, uint8_t drumNote, uint8_t vel,
                    uint8_t pan, uint8_t bus);
    void configSynth(uint16_t osc, const SynthParams& syn, uint8_t note,
                     uint8_t vel, uint8_t pan, uint8_t bus);
    void sendOff(uint16_t osc);
};
