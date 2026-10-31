#include "core/Illuminant.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace ccm {

namespace {
Illuminant make(const char* id, const char* name, const char* note, double x, double y) {
    Illuminant il;
    il.id = id;
    il.name = name;
    il.note = note;
    il.x = x;
    il.y = y;
    il.cct = mccamyCct(x, y);
    return il;
}
}  // namespace

const std::vector<Illuminant>& illuminantPresets() {
    static const std::vector<Illuminant> presets = {
        make("A", "CIE A - Halogen / Incandescent (2856K)", "Tungsten halogen lamps, incandescent", 0.44757, 0.40745),
        make("F12", "CIE F12 - U30 Fluorescent (3000K)", "Warm white tri-band fluorescent", 0.43695, 0.40441),
        make("LED-B2", "CIE LED-B2 - Warm White LED (3000K)", "Phosphor-converted blue LED", 0.4357, 0.4012),
        make("F11", "CIE F11 - TL84 Fluorescent (4000K)", "Store/office tri-band fluorescent", 0.38052, 0.37713),
        make("LED-B3", "CIE LED-B3 - Neutral White LED (4100K)", "Phosphor-converted blue LED", 0.3756, 0.3723),
        make("F2", "CIE F2 - Cool White Fluorescent (4230K)", "CWF, standard fluorescent", 0.37208, 0.37529),
        make("D50", "CIE D50 - Horizon Daylight (5003K)", "Graphic arts viewing standard", 0.34567, 0.35850),
        make("LED-B4", "CIE LED-B4 - White LED (5100K)", "Phosphor-converted blue LED", 0.3422, 0.3502),
        make("D55", "CIE D55 - Mid-morning Daylight (5503K)", "Photographic daylight", 0.33242, 0.34743),
        make("LED-B5", "CIE LED-B5 - Cool White LED (6600K)", "Phosphor-converted blue LED", 0.3118, 0.3236),
        make("D65", "CIE D65 - Noon Daylight (6504K)", "sRGB / video reference white", 0.31271, 0.32902),
        make("D75", "CIE D75 - North Sky Daylight (7504K)", "Overcast / shade", 0.29902, 0.31485),
    };
    return presets;
}

const Illuminant* findIlluminant(const std::string& id) {
    for (const auto& il : illuminantPresets())
        if (il.id == id) return &il;
    return nullptr;
}

cv::Vec2d planckianXy(double T) {
    T = std::clamp(T, 1667.0, 25000.0);
    const double t = 1e3 / T, t2 = t * t, t3 = t2 * t;
    double x = T <= 4000.0 ? -0.2661239 * t3 - 0.2343589 * t2 + 0.8776956 * t + 0.179910
                           : -3.0258469 * t3 + 2.1070379 * t2 + 0.2226347 * t + 0.240390;
    const double x2 = x * x, x3 = x2 * x;
    double y;
    if (T <= 2222.0) y = -1.1063814 * x3 - 1.34811020 * x2 + 2.18555832 * x - 0.20219683;
    else if (T <= 4000.0) y = -0.9549476 * x3 - 1.37418593 * x2 + 2.09137015 * x - 0.16748867;
    else y = 3.0817580 * x3 - 5.87338670 * x2 + 3.75112997 * x - 0.37001483;
    return cv::Vec2d(x, y);
}

cv::Vec2d daylightXy(double T) {
    T = std::clamp(T, 4000.0, 25000.0);
    const double t = 1e3 / T, t2 = t * t, t3 = t2 * t;
    double x = T <= 7000.0 ? -4.6070 * t3 + 2.9678 * t2 + 0.09911 * t + 0.244063
                           : -2.0064 * t3 + 1.9018 * t2 + 0.24748 * t + 0.237040;
    double y = -3.0 * x * x + 2.870 * x - 0.275;
    return cv::Vec2d(x, y);
}

double mccamyCct(double x, double y) {
    const double n = (x - 0.3320) / (0.1858 - y);
    return 449.0 * n * n * n + 3525.0 * n * n + 6823.3 * n + 5520.33;
}

Illuminant illuminantFromCct(double cct) {
    cv::Vec2d xy = cct < 4000.0 ? planckianXy(cct) : daylightXy(cct);
    char name[64];
    std::snprintf(name, sizeof(name), "Custom %.0fK (%s)", cct, cct < 4000.0 ? "Planckian" : "Daylight");
    Illuminant il;
    il.id = "CCT";
    il.name = name;
    il.note = cct < 4000.0 ? "Blackbody locus" : "CIE daylight locus";
    il.x = xy[0];
    il.y = xy[1];
    il.cct = cct;
    return il;
}

}  // namespace ccm
