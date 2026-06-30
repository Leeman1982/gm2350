#include "ui.h"
#include "medusa_gm.h"
#include <U8g2lib.h>
#include <Wire.h>

// AMY exposes the number of baked PCM presets in the linked library.  Shown on
// the boot splash so a wrong/!right AMY library is visible at a glance
// (147 = the Medusa GM bank; a small number = the stock AMY library).
extern "C" const uint16_t pcm_samples;

// Full-frame buffer SSD1309 2.42" 128x64 over hardware I2C.
static U8G2_SSD1309_128X64_NONAME0_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);

// ─── note / drum naming ─────────────────────────────────────────────────────
static const char* NOTE_NAMES[12] =
    {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};

static void noteName(uint8_t n, char* buf, size_t sz) {
    snprintf(buf, sz, "%s%d", NOTE_NAMES[n % 12], (int)(n / 12) - 1);
}

struct DrumName { uint8_t note; const char* name; };
static const DrumName DRUM_NAMES[] = {
    {35,"Kick2"},{36,"Kick"},{37,"Rim"},{38,"Snare"},{39,"Clap"},{40,"Snare2"},
    {41,"LoTom"},{42,"CHat"},{43,"Tom"},{44,"PedHat"},{45,"Tom2"},{46,"OHat"},
    {47,"MTom"},{48,"HiTom"},{49,"Crash"},{50,"HiTom2"},{51,"Ride"},{52,"China"},
    {53,"Bell"},{54,"Tamb"},{55,"Splash"},{56,"Cowbl"},{57,"Crash2"},{59,"Ride2"},
    {60,"HiBongo"},{61,"LoBongo"},{62,"MConga"},{63,"HiConga"},{64,"LoConga"},
};
static const char* drumName(uint8_t note) {
    for (auto& d : DRUM_NAMES) if (d.note == note) return d.name;
    return "Perc";
}

// ─── lifecycle ──────────────────────────────────────────────────────────────
void UI::begin(Song* song, Sequencer* seq, Engine* eng, Controls* ctl, Storage* st) {
    _song = song; _seq = seq; _eng = eng; _ctl = ctl; _st = st;
    // Route I2C0 to the OLED pins before U8g2 brings Wire up.
    Wire.setSDA(PIN_OLED_SDA);
    Wire.setSCL(PIN_OLED_SCL);
    Wire.begin();
    oled.setI2CAddress(OLED_ADDR << 1);   // U8g2 wants the 8-bit address
    oled.begin();
    oled.setBusClock(OLED_I2C_HZ);        // Fast-Mode-Plus

    // Boot splash — proves the panel is wired/addressed correctly before any
    // sequencer state matters.  If you see this, the OLED is good.
    oled.clearBuffer();
    oled.setFont(u8g2_font_6x10_tr);
    oled.drawStr(28, 26, "MEDUSA GM");
    char b[24];
    snprintf(b, sizeof(b), "PCM bank: %u", (unsigned)pcm_samples);
    oled.setFont(u8g2_font_5x7_tr);
    oled.drawStr(28, 42, b);
    oled.sendBuffer();
}

void UI::toast(const char* msg) {
    strncpy(_toast, msg, sizeof(_toast) - 1);
    _toast[sizeof(_toast) - 1] = 0;
    _toastUntil = millis() + 1100;
}

void UI::nextTrack(int d) {
    int t = (int)_track + d;
    while (t < 0) t += NUM_TRACKS;
    _track = t % NUM_TRACKS;
    if (_cursor >= NUM_STEPS) _cursor = NUM_STEPS - 1;
}

const char* UI::trackLabel(uint8_t t, char* buf, size_t n) {
    const TrackCfg& c = _song->track[t];
    if (c.kind == TK_SYNTH)      snprintf(buf, n, "SYNTH");
    else if (c.kind == TK_GM_DRUM) snprintf(buf, n, "%s", drumName(c.drumNote));
    else                          snprintf(buf, n, "%s", GM_NAMES[c.program & 0x7F]);
    return buf;
}

// ─── input dispatch ─────────────────────────────────────────────────────────
void UI::handleInput() {
    Controls& c = *_ctl;
    bool shift = c.shift.isHeld();
    int enc = c.encoder.getDelta();

    // global transport
    if (c.play.wasPressed()) { if (shift) _seq->stop(); else _seq->togglePlay(); }

    // view / track navigation
    if (c.page.wasPressed()) {
        int v = (int)_view + (shift ? -1 : 1);
        v = (v + (int)View::NUM) % (int)View::NUM;
        _view = (View)v; _field = 0;
    }
    if (c.track.wasPressed()) nextTrack(shift ? -1 : 1);

    // SHIFT + rotate = tempo, everywhere
    if (shift && enc) { _seq->nudgeBPM(enc); enc = 0; }

    switch (_view) {
        case View::STEP:  onStepView(enc, shift, c);  break;
        case View::INST:  onInstView(enc, shift, c);  break;
        case View::SYNTH: onSynthView(enc, shift, c); break;
        case View::SONG:  onSongView(enc, shift, c);  break;
        default: break;
    }
}

