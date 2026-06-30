#include "sequencer.h"

void Sequencer::begin(Song* song, Engine* engine) {
    _song = song; _engine = engine;
    computeInterval();
    for (int t = 0; t < NUM_TRACKS; t++) { _noteOn[t] = false; _activeNote[t] = 0; _gateOffUs[t] = 0; }
}

void Sequencer::computeInterval() {
    _stepIntervalUs = 15000000UL / constrain((int)_song->bpm, BPM_MIN, BPM_MAX);
}

void Sequencer::setBPM(uint16_t bpm) {
    _song->bpm = constrain((int)bpm, BPM_MIN, BPM_MAX);
    computeInterval();
}

void Sequencer::setPattern(uint8_t idx) {
    if (idx >= NUM_PATTERNS) return;
    allTrackNotesOff();
    _patIdx = idx;
}

void Sequencer::play() {
    if (_state == PlayState::PLAYING) return;
    _state = PlayState::PLAYING;
    _step = 0; _chainPos = 0;
    _patIdx = _song->chain[0] < NUM_PATTERNS ? _song->chain[0] : 0;
    _lastStepUs = micros();
    doStep(0);
    advance();
}

void Sequencer::stop() {
    _state = PlayState::STOPPED;
    allTrackNotesOff();
    _engine->allNotesOff();
    _step = 0;
}

void Sequencer::togglePlay() { if (playing()) stop(); else play(); }

void Sequencer::allTrackNotesOff() {
    for (int t = 0; t < NUM_TRACKS; t++) {
        if (_noteOn[t]) { _engine->noteOff(t, _activeNote[t]); _noteOn[t] = false; }
    }
}

// Fire all active steps for cursor position `s`.
void Sequencer::doStep(uint8_t s) {
    Pattern& p = pat();
    for (int t = 0; t < NUM_TRACKS; t++) {
        const TrackCfg& cfg = _song->track[t];
        if (cfg.mute) { if (_noteOn[t]) { _engine->noteOff(t, _activeNote[t]); _noteOn[t] = false; } continue; }
        const Step& st = p.step[t][s];
        if (!stepOn(st)) {
            // release a held (non-tied) note from a previous step
            if (_noteOn[t]) { _engine->noteOff(t, _activeNote[t]); _noteOn[t] = false; }
            continue;
        }
        bool tiedSame = stepTie(st) && _noteOn[t] && _activeNote[t] == st.note;
        if (_noteOn[t] && !tiedSame) { _engine->noteOff(t, _activeNote[t]); _noteOn[t] = false; }

        if (!tiedSame) {
            _engine->noteOn(t, cfg, _song->synth, st.note, st.vel, stepAccent(st));
            _activeNote[t] = st.note;
            _noteOn[t] = true;
        }
        // gate: 50% of step (drums ignore -- their envelope is one-shot)
        unsigned long gate = stepTie(st) ? _stepIntervalUs : (_stepIntervalUs / 2);
        _gateOffUs[t] = _lastStepUs + gate;
    }
}

void Sequencer::advance() {
    Pattern& p = pat();
    uint8_t len = constrain((int)p.length, 1, NUM_STEPS);
    _step = (_step + 1) % len;
    if (_step == 0 && _song->chainLen > 1) {
        _chainPos = (_chainPos + 1) % _song->chainLen;
        uint8_t next = _song->chain[_chainPos];
        if (next < NUM_PATTERNS) _patIdx = next;
    }
}

void Sequencer::triggerStepNow(uint8_t track) {
    if (track >= NUM_TRACKS) return;
    const Step& st = pat().step[track][_step];
    _engine->noteOn(track, _song->track[track], _song->synth, st.note, st.vel, stepAccent(st));
}

void Sequencer::update() {
    if (_state != PlayState::PLAYING) return;
    unsigned long now = micros();
    Pattern& p = pat();

    // gate-offs (skip drum tracks: one-shot)
    for (int t = 0; t < NUM_TRACKS; t++) {
        if (_noteOn[t] && _song->track[t].kind != TK_GM_DRUM &&
            (long)(now - _gateOffUs[t]) >= 0) {
            // don't cut a note that the upcoming step ties into
            uint8_t nextStep = (_step) % constrain((int)p.length, 1, NUM_STEPS);
            const Step& ns = p.step[t][nextStep];
            bool tieNext = stepOn(ns) && stepTie(ns) && ns.note == _activeNote[t];
            if (!tieNext) { _engine->noteOff(t, _activeNote[t]); _noteOn[t] = false; }
        }
    }

    // advance as many steps as have elapsed (robust to UI/render jitter)
    int guard = NUM_STEPS;
    while (guard-- > 0) {
        long swingUs = (p.swing > 0) ? (long)(_stepIntervalUs * p.swing / 200UL) : 0L;
        unsigned long interval = (unsigned long)((long)_stepIntervalUs + ((_step & 1) ? swingUs : -swingUs));
        if ((long)(now - _lastStepUs) < (long)interval) break;
        _lastStepUs += interval;
        doStep(_step);
        advance();
    }
}
