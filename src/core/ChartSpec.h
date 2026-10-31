#pragma once
#include <opencv2/core.hpp>
#include <string>
#include <vector>

namespace ccm {

struct ChartPatch {
    std::string name;
    cv::Vec3d labD50;  // reference CIELAB, D50 / 2°
    bool hasRef = false;
};

// Patches are stored row-major, starting at the top-left patch of the chart
// in its reference orientation.
struct ChartSpec {
    std::string id;
    std::string name;
    std::string note;
    int rows = 0;
    int cols = 0;
    bool builtinReference = false;
    bool customGrid = false;  // rows/cols editable by the user
    std::vector<ChartPatch> patches;

    int count() const { return rows * cols; }
    bool hasAllReferences() const;
    std::vector<int> neutralIndices(double maxChroma = 4.0) const;
    // Index of the darkest neutral patch, or -1.
    int darkestNeutral() const;
};

const std::vector<ChartSpec>& chartPresets();
ChartSpec makeCustomChart(int rows, int cols);
void resizeChart(ChartSpec& chart, int rows, int cols);

// Loads reference values from a CGATS (.cgats/.txt/.it8) file with LAB_L/LAB_A/LAB_B
// or XYZ_X/XYZ_Y/XYZ_Z (D50, Y=100) fields, or from a CSV with "[name,]L,a,b" rows.
// Values are assigned in file order, row-major or column-major over the chart grid.
bool loadReferenceFile(const std::string& path, bool columnMajor, ChartSpec& chart, std::string& error);

}  // namespace ccm
