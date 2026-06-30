// ============================================================================
//  Medusa GM  --  a 16-track General-MIDI groovebox + 1 AMY synth track
//  ---------------------------------------------------------------------------
//  Hardware:  Raspberry Pi Pico 2 (RP2350)  |  Arduino IDE w/ arduino-pico core
//             PCM5102 I2S DAC  |  2.42" SSD1309 OLED  |  rotary encoder + 5 buttons
//
//  Audio is produced by the AMY synthesizer library.  The 16 GM tracks play
//  samples from a SoundFont that has been baked into AMY's flash PCM ROM bank
//  by tools/sf2_to_amy.py; the 17th track is an AMY subtractive synth voice.
//
//  Build notes (Arduino IDE):
//    Board:        Raspberry Pi Pico 2  (Tools > board: "Raspberry Pi RP2350")
//    CPU Speed:    200 MHz or more (250 MHz recommended for full polyphony)
//    Optimize:     -O3 / "Fast"
//    Flash Size:   a layout that reserves an FS partition (for song save/load)
//    Libraries:    AMY (this repo's fork w/ the GM bank), U8g2
//  See README.md for full wiring and the control map.
// ============================================================================

#include <AMY-Arduino.h>

#include "config.h"
#include "model.h"
#include "engine.h"
#include "sequencer.h"
#include "controls.h"
#include "storage.h"
#include "ui.h"

Song      song;
Engine    engine;
Sequencer sequencer;
Controls  controls;
Storage   storage;
UI        ui;

// Cached at boot: is the 160-preset GM bank actually linked?  Drives the LED
// heartbeat so we can read the board's state even if the OLED is dead.
static bool g_bankOK = false;

// Heartbeat run from loop() — its mere presence proves we reached loop() and
// are NOT bricked or boot-looping.  Two distinct, *calm* patterns (never the
// old fast panic-blink):
//   bank OK    -> slow steady 1 Hz  (......on/off, like a resting pulse)
//   bank wrong -> "blip-blip ... pause"  (two quick blinks every 1.5 s)
static void ledHeartbeat(bool ok) {
#ifdef LED_BUILTIN
    uint32_t t = millis();
    bool on;
    if (ok) {
        on = (t % 1000) < 500;                       // 1 Hz steady
    } else {
        uint32_t p = t % 1500;                       // double-blip every 1.5 s
        on = (p < 110) || (p >= 250 && p < 360);
    }
    digitalWrite(LED_BUILTIN, on ? HIGH : LOW);
#endif
}

void setup() {
#ifdef LED_BUILTIN
    pinMode(LED_BUILTIN, OUTPUT);
    digitalWrite(LED_BUILTIN, HIGH);
#endif

    // ── Start AMY: stereo I2S out to the PCM5102, rendering across both cores.
    amy_config_t cfg = amy_default_config();
    cfg.audio              = AMY_AUDIO_IS_I2S;
    cfg.i2s_bclk           = PIN_I2S_BCLK;        // LRCLK is BCLK+1 (pico-audio)
    cfg.i2s_lrc            = PIN_I2S_BCLK + 1;
    cfg.i2s_dout           = PIN_I2S_DOUT;
    cfg.platform.multicore = 1;                   // use core1 for half the voices
    cfg.max_oscs           = AMY_MAX_OSCS;
    cfg.features.default_synths = 0;              // we drive oscillators ourselves
    cfg.features.startup_bleep  = 0;
    cfg.features.reverb    = MEDUSA_FX;            // stereo reverb (see config.h)
    cfg.features.chorus    = MEDUSA_FX;            // + chorus; set MEDUSA_FX 0 if CPU-bound
    cfg.features.echo      = 0;
    cfg.midi               = AMY_MIDI_IS_NONE;
    amy_start(cfg);

    // ── App modules.
    songInitDefault(song);
    storage.begin();
    engine.begin();
    sequencer.begin(&song, &engine);
    controls.begin();
    ui.begin(&song, &sequencer, &engine, &controls, &storage);

    // The firmware boots and runs no matter which AMY library is linked.  If the
    // GM bank isn't baked in (the stock Library-Manager AMY has only a handful of
    // ROM presets) you still get the full UI and sequencer; the GM voices just
    // fall back to whatever presets that library has.  The LED heartbeat and the
    // boot splash both report the linked preset count so a wrong library is
    // obvious without bricking anything.
    g_bankOK = engine.bankOK();

    // Global stereo reverb + chorus on the room/hall buses (no-op if MEDUSA_FX 0).
    engine.setupEffects();
}

void loop() {
    // Render + push one audio block to I2S (this paces the loop at block rate).
    amy_update();

    // Service controls, sequencer timing, and the display.
    controls.update();
    ui.handleInput();
    sequencer.update();
    ui.render();

    // Proof-of-life + bank status, visible even if the OLED is dead.
    ledHeartbeat(g_bankOK);
}
