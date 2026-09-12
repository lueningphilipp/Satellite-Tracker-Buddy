#pragma once
#include <Arduino.h>
#include <time.h>

// Ring buffer of recent (lat, lon) samples, sampled at period/300 (per
// CLAUDE.md: "Trail sampling interval... must scale with period"), so a GEO
// object's multi-hour period doesn't fill the buffer in minutes while a LEO
// object's ~90 min period doesn't leave it nearly empty. Capacity 300
// matches that formula's own denominator - a full buffer always covers
// ~1 period, regardless of orbit.
class TrailBuffer {
public:
    static const int CAPACITY = 300;

    void clear() { count_ = 0; head_ = 0; nextSampleAt_ = 0; }

    // Sets the sampling interval from the current orbit's period; call this
    // whenever the tracked satellite changes (elements refetched/reinit'd).
    void setPeriodMinutes(double periodMin) {
        intervalSec_ = (periodMin * 60.0) / 300.0;
        if (intervalSec_ < 1.0) intervalSec_ = 1.0;   // sane floor
    }

    // Call every loop tick with the latest known position; only actually
    // records a sample once `intervalSec_` has passed since the last one.
    void maybeSample(time_t now, double lat, double lon) {
        if (now < nextSampleAt_) return;
        nextSampleAt_ = now + (time_t)intervalSec_;
        lat_[head_] = lat; lon_[head_] = lon;
        head_ = (head_ + 1) % CAPACITY;
        if (count_ < CAPACITY) count_++;
    }

    int count() const { return count_; }
    // Index 0 = oldest sample, count()-1 = newest.
    void get(int i, double& lat, double& lon) const {
        int idx = (head_ - count_ + i + CAPACITY) % CAPACITY;
        lat = lat_[idx]; lon = lon_[idx];
    }

private:
    double lat_[CAPACITY], lon_[CAPACITY];
    int head_ = 0, count_ = 0;
    double intervalSec_ = 60.0;
    time_t nextSampleAt_ = 0;
};
