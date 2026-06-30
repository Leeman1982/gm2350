#pragma once
// ============================================================================
//  Medusa GM  --  ui.h
//  2.42" SSD1309 OLED interface driven by the encoder + 5 buttons.
//
//  Control scheme (SHIFT = hold the SHIFT button):
//    PLAY              play / stop          SHIFT+PLAY   stop & rewind
//    PAGE              next view            SHIFT+PAGE   previous view
//    TRACK             next track           SHIFT+TRACK  previous track
//    encoder rotate    context value / cursor
//    encoder push      context action (toggle step / next field)
//    encoder long      clear current track row
//    SHIFT + rotate    tempo (BPM)
//    REC (hold)+rotate edit cursor step note (melodic) / velocity (drum)
//    REC (tap)         toggle accent        SHIFT+REC    toggle tie
//  Views: STEP grid, INSTRUMENT, SYNTH, SONG (pattern/chain/save-load).
// ============================================================================
#include <Arduino.h>
#include "config.h"
#include "model.h"
#include "sequencer.h"
#include "engine.h"
#include "controls.h"
#include "storage.h"

enum class View : uint8_t { STEP, INST, SYNTH, SONG, NUM };

class UI {
public:
    void begin(Song* song, Sequencer* seq, Engine* eng, Controls* ctl, Storage* st);
    void handleInput();      // process controls
    void render();           // draw (call often; self-throttles)

private:
    Song*      _song = nullptr;
    Sequencer* _seq  = nullptr;
    Engine*    _eng  = nullptr;
    Controls*  _ctl  = nullptr;
    Storage*   _st   = nullptr;

    View    _view = View::STEP;
    uint8_t _track = 0;       // selected track
    uint8_t _cursor = 0;      // selected step (STEP view)
    uint8_t _field = 0;       // selected field (INST/SYNTH/SONG views)
    uint8_t _editPat = 0;     // pattern being edited
    uint8_t _slot = 0;        // save/load slot
    bool    _recEditing = false; // REC held + rotated (suppresses the tap action)
    char    _toast[22] = {0};
    uint32_t _toastUntil = 0;
    uint32_t _lastDraw = 0;

    void toast(const char* msg);
    void nextTrack(int d);
    void onStepView(int enc, bool shift, Controls& c);
    void onInstView(int enc, bool shift, Controls& c);
    void onSynthView(int enc, bool shift, Controls& c);
    void onSongView(int enc, bool shift, Controls& c);

    void drawHeader();
    void drawStepView();
    void drawInstView();
    void drawSynthView();
    void drawSongView();

    const char* trackLabel(uint8_t t, char* buf, size_t n);
};
