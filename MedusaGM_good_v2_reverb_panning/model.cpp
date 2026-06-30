#include "model.h"

static void putStep(Pattern& p, uint8_t trk, uint8_t s, uint8_t note, uint8_t vel,
                    uint8_t flags = STEP_ON) {
    p.step[trk][s].note = note;
    p.step[trk][s].vel  = vel;
    p.step[trk][s].flags = flags;
}

void songInitDefault(Song& s) {
    s = Song();   // reset to member defaults

    // ── Track instrument assignments ────────────────────────────────────────
    // Fields: { kind, program, drumNote, volume, pan(0=L,64=C,127=R), reverb, mute }
    // The default song is pre-panned into a stereo image; reverb sends are tuned
    // for a tasteful mix (they only audibly apply when FX are enabled).
    // Melodic voices
    s.track[0] = { TK_GM_MELODIC, 0,  36, 100, 64, 24, false };  // Piano  (center)
    s.track[1] = { TK_GM_MELODIC, 33, 36, 110, 64,  0, false };  // Bass   (center, dry)
    s.track[2] = { TK_GM_MELODIC, 48, 36,  80, 48, 52, false };  // Strings(left, hall)
    s.track[3] = { TK_GM_MELODIC, 80, 36,  85, 82, 40, false };  // Lead   (right)
    for (int t = 4; t < 9; t++)
        s.track[t] = { TK_GM_MELODIC, (uint8_t)(t*4), 36, 90,
                       (uint8_t)(40 + (t-4)*12), 30, false };     // spread L->R

    // Drum lanes (each track = one drum sound), panned like a kit
    s.track[9]  = { TK_GM_DRUM, 0, 36, 120, 64,  0, false };  // Kick   (center, dry)
    s.track[10] = { TK_GM_DRUM, 0, 38, 110, 64, 18, false };  // Snare  (center)
    s.track[11] = { TK_GM_DRUM, 0, 42,  90, 88,  8, false };  // CHat   (right)
    s.track[12] = { TK_GM_DRUM, 0, 46,  90, 40, 14, false };  // OHat   (left)
    s.track[13] = { TK_GM_DRUM, 0, 39, 100, 54, 22, false };  // Clap
    s.track[14] = { TK_GM_DRUM, 0, 49,  90, 30, 44, false };  // Crash  (wide left)
    s.track[15] = { TK_GM_DRUM, 0, 37, 100, 96, 10, false };  // Rim    (right)

    // Synth track (center, lush)
    s.track[SYNTH_TRACK] = { TK_SYNTH, 0, 36, 95, 64, 40, false };
    s.synth = SynthParams();

    // ── Pattern 0: a basic groove so it makes sound out of the box ──────────
    Pattern& p = s.pattern[0];
    p.length = 16;
    // Kick on quarters
    for (int s4 = 0; s4 < 16; s4 += 4) putStep(p, 9, s4, 36, 120);
    // Snare/clap on 2 & 4
    putStep(p, 10, 4, 38, 110); putStep(p, 10, 12, 38, 110);
    putStep(p, 13, 4, 39, 95);  putStep(p, 13, 12, 39, 95);
    // Closed hats on 8ths
    for (int s2 = 0; s2 < 16; s2 += 2) putStep(p, 11, s2, 42, 80);
    // Open hat on offbeats
    putStep(p, 12, 2, 46, 70); putStep(p, 12, 10, 46, 70);
    // Bass line (root - fifth feel around A1/E2)
    const uint8_t bass[16] = {33,0,33,0, 33,0,40,0, 33,0,33,0, 36,0,40,0};
    for (int i = 0; i < 16; i++) if (bass[i]) putStep(p, 1, i, bass[i], 110);
    // Piano stabs
    putStep(p, 0, 0, 60, 90); putStep(p, 0, 8, 64, 90);

    s.bpm = BPM_DEFAULT;
    s.chainLen = 1;
    s.chain[0] = 0;
    for (int i = 1; i < NUM_PATTERNS; i++) s.chain[i] = 0xFF;
}
