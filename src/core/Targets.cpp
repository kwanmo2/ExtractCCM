#include "core/Targets.h"

namespace ccm {

Targets computeTargets(const ChartSpec& chart, const Illuminant& illuminant, const TargetOptions& opt) {
    const RgbSpace& space = rgbSpace(opt.outputSpace);
    const Vec3 d50 = whiteD50();
    const Vec3 illumWhite = xyToXyz(illuminant.x, illuminant.y);
    // adapt: appearance under the output white; otherwise: as lit by the illuminant
    const Mat33 cat = opt.adaptToOutputWhite ? bradford(d50, space.white) : bradford(d50, illumWhite);

    Targets t;
    for (const auto& p : chart.patches) {
        if (!p.hasRef) {
            t.linearRgb.emplace_back(0, 0, 0);
            t.lab.emplace_back(0, 0, 0);
            continue;
        }
        Vec3 xyz = cat * labToXyz(p.labD50, d50);
        t.linearRgb.push_back(space.xyzToRgb * xyz);
        t.lab.push_back(xyzToLab(xyz, space.white));
    }
    return t;
}

Vec3 linearRgbToLab(const Vec3& rgb, const RgbSpace& space) {
    return xyzToLab(space.rgbToXyz * rgb, space.white);
}

}  // namespace ccm
