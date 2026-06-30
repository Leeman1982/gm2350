#pragma once
// ============================================================================
//  Medusa GM  --  storage.h
//  Persist the whole Song to the RP2350's on-chip flash via LittleFS.
//  (Arduino IDE: pick a "Flash Size" option that reserves an FS partition.)
// ============================================================================
#include <Arduino.h>
#include "model.h"

class Storage {
public:
    bool begin();                       // mount LittleFS
    bool saveSong(uint8_t slot, const Song& s);
    bool loadSong(uint8_t slot, Song& s);
    bool slotExists(uint8_t slot);
    static const uint8_t NUM_SLOTS = 8;
private:
    bool _mounted = false;
    void path(uint8_t slot, char* buf, size_t n);
};
