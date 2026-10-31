#pragma once
#include <opencv2/core.hpp>
#include <string>
#include <vector>

namespace ccm {

// Illuminants are described by their white point (CIE 1931 2° xy).
// The white point drives chromatic adaptation of the chart reference values.
struct Illuminant {
    std::string id;
    std::string name;
    std::string note;
    double x = 0.0;
    double y = 0.0;
    double cct = 0.0;  // correlated color temperature (K), informational
};

const std::vector<Illuminant>& illuminantPresets();
const Illuminant* findIlluminant(const std::string& id);

// Custom CCT: Planckian locus below 4000 K, CIE daylight locus at/above 4000 K.
Illuminant illuminantFromCct(double cct);

cv::Vec2d planckianXy(double cct);  // Kim et al. cubic spline, 1667-25000 K
cv::Vec2d daylightXy(double cct);   // CIE daylight locus, 4000-25000 K
double mccamyCct(double x, double y);

}  // namespace ccm
