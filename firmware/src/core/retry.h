#pragma once

// Shared retry rule for every outbound API call (CelesTrak elements,
// CelesTrak launch date, n2yo name): a first attempt plus at most
// MAX_RETRIES retries, RETRY_DELAY_MS apart, and only for failures that can
// plausibly clear up on their own. After that a job gives up until its next
// scheduled trigger. Pure logic (no Arduino/ESP headers) so it can be reasoned
// about - and unit-tested on a PC - in isolation from the network code.
//
// Why "transient only": a 404 (mistyped NORAD id) or a parse error will fail
// the same way every time, and each retry would be one more HTTP error
// counted against CelesTrak's 50-errors/2h firewall (see README's Rate
// limits) for nothing.

enum class FetchResult {
    Ok,
    Transient,   // connection/TLS error, HTTP 408/429/5xx - worth retrying
    Permanent,   // 404/403/other 4xx, unparseable data, no key - retrying can't help
};

// Maps an HTTPClient::GET() return value (negative = connection-level error,
// otherwise an HTTP status) to a FetchResult. 403 is deliberately Permanent:
// CelesTrak uses it for its IP block, and retrying 2 minutes later would
// just hit the same block - the normal schedule covers that case.
inline FetchResult classifyHttp(int code) {
    if (code == 200) return FetchResult::Ok;
    if (code <= 0 || code == 408 || code == 429 || code >= 500)
        return FetchResult::Transient;
    return FetchResult::Permanent;
}

// Schedule + retry state for one API call. The owner (main.cpp's
// serviceFetchJobs()) asks due() each loop() iteration, runs the fetch, then
// feeds the outcome to report(), which decides when (if ever) it runs again.
class FetchJob {
public:
    static constexpr int MAX_RETRIES = 2;
    static constexpr unsigned long RETRY_DELAY_MS = 2UL * 60UL * 1000UL;

    // Schedules an attempt `delayMs` from now with a fresh retry budget.
    void arm(unsigned long nowMs, unsigned long delayMs = 0) {
        armed_ = true;
        dueAtMs_ = nowMs + delayMs;
        retriesUsed_ = 0;
        failed_ = false;
    }

    // Cancels any pending attempt. Deliberately leaves failed_ alone - a job
    // that gave up on a transient error should still be revived by
    // onNetworkRestored().
    void disarm() { armed_ = false; }

    bool armed() const { return armed_; }

    // Signed subtraction so this stays correct across millis() rollover.
    bool due(unsigned long nowMs) const {
        return armed_ && (long)(nowMs - dueAtMs_) >= 0;
    }

    // Records an attempt's outcome and reschedules. `normalDelayMs` is when
    // to run again once this attempt is settled (success, permanent failure,
    // or retries exhausted); 0 makes it a one-shot job that disarms instead.
    // Returns true if a retry was scheduled (retriesUsed() is then 1..MAX).
    bool report(FetchResult r, unsigned long nowMs, unsigned long normalDelayMs) {
        failed_ = (r == FetchResult::Transient);
        if (failed_ && retriesUsed_ < MAX_RETRIES) {
            retriesUsed_++;
            dueAtMs_ = nowMs + RETRY_DELAY_MS;
            armed_ = true;
            return true;
        }
        // Settled: next attempt (if any) starts with a fresh retry budget.
        retriesUsed_ = 0;
        if (normalDelayMs == 0) {
            armed_ = false;
        } else {
            armed_ = true;
            dueAtMs_ = nowMs + normalDelayMs;
        }
        return false;
    }

    // WiFi just came back: a job whose last attempt failed transiently (still
    // waiting on a retry, or having given up until its next scheduled run)
    // gets a fresh budget and runs immediately - the failure was very likely
    // the outage itself. Jobs that succeeded, or failed permanently, are left
    // on their normal schedule.
    void onNetworkRestored(unsigned long nowMs) {
        if (failed_) arm(nowMs);
    }

    int retriesUsed() const { return retriesUsed_; }

private:
    bool armed_ = false;
    bool failed_ = false;
    int retriesUsed_ = 0;
    unsigned long dueAtMs_ = 0;
};
