#include "astro.h"
#include <math.h>

static const double PI_D = 3.14159265358979323846;
static const double DEG = PI_D / 180.0;
// J2000 epoch (2000-01-01 12:00:00 UTC) as a unix timestamp.
static const double J2000_UNIX = 946728000.0;

void subsolar(time_t t, double& decDeg, double& lonDeg) {
    double d = ((double)t - J2000_UNIX) / 86400.0;
    double L = fmod(280.46 + 0.9856474 * d, 360.0) * DEG;
    double g = fmod(357.528 + 0.9856003 * d, 360.0) * DEG;
    double lam = L + (1.915 * DEG) * sin(g) + (0.02 * DEG) * sin(2 * g);
    double dec = asin(sin(23.44 * DEG) * sin(lam)) / DEG;
    double gmst = fmod(280.46061837 + 360.98564736629 * d, 360.0);
    double ra = atan2(cos(23.44 * DEG) * sin(lam), cos(lam)) / DEG;
    decDeg = dec;
    lonDeg = fmod(ra - gmst + 540.0, 360.0) - 180.0;
}

bool isDay(double lat, double lon, double sunDecDeg, double sunLonDeg) {
    double sl = sunDecDeg * DEG, sn = sunLonDeg * DEG;
    double cosz = sin(lat * DEG) * sin(sl) +
                  cos(lat * DEG) * cos(sl) * cos(lon * DEG - sn);
    return cosz > 0;
}
