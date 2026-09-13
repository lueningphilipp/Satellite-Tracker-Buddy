#include "pass_predict.h"
#include <Arduino.h>   // yield()
#include <math.h>

static const double RE = 6371.0;
static const double PI_D = 3.14159265358979323846;
static const double DEG = PI_D / 180.0;

double elevationDeg(double obsLat, double obsLon, double satLat, double satLon, double satAltKm) {
    double olat = obsLat * DEG, olon = obsLon * DEG;
    double slat = satLat * DEG, slon = satLon * DEG;

    double obsX = RE * cos(olat) * cos(olon);
    double obsY = RE * cos(olat) * sin(olon);
    double obsZ = RE * sin(olat);

    double r = RE + satAltKm;
    double satX = r * cos(slat) * cos(slon);
    double satY = r * cos(slat) * sin(slon);
    double satZ = r * sin(slat);

    double dx = satX - obsX, dy = satY - obsY, dz = satZ - obsZ;

    // Topocentric East-North-Up frame at the observer.
    double upX = cos(olat) * cos(olon), upY = cos(olat) * sin(olon), upZ = sin(olat);
    double eastX = -sin(olon), eastY = cos(olon), eastZ = 0.0;
    double northX = -sin(olat) * cos(olon), northY = -sin(olat) * sin(olon), northZ = cos(olat);

    double du = dx * upX + dy * upY + dz * upZ;
    double de = dx * eastX + dy * eastY + dz * eastZ;
    double dn = dx * northX + dy * northY + dz * northZ;

    return atan2(du, hypot(de, dn)) / DEG;
}

PassState findNextPass(Sgp4Track& track, double obsLat, double obsLon,
                        time_t start, time_t& outPassTime,
                        double minElevDeg, int maxDays, double stepScale) {
    double stepS = track.periodMin() * 60.0 / stepScale;
    if (stepS < 30.0) stepS = 30.0;
    time_t end = start + (time_t)maxDays * 86400;

    auto elevAt = [&](time_t t) -> double {
        SatPosition p = track.positionAt(t);
        if (!p.valid) return -90.0;
        return elevationDeg(obsLat, obsLon, p.lat, p.lon, p.altKm);
    };

    time_t t = start;
    double prev = elevAt(t);
    if (prev >= minElevDeg) {
        outPassTime = start;
        return PassState::kNow;
    }

    t += (time_t)stepS;
    int stepsSinceYield = 0;
    while (t <= end) {
        double e = elevAt(t);
        if (e >= minElevDeg && prev < minElevDeg) {
            outPassTime = t;
            return PassState::kFound;
        }
        prev = e;
        t += (time_t)stepS;
        // Defensive yield every so often - see CLAUDE.md's TODO on the
        // async_tcp watchdog crash found this session: a long, tight CPU
        // loop with no yields can starve other tasks on the same core even
        // with zero network/radio calls involved. This loop can run to a
        // few thousand iterations (maxDays worth of steps), so it's cheap
        // insurance against a class of bug we've already been bitten by
        // once.
        if (++stepsSinceYield >= 200) {
            stepsSinceYield = 0;
            yield();
        }
    }
    return PassState::kNone;
}
