#include "sgp4_track.h"
#include <Sgp4.h>
#include <math.h>

// sgp4init() is a free function declared in the library's sgp4unit.h, which
// Sgp4.h pulls in transitively (via sgp4pred.h). Verified signature (no
// ndot/nddot params - this port's SGP4 ignores mean-motion derivatives):
//   bool sgp4init(gravconsttype whichconst, char opsmode, const int satn,
//       const double epoch, const double xbstar, const double xecco,
//       const double xargpo, const double xinclo, const double xmo,
//       const double xno, const double xnodeo, elsetrec& satrec);

Sgp4Track satTrack;

static Sgp4 sgp4lib;   // library's own class; we populate sgp4lib.satrec ourselves,
                        // bypassing its TLE-text init(), then reuse its propagation
                        // (findsat) and ECI->lat/lon/alt conversion as-is.

static const double PI_D = 3.14159265358979323846;
static const double MU = 398600.4418;   // km^3/s^2, matches demo's constant
static const double RE = 6371.0;        // km

// Standard Vallado Julian Date formula - same one python-sgp4's jday() (used
// by the demo) implements. Written locally rather than relying on the
// library's own jday() helper, whose extra parameters weren't worth the risk
// of misreading from source.
static double jday(int year, int mon, int day, int hr, int minute, double sec) {
    return 367.0 * year -
           floor((7 * (year + floor((mon + 9) / 12.0))) * 0.25) +
           floor(275 * mon / 9.0) + day + 1721013.5 +
           ((sec / 60.0 + minute) / 60.0 + hr) / 24.0;
}

bool Sgp4Track::init(const OrbitalElements& el) {
    double jd = jday(el.epochYear, el.epochMonth, el.epochDay,
                      el.epochHour, el.epochMin, el.epochSec);
    double epoch = jd - 2433281.5;   // sgp4init's epoch ref: 1949-12-31 00:00 UT

    double no    = el.meanMotionRevPerDay * PI_D / 720.0;   // rev/day -> rad/min
    double inclo = el.inclinationDeg * PI_D / 180.0;
    double nodeo = el.raanDeg * PI_D / 180.0;
    double argpo = el.argPerigeeDeg * PI_D / 180.0;
    double mo    = el.meanAnomalyDeg * PI_D / 180.0;

    bool ok = sgp4init(wgs84, 'i', (int)el.noradCatId, epoch,
                        el.bstar, el.eccentricity, argpo, inclo, mo, no, nodeo,
                        sgp4lib.satrec);
    if (!ok) return false;

    // sgp4init() never touches jdsatepoch (only the TLE-text parsing path
    // we bypassed does that) - but Sgp4::findsat() computes tsince as
    // (jdNow - satrec.jdsatepoch)*1440, so leaving it at 0 makes tsince a
    // huge bogus value and every propagation fails. Set it ourselves from
    // the same full JD we derived `epoch` from. Found this the hard way: it
    // compiled and initialized fine (apogee/perigee came out correct) but
    // every findsat() call failed until this was added.
    sgp4lib.satrec.jdsatepoch = jd;

    // Same formula as the demo's Sat.__init__: a = (mu/n^2)^(1/3), n in rad/s.
    double n_rad_s = el.meanMotionRevPerDay * 2.0 * PI_D / 86400.0;
    double a = pow(MU / (n_rad_s * n_rad_s), 1.0 / 3.0);
    apogeeKm_  = a * (1.0 + el.eccentricity) - RE;
    perigeeKm_ = a * (1.0 - el.eccentricity) - RE;
    periodMin_ = 1440.0 / el.meanMotionRevPerDay;
    orbitClass_ = classifyOrbit(apogeeKm_, perigeeKm_, el.inclinationDeg, periodMin_);
    return true;
}

const char* classifyOrbit(double apogeeKm, double perigeeKm, double inclDeg, double periodMin) {
    double alt = (apogeeKm + perigeeKm) / 2.0;
    double spread = apogeeKm - perigeeKm;
    if (spread > 15000 && inclDeg >= 55 && inclDeg <= 65) return "Molniya";
    if (fabs(periodMin - 1436) < 60 && spread < 500 && inclDeg < 15) return "GEO";
    if (spread > 15000) return perigeeKm < 5000 ? "GTO" : "HEO";
    if (alt < 2000) return (inclDeg >= 95 && inclDeg <= 105) ? "SSO" : "LEO";
    return alt < 35000 ? "MEO" : "HEO";
}

SatPosition Sgp4Track::positionAt(time_t unixTime) {
    sgp4lib.findsat((unsigned long)unixTime);
    SatPosition pos;
    pos.lat   = sgp4lib.satLat;
    pos.lon   = sgp4lib.satLon;
    pos.altKm = sgp4lib.satAlt;
    pos.valid = (sgp4lib.satrec.error == 0);
    return pos;
}