// ─── STEP grid view ─────────────────────────────────────────────────────────
void UI::onStepView(int enc, bool shift, Controls& c) {
    Pattern& p = _song->pattern[_seq->curPattern()];
    Step& st = p.step[_track][_cursor];
    bool drum = _song->track[_track].kind == TK_GM_DRUM;

    if (enc) {
        if (c.rec.isHeld()) {
            // edit cursor step value: note (melodic) or velocity (drum)
            if (drum) st.vel = constrain((int)st.vel + enc, 1, 127);
            else      st.note = constrain((int)st.note + enc, 0, 127);
            st.flags |= STEP_ON;
            _recEditing = true;        // suppress the accent toggle on release
        } else {
            _cursor = (uint8_t)((_cursor + enc + NUM_STEPS * 4) % NUM_STEPS);
        }
    }
    if (c.encoder.wasPressed())   st.flags ^= STEP_ON;           // toggle step
    if (c.encoder.wasLongPress()) {                              // clear row
        for (int i = 0; i < NUM_STEPS; i++) p.step[_track][i].flags = 0;
        toast("row cleared");
    }
    if (c.rec.wasPressed()) {                                    // accent / tie
        if (_recEditing) _recEditing = false;                   // was an edit, not a tap
        else {
            if (shift) st.flags ^= STEP_TIE; else st.flags ^= STEP_ACCENT;
            st.flags |= STEP_ON;
        }
    }
}

// ─── INSTRUMENT view ────────────────────────────────────────────────────────
void UI::onInstView(int enc, bool shift, Controls& c) {
    TrackCfg& cfg = _song->track[_track];
    const uint8_t NFIELDS = 6;   // kind, program/drum, volume, pan, reverb, mute
    if (c.encoder.wasPressed()) _field = (_field + 1) % NFIELDS;
    if (enc) {
        switch (_field) {
            case 0: { int k = (int)cfg.kind + enc;
                      if (_track == SYNTH_TRACK) cfg.kind = TK_SYNTH;          // locked
                      else cfg.kind = (TrackKind)constrain(k, 0, 1); } break;  // melodic/drum
            case 1: if (cfg.kind == TK_GM_DRUM)
                        cfg.drumNote = constrain((int)cfg.drumNote + enc, 27, 87);
                    else
                        cfg.program  = constrain((int)cfg.program + enc, 0, 127);
                    break;
            case 2: cfg.volume = constrain((int)cfg.volume + enc * 4, 0, 127); break;
            case 3: cfg.pan    = constrain((int)cfg.pan + enc * 4, 0, 127); break;
            case 4: cfg.reverb = constrain((int)cfg.reverb + enc * 4, 0, 127); break;
            case 5: if (enc) cfg.mute = !cfg.mute; break;
        }
    }
    if (c.rec.wasPressed())                                       // audition
        _eng->preview(cfg, _song->synth, 60);
}

// ─── SYNTH view ─────────────────────────────────────────────────────────────
void UI::onSynthView(int enc, bool shift, Controls& c) {
    SynthParams& s = _song->synth;
    const uint8_t NFIELDS = 8;   // wave,cutoff,reso,atk,dec,sus,rel,oct
    if (c.encoder.wasPressed()) _field = (_field + 1) % NFIELDS;
    if (enc) {
        switch (_field) {
            case 0: s.wave    = (uint8_t)((s.wave + enc + SW_NUM) % SW_NUM); break;
            case 1: s.cutoff  = constrain((int)s.cutoff + enc * 100, 60, 16000); break;
            case 2: s.reso    = constrain((int)s.reso + enc * 2, 0, 100); break;
            case 3: s.attack  = constrain((int)s.attack + enc * 2, 1, 2000); break;
            case 4: s.decay   = constrain((int)s.decay + enc * 5, 1, 3000); break;
            case 5: s.sustain = constrain((int)s.sustain + enc * 2, 0, 100); break;
            case 6: s.release = constrain((int)s.release + enc * 5, 1, 3000); break;
            case 7: s.octave  = constrain((int)s.octave + enc, -3, 3); break;
        }
    }
    if (c.rec.wasPressed()) {
        TrackCfg tmp{ TK_SYNTH, 0, 36, 110, 64, 30, false };  // center pan, some reverb
        _eng->preview(tmp, s, 60);
    }
}

