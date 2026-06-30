#pragma once
// ============================================================================
//  Medusa GM  --  sequencer.h
//  17-track, 16-step grid sequencer.  All tracks share one step cursor.
//  Timing is micros()-based and survives the ~71 min rollover (signed diffs).
// ============================================================================
#include <Arduino.h>
#include "config.h"
#include "model.h"
#include "engine.h"

enum class PlayState : uint8_t { STOPPED, PLAYING };

class Sequencer {
public:
    void begin(Song* song, Engine* engine);

    void play();
    void stop();
    void togglePlay();
    PlayState state() const { return _state; }
    bool playing() const { return _state == PlayState::PLAYING; }

    void update();                       // call every loop()

    void setBPM(uint16_t bpm);
    uint16_t bpm() const { return _song->bpm; }
    void nudgeBPM(int d) { setBPM((int)_song->bpm + d); }

    uint8_t  curStep()    const { return _step; }
    uint8_t  curPattern() const { return _patIdx; }
    void     setPattern(uint8_t idx);

    // live "play this step now" for record/preview from the UI
    void triggerStepNow(uint8_t track);

private:
    Song*   _song   = nullptr;
    Engine* _engine = nullptr;
    PlayState _state = PlayState::STOPPED;

    uint8_t  _patIdx = 0;
    uint8_t  _step   = 0;
    uint8_t  _chainPos = 0;

    unsigned long _stepIntervalUs = 0;
    unsigned long _lastStepUs = 0;

    // per-track sustained note bookkeeping for note-off / ties
    uint8_t  _activeNote[NUM_TRACKS];
    bool     _noteOn[NUM_TRACKS];
    unsigned long _gateOffUs[NUM_TRACKS];

    void computeInterval();
    void doStep(uint8_t s);
    void advance();
    void allTrackNotesOff();
    Pattern& pat() { return _song->pattern[_patIdx]; }
};
