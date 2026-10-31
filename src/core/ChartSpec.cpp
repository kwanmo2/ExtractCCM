#include "core/ChartSpec.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <sstream>

#include "core/ColorMath.h"

namespace ccm {

namespace {

// X-Rite ColorChecker Classic, post-November-2014 formulation, CIELAB D50/2°.
struct NamedLab {
    const char* name;
    double L, a, b;
};
const NamedLab kColorChecker24[] = {
    {"Dark Skin", 37.54, 14.37, 14.92},      {"Light Skin", 64.66, 19.27, 17.50},
    {"Blue Sky", 49.32, -3.82, -22.54},      {"Foliage", 43.46, -12.74, 22.72},
    {"Blue Flower", 54.94, 9.61, -24.79},    {"Bluish Green", 70.48, -32.26, -0.37},
    {"Orange", 62.73, 35.83, 56.50},         {"Purplish Blue", 39.43, 10.75, -45.17},
    {"Moderate Red", 50.57, 48.64, 16.67},   {"Purple", 30.10, 22.54, -20.87},
    {"Yellow Green", 71.77, -24.13, 58.19},  {"Orange Yellow", 71.51, 18.24, 67.37},
    {"Blue", 28.37, 15.42, -49.80},          {"Green", 54.38, -39.72, 32.27},
    {"Red", 42.43, 51.05, 28.62},            {"Yellow", 81.80, 2.67, 80.41},
    {"Magenta", 50.63, 51.28, -14.12},       {"Cyan", 49.57, -29.71, -28.32},
    {"White 9.5", 95.19, -1.03, 2.93},       {"Neutral 8", 81.29, -0.57, 0.44},
    {"Neutral 6.5", 66.89, -0.75, -0.06},    {"Neutral 5", 50.76, -0.13, 0.14},
    {"Neutral 3.5", 35.63, -0.46, -0.48},    {"Black 2", 20.64, 0.07, -0.46},
};

std::string gridName(int r, int c) {
    // A1-style name: column letter(s) + row number
    std::string s;
    int n = c;
    do {
        s.insert(s.begin(), char('A' + n % 26));
        n = n / 26 - 1;
    } while (n >= 0);
    return s + std::to_string(r + 1);
}

ChartSpec emptyChart(const char* id, const char* name, const char* note, int rows, int cols) {
    ChartSpec c;
    c.id = id;
    c.name = name;
    c.note = note;
    c.rows = rows;
    c.cols = cols;
    c.patches.resize(size_t(rows) * cols);
    for (int r = 0; r < rows; ++r)
        for (int k = 0; k < cols; ++k) c.patches[size_t(r) * cols + k].name = gridName(r, k);
    return c;
}

ChartSpec colorChecker24(const char* id, const char* name) {
    ChartSpec c = emptyChart(id, name,
                             "Built-in reference: X-Rite post-2014 formulation (D50). "
                             "Load the batch measurement file for best accuracy.",
                             4, 6);
    for (int i = 0; i < 24; ++i) {
        c.patches[i].name = kColorChecker24[i].name;
        c.patches[i].labD50 = cv::Vec3d(kColorChecker24[i].L, kColorChecker24[i].a, kColorChecker24[i].b);
        c.patches[i].hasRef = true;
    }
    c.builtinReference = true;
    return c;
}

std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false;
    for (char ch : line) {
        if (ch == '"') {
            quoted = !quoted;
            continue;
        }
        if (!quoted && (std::isspace(static_cast<unsigned char>(ch)) || ch == ',' || ch == ';')) {
            if (!cur.empty()) out.push_back(cur);
            cur.clear();
        } else {
            cur += ch;
        }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

bool toDouble(const std::string& s, double& v) {
    try {
        size_t pos = 0;
        v = std::stod(s, &pos);
        return pos == s.size();
    } catch (...) {
        return false;
    }
}

std::string upper(std::string s) {
    for (auto& ch : s) ch = char(std::toupper(static_cast<unsigned char>(ch)));
    return s;
}

struct RefEntry {
    std::string name;
    cv::Vec3d lab;
};

bool parseCgats(const std::vector<std::string>& lines, std::vector<RefEntry>& out, std::string& error) {
    std::vector<std::string> fields;
    bool inFormat = false, inData = false;
    int iName = -1, iL = -1, ia = -1, ib = -1, iX = -1, iY = -1, iZ = -1;
    for (const auto& raw : lines) {
        auto tok = tokenize(raw);
        if (tok.empty()) continue;
        std::string key = upper(tok[0]);
        if (key == "BEGIN_DATA_FORMAT") { inFormat = true; continue; }
        if (key == "END_DATA_FORMAT") {
            inFormat = false;
            for (int i = 0; i < int(fields.size()); ++i) {
                std::string f = upper(fields[i]);
                if (f == "SAMPLE_NAME" || (f == "SAMPLE_ID" && iName < 0)) iName = i;
                else if (f == "LAB_L") iL = i;
                else if (f == "LAB_A") ia = i;
                else if (f == "LAB_B") ib = i;
                else if (f == "XYZ_X") iX = i;
                else if (f == "XYZ_Y") iY = i;
                else if (f == "XYZ_Z") iZ = i;
            }
            continue;
        }
        if (inFormat) {
            for (auto& t : tok) fields.push_back(t);
            continue;
        }
        if (key == "BEGIN_DATA") { inData = true; continue; }
        if (key == "END_DATA") { inData = false; continue; }
        if (!inData) continue;

        bool haveLab = iL >= 0 && ia >= 0 && ib >= 0;
        bool haveXyz = iX >= 0 && iY >= 0 && iZ >= 0;
        if (!haveLab && !haveXyz) {
            error = "CGATS file has neither LAB_L/LAB_A/LAB_B nor XYZ_X/XYZ_Y/XYZ_Z fields.";
            return false;
        }
        RefEntry e;
        if (iName >= 0 && iName < int(tok.size())) e.name = tok[iName];
        double v0, v1, v2;
        if (haveLab) {
            if (std::max({iL, ia, ib}) >= int(tok.size()) || !toDouble(tok[iL], v0) || !toDouble(tok[ia], v1) ||
                !toDouble(tok[ib], v2))
                continue;
            e.lab = cv::Vec3d(v0, v1, v2);
        } else {
            if (std::max({iX, iY, iZ}) >= int(tok.size()) || !toDouble(tok[iX], v0) || !toDouble(tok[iY], v1) ||
                !toDouble(tok[iZ], v2))
                continue;
            e.lab = xyzToLab(cv::Vec3d(v0, v1, v2) / 100.0, whiteD50());
        }
        out.push_back(e);
    }
    if (out.empty()) {
        error = "No data rows found between BEGIN_DATA and END_DATA.";
        return false;
    }
    return true;
}

void parseCsv(const std::vector<std::string>& lines, std::vector<RefEntry>& out) {
    for (const auto& raw : lines) {
        auto tok = tokenize(raw);
        if (tok.size() < 3) continue;
        // Take the last three numeric fields as L,a,b; a leading non-numeric field is the name.
        std::vector<double> nums;
        std::string name;
        for (auto& t : tok) {
            double v;
            if (toDouble(t, v)) nums.push_back(v);
            else if (nums.empty()) name = t;
        }
        if (nums.size() < 3) continue;
        size_t n = nums.size();
        RefEntry e;
        e.name = name;
        e.lab = cv::Vec3d(nums[n - 3], nums[n - 2], nums[n - 1]);
        out.push_back(e);
    }
}

}  // namespace

bool ChartSpec::hasAllReferences() const {
    if (patches.empty()) return false;
    return std::all_of(patches.begin(), patches.end(), [](const ChartPatch& p) { return p.hasRef; });
}

std::vector<int> ChartSpec::neutralIndices(double maxChroma) const {
    std::vector<int> idx;
    for (int i = 0; i < int(patches.size()); ++i) {
        const auto& p = patches[i];
        if (p.hasRef && std::hypot(p.labD50[1], p.labD50[2]) <= maxChroma) idx.push_back(i);
    }
    return idx;
}

int ChartSpec::darkestNeutral() const {
    int best = -1;
    for (int i : neutralIndices())
        if (best < 0 || patches[i].labD50[0] < patches[best].labD50[0]) best = i;
    return best;
}

const std::vector<ChartSpec>& chartPresets() {
    static const std::vector<ChartSpec> presets = [] {
        std::vector<ChartSpec> v;
        v.push_back(colorChecker24("cc24", "X-Rite / Calibrite ColorChecker Classic 24"));
        v.push_back(colorChecker24("cc24-passport", "ColorChecker Passport (Classic 24 page)"));
        v.push_back(emptyChart("sg140", "ColorChecker Digital SG 140 (14x10)",
                               "Load the X-Rite reference file (CGATS) for your chart.", 10, 14));
        v.push_back(emptyChart("spyder24", "Datacolor SpyderCHECKR 24 (6x4)",
                               "Load the Datacolor reference file (CGATS/CSV).", 6, 4));
        v.push_back(emptyChart("spyder48", "Datacolor SpyderCHECKR 48 (8x6)",
                               "Load the Datacolor reference file (CGATS/CSV).", 8, 6));
        v.push_back(emptyChart("it8", "IT8.7/2 (12x22 color area)",
                               "Load the batch CGATS file shipped with the target. "
                               "Gray scale row is not part of this grid.", 12, 22));
        ChartSpec custom = makeCustomChart(4, 6);
        v.push_back(custom);
        return v;
    }();
    return presets;
}

ChartSpec makeCustomChart(int rows, int cols) {
    ChartSpec c = emptyChart("custom", "Custom grid", "Set rows/columns and load a reference file.", rows, cols);
    c.customGrid = true;
    return c;
}

void resizeChart(ChartSpec& chart, int rows, int cols) {
    ChartSpec fresh = emptyChart(chart.id.c_str(), chart.name.c_str(), chart.note.c_str(), rows, cols);
    fresh.customGrid = chart.customGrid;
    chart = fresh;
}

bool loadReferenceFile(const std::string& path, bool columnMajor, ChartSpec& chart, std::string& error) {
    std::ifstream in(path);
    if (!in) {
        error = "Cannot open " + path;
        return false;
    }
    std::vector<std::string> lines;
    std::string line;
    bool isCgats = false;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (upper(line).find("BEGIN_DATA") != std::string::npos) isCgats = true;
        lines.push_back(line);
    }

    std::vector<RefEntry> entries;
    if (isCgats) {
        if (!parseCgats(lines, entries, error)) return false;
    } else {
        parseCsv(lines, entries);
        if (entries.empty()) {
            error = "No rows with three numeric L,a,b values found.";
            return false;
        }
    }

    const int n = chart.count();
    if (int(entries.size()) < n) {
        error = "Reference file has " + std::to_string(entries.size()) + " entries but the chart has " +
                std::to_string(n) + " patches.";
        return false;
    }
    for (int i = 0; i < n; ++i) {
        int r = columnMajor ? i % chart.rows : i / chart.cols;
        int c = columnMajor ? i / chart.rows : i % chart.cols;
        auto& p = chart.patches[size_t(r) * chart.cols + c];
        p.labD50 = entries[i].lab;
        p.hasRef = true;
        if (!entries[i].name.empty()) p.name = entries[i].name;
    }
    if (int(entries.size()) > n)
        error = "Warning: file has " + std::to_string(entries.size()) + " entries; used the first " +
                std::to_string(n) + ".";
    return true;
}

}  // namespace ccm