// ─── SONG view (pattern / chain / persistence) ──────────────────────────────
void UI::onSongView(int enc, bool shift, Controls& c) {
    const uint8_t NFIELDS = 5;   // pattern, length, swing, chainLen, slot
    if (c.encoder.wasPressed()) _field = (_field + 1) % NFIELDS;
    Pattern& p = _song->pattern[_editPat];
    if (enc) {
        switch (_field) {
            case 0: _editPat = (uint8_t)((_editPat + enc + NUM_PATTERNS) % NUM_PATTERNS);
                    _seq->setPattern(_editPat); break;
            case 1: p.length = constrain((int)p.length + enc, 1, NUM_STEPS); break;
            case 2: p.swing  = constrain((int)p.swing + enc, 0, SWING_MAX); break;
            case 3: _song->chainLen = constrain((int)_song->chainLen + enc, 1, NUM_PATTERNS); break;
            case 4: _slot = (uint8_t)((_slot + enc + Storage::NUM_SLOTS) % Storage::NUM_SLOTS); break;
        }
    }
    if (c.rec.wasPressed()) {                 // REC saves, SHIFT+REC loads
        if (shift) {
            if (_st->loadSong(_slot, *_song)) { _seq->setBPM(_song->bpm); toast("loaded"); }
            else toast("empty slot");
        } else {
            toast(_st->saveSong(_slot, *_song) ? "saved" : "save fail");
        }
    }
}

// ─── drawing ────────────────────────────────────────────────────────────────
void UI::drawHeader() {
    char buf[18];
    oled.setFont(u8g2_font_5x7_tr);
    // transport + bpm + pattern
    oled.drawStr(0, 7, _seq->playing() ? ">" : "#");
    snprintf(buf, sizeof(buf), "%uBPM", (unsigned)_seq->bpm());
    oled.drawStr(8, 7, buf);
    snprintf(buf, sizeof(buf), "P%u", (unsigned)_seq->curPattern() + 1);
    oled.drawStr(52, 7, buf);
    // track + instrument name
    char lbl[16]; trackLabel(_track, lbl, sizeof(lbl));
    snprintf(buf, sizeof(buf), "T%02u %s", (unsigned)_track + 1, lbl);
    oled.drawStr(72, 7, buf);
    oled.drawHLine(0, 9, 128);
}

void UI::drawStepView() {
    Pattern& p = _song->pattern[_seq->curPattern()];
    uint8_t playhead = _seq->curStep();
    // 16 steps -> 2 rows x 8 cols
    const int x0 = 2, y0 = 14, cw = 15, ch = 22, gap = 1;
    for (int i = 0; i < NUM_STEPS; i++) {
        int col = i % 8, row = i / 8;
        int x = x0 + col * cw;
        int y = y0 + row * (ch + gap);
        const Step& st = p.step[_track][i];
        bool on = stepOn(st);
        bool beyond = i >= p.length;
        if (on) oled.drawBox(x, y, cw - 2, ch - 4);
        else    oled.drawFrame(x, y, cw - 2, ch - 4);
        if (beyond) oled.drawHLine(x, y + ch - 3, cw - 2);  // out-of-length marker
        // accent dot / tie bar
        if (stepAccent(st)) oled.drawDisc(x + cw - 5, y + 2, 1);
        if (stepTie(st))    oled.drawHLine(x, y + (ch - 4) / 2, cw - 2);
        // cursor
        if (i == _cursor) oled.drawFrame(x - 1, y - 1, cw, ch - 2);
        // playhead
        if (_seq->playing() && i == playhead) oled.drawBox(x + 2, y + ch - 4, cw - 6, 2);
    }
    // cursor step detail line
    char buf[26], nn[8];
    const Step& cs = p.step[_track][_cursor];
    if (_song->track[_track].kind == TK_GM_DRUM)
        snprintf(buf, sizeof(buf), "St%02u v%u%s%s", _cursor + 1, cs.vel,
                 stepAccent(cs) ? " A" : "", stepOn(cs) ? "" : " -");
    else {
        noteName(cs.note, nn, sizeof(nn));
        snprintf(buf, sizeof(buf), "St%02u %s v%u%s", _cursor + 1, nn, cs.vel,
                 stepTie(cs) ? " ~" : "");
    }
    oled.setFont(u8g2_font_4x6_tr);
    oled.drawStr(2, 63, buf);
}

