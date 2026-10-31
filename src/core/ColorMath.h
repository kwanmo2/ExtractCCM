#pragma once
#include <opencv2/core.hpp>
#include <string>
#include <vector>

namespace ccm {

using Vec3 = cv::Vec3d;
using Mat33 = cv::Matx33d;

// Linear RGB working space used as the CCM output.
struct RgbSpace {
    std::string id;
    std::string name;
    Mat33 rgbToXyz;  // linear RGB -> XYZ (Y of white = 1)
    Mat33 xyzToRgb;
    Vec3 white;      // XYZ of the reference white
};

const std::vector<RgbSpace>& rgbSpaces();
const RgbSpace& rgbSpace(const std::string& id);

Vec3 xyToXyz(double x, double y);  // Y = 1
cv::Vec2d xyzToXy(const Vec3& xyz);
Vec3 whiteD50();
Vec3 whiteD65();

// Bradford chromatic adaptation from srcWhite to dstWhite (both XYZ).
Mat33 bradford(const Vec3& srcWhite, const Vec3& dstWhite);

Vec3 labToXyz(const Vec3& lab, const Vec3& white);
Vec3 xyzToLab(const Vec3& xyz, const Vec3& white);

double deltaE76(const Vec3& lab1, const Vec3& lab2);
double deltaE2000(const Vec3& lab1, const Vec3& lab2);
double deltaC(const Vec3& lab1, const Vec3& lab2);  // a*b* distance only

double srgbEncode(double linear);  // sRGB transfer curve, input clipped to [0,1]
double srgbDecode(double encoded);

}  // namespace ccm
