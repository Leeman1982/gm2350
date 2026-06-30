#include "storage.h"
#include <LittleFS.h>

// On-disk header so we can reject incompatible blobs.
struct SongFileHdr {
    uint16_t magic;
    uint8_t  version;
    uint8_t  reserved;
    uint32_t bytes;     // sizeof(Song)
};

bool Storage::begin() {
    _mounted = LittleFS.begin();
    return _mounted;
}

void Storage::path(uint8_t slot, char* buf, size_t n) {
    snprintf(buf, n, "/song%u.bin", (unsigned)slot);
}

bool Storage::saveSong(uint8_t slot, const Song& s) {
    if (!_mounted || slot >= NUM_SLOTS) return false;
    char p[24]; path(slot, p, sizeof(p));
    File f = LittleFS.open(p, "w");
    if (!f) return false;
    SongFileHdr h{ STORAGE_MAGIC, STORAGE_VERSION, 0, (uint32_t)sizeof(Song) };
    f.write((const uint8_t*)&h, sizeof(h));
    f.write((const uint8_t*)&s, sizeof(Song));
    f.close();
    return true;
}

bool Storage::loadSong(uint8_t slot, Song& s) {
    if (!_mounted || slot >= NUM_SLOTS) return false;
    char p[24]; path(slot, p, sizeof(p));
    File f = LittleFS.open(p, "r");
    if (!f) return false;
    SongFileHdr h{};
    if (f.read((uint8_t*)&h, sizeof(h)) != sizeof(h) ||
        h.magic != STORAGE_MAGIC || h.version != STORAGE_VERSION ||
        h.bytes != sizeof(Song)) { f.close(); return false; }
    bool ok = f.read((uint8_t*)&s, sizeof(Song)) == (int)sizeof(Song);
    f.close();
    return ok;
}

bool Storage::slotExists(uint8_t slot) {
    if (!_mounted || slot >= NUM_SLOTS) return false;
    char p[24]; path(slot, p, sizeof(p));
    return LittleFS.exists(p);
}
