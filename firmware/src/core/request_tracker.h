#pragma once
#include <Arduino.h>

// Tracks outbound CelesTrak requests as a rolling 2-hour window, persisted
// across reboots (NVS) so a restart can't hide recent activity from the
// count - a fresh boot starting back at 0 would understate how close to
// CelesTrak's threshold recent activity actually got, e.g. right after the
// kind of rapid reflash/retest cycle that's *already* caused a real
// gp.php 403 earlier in this project (see CLAUDE.md's TODO).
//
// This is a visibility/diagnostic tool, not a hard safety mechanism by
// itself - the elements-fetch-interval floor (web_server.cpp's 10-minute
// clamp) already structurally keeps normal use far under CelesTrak's
// documented "50 HTTP error responses in 2h" firewall threshold (see the
// README's "Rate limits" section). This surfaces the actual count on the
// config page so that stays visibly true instead of just assumed.
//
// Counts every request made (success or failure), not just errors -
// CelesTrak's own policy counts error responses specifically, but this
// device has no way to know in advance which requests will error, so
// tracking total volume is the honest, conservative proxy.
class RequestTracker {
public:
    void begin();             // load persisted history from NVS
    void recordRequest();     // call once per outbound request, right after it completes
    int countInLast2h() const;

private:
    static const int CAPACITY = 64;   // comfortably more than CelesTrak's own 50-in-2h threshold
    uint32_t timestamps_[CAPACITY] = {0};
    int count_ = 0;      // how many of timestamps_[] are populated (<= CAPACITY)
    int nextSlot_ = 0;   // ring buffer write position
    void save();
};

extern RequestTracker celestrakRequests;