void UI::drawInstView() {
    TrackCfg& cfg = _song->track[_track];
    oled.setFont(u8g2_font_6x10_tr);
    const char* kindStr = cfg.kind == TK_SYNTH ? "Synth" :
                          cfg.kind == TK_GM_DRUM ? "Drum" : "Melodic";
    char rows[6][24];
    snprintf(rows[0], 24, "Kind:  %s", kindStr);
    if (cfg.kind == TK_GM_DRUM) snprintf(rows[1], 24, "Drum:  %s", drumName(cfg.drumNote));
    else if (cfg.kind == TK_SYNTH) snprintf(rows[1], 24, "Patch: (synth view)");
    else snprintf(rows[1], 24, "Prog:  %s", GM_NAMES[cfg.program & 0x7F]);
    snprintf(rows[2], 24, "Vol:   %u", cfg.volume);
    int pv = (int)cfg.pan - 64;                    // -64..+63, 0 = center
    if (pv == 0)      snprintf(rows[3], 24, "Pan:   C");
    else if (pv < 0)  snprintf(rows[3], 24, "Pan:   L%d", -pv);
    else              snprintf(rows[3], 24, "Pan:   R%d", pv);
    snprintf(rows[4], 24, "Rev:   %u", cfg.reverb);
    snprintf(rows[5], 24, "Mute:  %s", cfg.mute ? "YES" : "no");
    for (int i = 0; i < 6; i++) {
        int y = 18 + i * 8;
        if (i == _field) { oled.drawBox(0, y - 7, 128, 9); oled.setDrawColor(0); }
        oled.drawStr(3, y, rows[i]);
        oled.setDrawColor(1);
    }
}

void UI::drawSynthView() {
    SynthParams& s = _song->synth;
    char rows[8][20];
    snprintf(rows[0], 20, "Wave %s", SYNTH_WAVE_NAMES[s.wave]);
    snprintf(rows[1], 20, "Cut  %u", s.cutoff);
    snprintf(rows[2], 20, "Res  %u", s.reso);
    snprintf(rows[3], 20, "Atk  %u", s.attack);
    snprintf(rows[4], 20, "Dec  %u", s.decay);
    snprintf(rows[5], 20, "Sus  %u", s.sustain);
    snprintf(rows[6], 20, "Rel  %u", s.release);
    snprintf(rows[7], 20, "Oct  %+d", s.octave);
    oled.setFont(u8g2_font_5x7_tr);
    for (int i = 0; i < 8; i++) {
        int col = i / 4, row = i % 4;
        int x = col * 64, y = 20 + row * 11;
        if (i == _field) { oled.drawBox(x, y - 7, 64, 10); oled.setDrawColor(0); }
        oled.drawStr(x + 2, y, rows[i]);
        oled.setDrawColor(1);
    }
}

void UI::drawSongView() {
    Pattern& p = _song->pattern[_editPat];
    char rows[5][22];
    snprintf(rows[0], 22, "Edit Pattern: %u", _editPat + 1);
    snprintf(rows[1], 22, "Length: %u", p.length);
    snprintf(rows[2], 22, "Swing:  %u%%", p.swing);
    snprintf(rows[3], 22, "Chain:  %u pat", _song->chainLen);
    snprintf(rows[4], 22, "Slot %u  REC=save", _slot + 1);
    oled.setFont(u8g2_font_6x10_tr);
    for (int i = 0; i < 5; i++) {
        int y = 20 + i * 9;
        if (i == _field) { oled.drawBox(0, y - 7, 128, 9); oled.setDrawColor(0); }
        oled.drawStr(3, y, rows[i]);
        oled.setDrawColor(1);
    }
}

void UI::render() {
    uint32_t now = millis();
    // ~20 fps cap.  A full I2C frame blocks this (audio) core ~10 ms, so keeping
    // the rate modest leaves the deepened audio buffer plenty of refill time.
    if (now - _lastDraw < 50) return;
    _lastDraw = now;

    oled.clearBuffer();
    drawHeader();
    switch (_view) {
        case View::STEP:  drawStepView();  break;
        case View::INST:  drawInstView();  break;
        case View::SYNTH: drawSynthView(); break;
        case View::SONG:  drawSongView();  break;
        default: break;
    }
    if (_toast[0] && now < _toastUntil) {
        oled.setFont(u8g2_font_6x10_tr);
        int w = strlen(_toast) * 6 + 6;
        oled.setDrawColor(0); oled.drawBox(64 - w/2, 26, w, 13); oled.setDrawColor(1);
        oled.drawFrame(64 - w/2, 26, w, 13);
        oled.drawStr(64 - w/2 + 3, 36, _toast);
    } else if (_toast[0] && now >= _toastUntil) _toast[0] = 0;

    oled.sendBuffer();
}
