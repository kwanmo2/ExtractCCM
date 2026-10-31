#include "core/ColorMath.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace ccm {

namespace {

RgbSpace makeSpace(const std::string& id, const std::string& name, const Mat33& rgbToXyz) {
    RgbSpace s;
    s.id = id;
    s.name = name;
    s.rgbToXyz = rgbToXyz;
    s.xyzToRgb = rgbToXyz.inv();
    s.white = rgbToXyz * Vec3(1, 1, 1);
    return s;
}

constexpr double kEps = 216.0 / 24389.0;  // (6/29)^3
constexpr double kKappa = 24389.0 / 27.0;

double labF(double t) { return t > kEps ? std::cbrt(t) : (kKappa * t + 16.0) / 116.0; }
double labFInv(double f) {
    double f3 = f * f * f;
    return f3 > kEps ? f3 : (116.0 * f - 16.0) / kKappa;
}

double deg2rad(double d) { return d * CV_PI / 180.0; }
double rad2deg(double r) { return r * 180.0 / CV_PI; }

}  // namespace

const std::vector<RgbSpace>& rgbSpaces() {
    static const std::vector<RgbSpace> spaces = {
        makeSpace("srgb", "sRGB / Rec.709 (D65)",
                  Mat33(0.4124564, 0.3575761, 0.1804375,
                        0.2126729, 0.7151522, 0.0721750,
                        0.0193339, 0.1191920, 0.9503041)),
        makeSpace("adobe", "Adobe RGB (1998) (D65)",
                  Mat33(0.5767309, 0.1855540, 0.1881852,
                        0.2973769, 0.6273491, 0.0752741,
                        0.0270343, 0.0706872, 0.9911085)),
    };
    return spaces;
}

const RgbSpace& rgbSpace(const std::string& id) {
    for (const auto& s : rgbSpaces())
        if (s.id == id) return s;
    return rgbSpaces().front();
}

Vec3 xyToXyz(double x, double y) { return Vec3(x / y, 1.0, (1.0 - x - y) / y); }

cv::Vec2d xyzToXy(const Vec3& xyz) {
    double s = xyz[0] + xyz[1] + xyz[2];
    if (std::abs(s) < 1e-12) return cv::Vec2d(0, 0);
    return cv::Vec2d(xyz[0] / s, xyz[1] / s);
}

Vec3 whiteD50() { return xyToXyz(0.34567, 0.35850); }
Vec3 whiteD65() { return xyToXyz(0.31271, 0.32902); }

Mat33 bradford(const Vec3& srcWhite, const Vec3& dstWhite) {
    const Mat33 ma(0.8951, 0.2664, -0.1614,
                   -0.7502, 1.7135, 0.0367,
                   0.0389, -0.0685, 1.0296);
    Vec3 s = ma * srcWhite;
    Vec3 d = ma * dstWhite;
    Mat33 scale = Mat33::diag(Vec3(d[0] / s[0], d[1] / s[1], d[2] / s[2]));
    return ma.inv() * scale * ma;
}

Vec3 labToXyz(const Vec3& lab, const Vec3& white) {
    double fy = (lab[0] + 16.0) / 116.0;
    double fx = fy + lab[1] / 500.0;
    double fz = fy - lab[2] / 200.0;
    return Vec3(white[0] * labFInv(fx), white[1] * labFInv(fy), white[2] * labFInv(fz));
}

Vec3 xyzToLab(const Vec3& xyz, const Vec3& white) {
    double fx = labF(xyz[0] / white[0]);
    double fy = labF(xyz[1] / white[1]);
    double fz = labF(xyz[2] / white[2]);
    return Vec3(116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz));
}

double deltaE76(const Vec3& a, const Vec3& b) { return cv::norm(a - b); }

double deltaC(const Vec3& a, const Vec3& b) { return std::hypot(a[1] - b[1], a[2] - b[2]); }

// CIEDE2000 (Sharma, Wu, Dalal 2005), kL = kC = kH = 1.
double deltaE2000(const Vec3& lab1, const Vec3& lab2) {
    const double L1 = lab1[0], a1 = lab1[1], b1 = lab1[2];
    const double L2 = lab2[0], a2 = lab2[1], b2 = lab2[2];

    const double C1 = std::hypot(a1, b1), C2 = std::hypot(a2, b2);
    const double Cbar = (C1 + C2) / 2.0;
    const double Cbar7 = std::pow(Cbar, 7.0);
    const double G = 0.5 * (1.0 - std::sqrt(Cbar7 / (Cbar7 + std::pow(25.0, 7.0))));
    const double a1p = (1.0 + G) * a1, a2p = (1.0 + G) * a2;
    const double C1p = std::hypot(a1p, b1), C2p = std::hypot(a2p, b2);

    auto hueAngle = [](double b, double ap) {
        if (b == 0.0 && ap == 0.0) return 0.0;
        double h = rad2deg(std::atan2(b, ap));
        return h < 0 ? h + 360.0 : h;
    };
    const double h1p = hueAngle(b1, a1p), h2p = hueAngle(b2, a2p);

    const double dLp = L2 - L1;
    const double dCp = C2p - C1p;
    double dhp = 0.0;
    if (C1p * C2p != 0.0) {
        dhp = h2p - h1p;
        if (dhp > 180.0) dhp -= 360.0;
        else if (dhp < -180.0) dhp += 360.0;
    }
    const double dHp = 2.0 * std::sqrt(C1p * C2p) * std::sin(deg2rad(dhp / 2.0));

    const double Lbarp = (L1 + L2) / 2.0;
    const double Cbarp = (C1p + C2p) / 2.0;
    double hbarp = h1p + h2p;
    if (C1p * C2p != 0.0) {
        if (std::abs(h1p - h2p) <= 180.0) hbarp /= 2.0;
        else if (h1p + h2p < 360.0) hbarp = (h1p + h2p + 360.0) / 2.0;
        else hbarp = (h1p + h2p - 360.0) / 2.0;
    }

    const double T = 1.0 - 0.17 * std::cos(deg2rad(hbarp - 30.0)) +
                      0.24 * std::cos(deg2rad(2.0 * hbarp)) +
                      0.32 * std::cos(deg2rad(3.0 * hbarp + 6.0)) -
                      0.20 * std::cos(deg2rad(4.0 * hbarp - 63.0));
    const double dTheta = 30.0 * std::exp(-std::pow((hbarp - 275.0) / 25.0, 2.0));
    const double Cbarp7 = std::pow(Cbarp, 7.0);
    const double Rc = 2.0 * std::sqrt(Cbarp7 / (Cbarp7 + std::pow(25.0, 7.0)));
    const double Lm50 = (Lbarp - 50.0) * (Lbarp - 50.0);
    const double Sl = 1.0 + 0.015 * Lm50 / std::sqrt(20.0 + Lm50);
    const double Sc = 1.0 + 0.045 * Cbarp;
    const double Sh = 1.0 + 0.015 * Cbarp * T;
    const double Rt = -std::sin(deg2rad(2.0 * dTheta)) * Rc;

    const double tl = dLp / Sl, tc = dCp / Sc, th = dHp / Sh;
    return std::sqrt(tl * tl + tc * tc + th * th + Rt * tc * th);
}

double srgbEncode(double v) {
    v = std::clamp(v, 0.0, 1.0);
    return v <= 0.0031308 ? 12.92 * v : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
}

double srgbDecode(double v) {
    return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}

}  // namespace ccm
