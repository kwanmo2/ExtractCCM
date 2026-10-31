#pragma once
#include <QJsonObject>
#include <QMainWindow>

#include "core/CcmSolver.h"
#include "core/ChartSpec.h"
#include "core/Illuminant.h"
#include "core/ImageIO.h"
#include "core/PatchSampler.h"
#include "core/Report.h"
#include "core/Targets.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class ImageView;
class ResultPanel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    // Batch helpers (command line): return false and set error on failure.
    void setBatchMode(bool on) { batch_ = on; }
    bool loadSessionFile(const QString& path, QString& error);
    bool computeNow(QString& error);
    bool exportJsonTo(const QString& path, QString& error);

    // Takes over the state of another window (used when switching the UI language).
    void restoreState(const QJsonObject& settings, bool recompute);

private:
    // UI construction
    QWidget* buildSettingsPanel();
    void buildMenus();
    void switchLanguage(int lang);

    // actions
    void openImage();
    void openRaw();
    void loadReference();
    void compute();
    void exportJson();
    QByteArray resultJson() const;
    void exportCsv();
    void exportHeader();
    void exportRegisters();
    void saveCorrectedImage();
    void saveSession();
    void loadSession();

    // state updates
    void setImage(const ccm::LoadedImage& img);
    void updatePreview();
    void onChartChanged();
    void onIlluminantChanged();
    void updateOverlay();
    void updateModelControls();
    void invalidateResult();

    // helpers
    double whiteLevel() const;
    cv::Vec3d blackLevel() const;
    ccm::Illuminant currentIlluminant() const;
    ccm::SampleSettings sampleSettings() const;
    ccm::FitOptions fitOptions() const;
    bool cornersReady() const;
    ccm::ChartCorners chartCorners() const;
    std::vector<bool> includedMask() const;
    std::vector<bool> neutralMask() const;
    cv::Mat correctImage(const cv::Mat& native) const;  // -> CV_8UC3 sRGB (R,G,B)
    QString buildSummary() const;
    QString buildDiagnostics() const;
    QJsonObject settingsJson() const;
    void applySettingsJson(const QJsonObject& o, const QString& baseDir);
    ccm::ExportInfo exportInfo() const;
    void notify(const QString& title, const QString& message);
    bool writeText(const QString& filter, const QString& suffix, const std::string& text);

    // widgets
    QLabel* imageInfo_ = nullptr;
    QSpinBox* dataBits_ = nullptr;
    QCheckBox* autoStretch_ = nullptr;
    QDoubleSpinBox* displayGain_ = nullptr;
    QComboBox* blackMode_ = nullptr;
    QDoubleSpinBox* black_[3] = {};
    QDoubleSpinBox* satPct_ = nullptr;
    QComboBox* illuminant_ = nullptr;
    QSpinBox* customCct_ = nullptr;
    QLabel* illumInfo_ = nullptr;
    QCheckBox* adaptWhite_ = nullptr;
    QComboBox* outSpace_ = nullptr;
    QComboBox* chart_ = nullptr;
    QSpinBox* rows_ = nullptr;
    QSpinBox* cols_ = nullptr;
    QComboBox* dataOrder_ = nullptr;
    QLabel* chartInfo_ = nullptr;
    QDoubleSpinBox* sampleSize_ = nullptr;
    QComboBox* model_ = nullptr;
    QComboBox* objective_ = nullptr;
    QCheckBox* separateGain_ = nullptr;
    QCheckBox* boundCoef_ = nullptr;
    QDoubleSpinBox* coefMin_ = nullptr;
    QDoubleSpinBox* coefMax_ = nullptr;
    QComboBox* offsetMode_ = nullptr;
    QDoubleSpinBox* neutralWeight_ = nullptr;
    QCheckBox* quantEnable_ = nullptr;
    QSpinBox* intBits_ = nullptr;
    QSpinBox* fracBits_ = nullptr;
    QCheckBox* quantSigned_ = nullptr;
    QCheckBox* preserveSum_ = nullptr;
    QSpinBox* offsetBits_ = nullptr;
    ImageView* view_ = nullptr;
    ResultPanel* results_ = nullptr;

    // state
    ccm::LoadedImage image_;
    bool imageIsRaw_ = false;
    ccm::RawParams rawParams_;
    cv::Mat preview_;  // downscaled native image
    double previewScale_ = 1.0;
    ccm::ChartSpec chartSpec_;
    QString referencePath_;
    std::vector<bool> excluded_;
    std::vector<ccm::PatchStat> stats_;
    ccm::Targets targets_;
    ccm::FitResult fit_;
    ccm::Evaluation eval_;
    ccm::LinearityResult linearity_;
    ccm::SeparabilityReport separability_;
    bool haveResult_ = false;
    bool batch_ = false;  // no modal dialogs (command-line mode)
    QString lastError_;
};
