#pragma once
#include <QJsonObject>
#include <QMainWindow>

#include "core/AutoOrient.h"
#include "core/CcmSolver.h"
#include "core/ChartSpec.h"
#include "core/Illuminant.h"
#include "core/ImageIO.h"
#include "core/PatchSampler.h"
#include "core/PointMapping.h"
#include "core/Report.h"
#include "core/Targets.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QStackedWidget;
class QGroupBox;
class QTableWidget;
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
    void autoOrient();
    void setChartCorners(const ccm::ChartCorners& corners);
    bool prepareChartData();  // compute() input for chart mode
    bool preparePointData();  // compute() input for point-mapping mode
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

    // point-mapping mode (PointMode.cpp)
    bool pointMode() const;
    void setMode(int mode);
    QGroupBox* buildPointGroup();
    void setupPointViews();
    void openReferenceImage();
    void setReferenceImage(const ccm::LoadedImage& img);
    void updateReferencePreview();
    void onPointClicked(int side, QPointF pos);
    void onMarkerMoved(int side, int index, QPointF pos);
    void selectPair(int index);
    void deleteSelectedPair();
    void cancelPendingPoint();
    void clearPairs();
    void samplePoints();
    void updatePointViews();
    void updatePointTable();
    ccm::RefEncoding refEncoding() const;
    QJsonObject pointSettingsJson() const;
    void applyPointSettingsJson(const QJsonObject& o, const QString& baseDir);
    QString pointDiagnostics() const;

    // helpers
    double whiteLevel() const;
    cv::Vec3d blackLevel() const;
    ccm::Illuminant currentIlluminant() const;
    ccm::SampleSettings sampleSettings() const;
    ccm::Targets currentTargets() const;
    ccm::FitOptions fitOptions() const;
    bool cornersReady() const;
    ccm::ChartCorners chartCorners() const;
    std::vector<bool> includedMask() const;
    std::vector<bool> neutralMask() const;
    // -> CV_8UC3 (R,G,B); srgb: apply the sRGB transfer curve, else linear output (no gamma)
    cv::Mat correctImage(const cv::Mat& native, bool srgb = true) const;
    void updateCorrectedPreview();
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
    QCheckBox* rawDisplay_ = nullptr;  // show stored pixel values (no black / gain / gamma)
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
    QCheckBox* autoOrient_ = nullptr;
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
    // point-mapping widgets
    QComboBox* mode_ = nullptr;
    QStackedWidget* centerStack_ = nullptr;
    ImageView* refView_ = nullptr;
    ImageView* camView_ = nullptr;
    QGroupBox* chartGroup_ = nullptr;
    QGroupBox* pointGroup_ = nullptr;
    QLabel* refInfo_ = nullptr;
    QComboBox* refEncoding_ = nullptr;
    QSpinBox* refHalf_ = nullptr;
    QSpinBox* camHalf_ = nullptr;
    QCheckBox* neutralizeRef_ = nullptr;
    QCheckBox* matchBrightness_ = nullptr;
    QTableWidget* pointTable_ = nullptr;

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
    // compute() input, filled by prepareChartData()/preparePointData()
    std::vector<std::string> names_;
    int gridCols_ = 6;
    // point-mapping state
    ccm::LoadedImage refImage_;
    cv::Mat refPreview_;
    double refPreviewScale_ = 1.0;
    std::vector<ccm::PointPair> pairs_;
    std::vector<ccm::ReferenceSample> refSamples_;  // one per pair
    int pendingSide_ = -1;  // 0 = reference, 1 = camera, -1 = none
    QPointF pendingPos_;
    int selectedPair_ = -1;
    cv::Vec3d referenceGains_{1, 1, 1};
    bool updatingTable_ = false;
    QString orientationWarning_;  // set by compute() when another corner order fits much better
    QString orientationNote_;     // set by compute() when it fixed the corner order itself
    bool haveResult_ = false;
    bool batch_ = false;  // no modal dialogs (command-line mode)
    QString lastError_;
};
