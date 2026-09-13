#include "request_tracker.h"
#include <Preferences.h>
#include <time.h>
#include <string.h>   // memset

static const char* NVS_NS = "reqtrack";

RequestTracker celestrakRequests;

void RequestTracker::begin() {
    Preferences prefs;
    prefs.begin(NVS_NS, /*readOnly=*/true);
    count_ = prefs.getInt("count", 0);
    nextSlot_ = prefs.getInt("next", 0);
    size_t got = prefs.getBytes("ts", timestamps_, sizeof(timestamps_));
    prefs.end();

    // First boot (or a size mismatch after a firmware change) - start clean
    // rather than trust partially-read garbage.
    if (got != sizeof(timestamps_) || count_ < 0 || count_ > CAPACITY ||
        nextSlot_ < 0 || nextSlot_ >= CAPACITY) {
        count_ = 0;
        nextSlot_ = 0;
        memset(timestamps_, 0, sizeof(timestamps_));
    }
}

void RequestTracker::recordRequest() {
    timestamps_[nextSlot_] = (uint32_t)time(nullptr);
    nextSlot_ = (nextSlot_ + 1) % CAPACITY;
    if (count_ < CAPACITY) count_++;
    save();
}

void RequestTracker::save() {
    Preferences prefs;
    prefs.begin(NVS_NS, /*readOnly=*/false);
    prefs.putInt("count", count_);
    prefs.putInt("next", nextSlot_);
    prefs.putBytes("ts", timestamps_, sizeof(timestamps_));
    prefs.end();
}

int RequestTracker::countInLast2h() const {
    time_t now = time(nullptr);
    time_t cutoff = now - 2 * 3600;
    int n = 0;
    for (int i = 0; i < count_; i++) {
        if ((time_t)timestamps_[i] >= cutoff) n++;
    }
    return n;
}
