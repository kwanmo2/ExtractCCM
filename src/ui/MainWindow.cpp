#include "ui/MainWindow.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QActionGroup>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSpinBox>
#include <QSplitter>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include "ui/ImageView.h"
#include "ui/Language.h"
#include "ui/RawImportDialog.h"
#include "ui/ResultPanel.h"

using namespace ccm;

namespace {

constexpr int kPreviewMaxDim = 3000;

enum class BlackMode { Manual, FromChart, Zero };

QString qs(const std::string& s) { return QString::fromStdString(s); }
// Strings defined in the core library are translated in the "ccm" context.
QString trCore(const std::string& s) { return QCoreApplication::translate("ccm", s.c_str()); }

QDoubleSpinBox* doubleSpin(double lo, double hi, double val, int decimals, double step = 1.0) {
    auto* s = new QDoubleSpinBox;
    s->setRange(lo, hi);
    s->setDecimals(decimals);
    s->setSingleStep(step);
    s->setValue(val);
    return s;
}

QSpinBox* intSpin(int lo, int hi, int val) {
    auto* s = new QSpinBox;
    s->setRange(lo, hi);
    s->setValue(val);
    return s;
}

QJsonArray matToJson(const cv::Mat& m) {
    QJsonArray rows;
    for (int r = 0; r < m.rows; ++r) {
        QJsonArray row;
        for (int c = 0; c < m.cols; ++c)
            row.append(m.type() == CV_32S ? double(m.at<int>(r, c)) : m.at<double>(r, c));
        rows.append(row);
    }
    return rows;
}

QJsonArray vecToJson(const cv::Vec3d& v) { return QJsonArray{v[0], v[1], v[2]}; }

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle(tr("ExtractCCM"));
    resize(1600, 950);

    view_ = new ImageView;
    results_ = new ResultPanel;
    setupPointViews();
    centerStack_ = new QStackedWidget;
    centerStack_->addWidget(view_);
    auto* pointSplit = new QSplitter;
    pointSplit->addWidget(refView_);
    pointSplit->addWidget(camView_);
    centerStack_->addWidget(pointSplit);

    auto* scroll = new QScrollArea;
    scroll->setWidget(buildSettingsPanel());
    scroll->setWidgetResizable(true);
    scroll->setMinimumWidth(440);

    auto* split = new QSplitter;
    split->addWidget(scroll);
    split->addWidget(centerStack_);
    split->addWidget(results_);
    split->setStretchFactor(0, 0);
    split->setStretchFactor(1, 3);
    split->setStretchFactor(2, 2);
    split->setChildrenCollapsible(false);
    split->setSizes({460, 760, 560});
    setCentralWidget(split);
    buildMenus();

    connect(view_, &ImageView::cornersChanged, this, [this] {
        invalidateResult();
        updateOverlay();
    });
    connect(results_, &ResultPanel::correctedGammaToggled, this, &MainWindow::updateCorrectedPreview);
    connect(results_, &ResultPanel::patchIncludeToggled, this, [this](int i, bool include) {
        if (pointMode()) {
            if (i < 0 || i >= int(pairs_.size())) return;
            pairs_[i].enabled = include;
            updatePointTable();
            updatePointViews();
        } else {
            if (i < 0 || i >= int(excluded_.size())) return;
            excluded_[i] = !include;
            updateOverlay();
        }
        if (haveResult_) compute();
    });

    setMode(0);
    onChartChanged();
    onIlluminantChanged();
    updateModelControls();
    statusBar()->showMessage(tr("Open a chart image (camera CCM and gamma OFF), then click the centers of "
                                "patches 1 → 2 → 3 → 4 as shown in the chart panel."));
}

// ---------------------------------------------------------------- UI construction

QWidget* MainWindow::buildSettingsPanel() {
    auto* panel = new QWidget;
    auto* v = new QVBoxLayout(panel);

    auto* modeRow = new QHBoxLayout;
    mode_ = new QComboBox;
    mode_->addItem(tr("Color chart"), 0);
    mode_->addItem(tr("Point mapping (no chart)"), 1);
    mode_->setToolTip(tr("Point mapping: click the same surface points in a reference image (e.g. phone photo)\n"
                         "and in the camera image; the CCM is fitted to the reference colors."));
    modeRow->addWidget(new QLabel(tr("Mode")));
    modeRow->addWidget(mode_, 1);
    v->addLayout(modeRow);
    connect(mode_, &QComboBox::currentIndexChanged, this, [this] { setMode(mode_->currentData().toInt()); });

    // Image
    auto* gImg = new QGroupBox(tr("1. Image"));
    auto* fImg = new QFormLayout(gImg);
    auto* openRow = new QHBoxLayout;
    auto* openBtn = new QPushButton(tr("Open image…"));
    auto* rawBtn = new QPushButton(tr("Open RAW…"));
    openRow->addWidget(openBtn);
    openRow->addWidget(rawBtn);
    fImg->addRow(openRow);
    imageInfo_ = new QLabel(tr("No image"));
    imageInfo_->setWordWrap(true);
    fImg->addRow(imageInfo_);
    dataBits_ = intSpin(8, 16, 8);
    dataBits_->setToolTip(tr("Significant bits of the pixel data. White level = 2^bits - 1.\n"
                             "Use 16 for MSB-aligned data in a 16-bit container."));
    fImg->addRow(tr("Data bits"), dataBits_);
    rawDisplay_ = new QCheckBox(tr("Original pixel values (no gain / gamma)"));
    rawDisplay_->setChecked(true);
    rawDisplay_->setToolTip(tr("Show the image as stored, like other image viewers.\n"
                               "Off: subtract black, apply the display gain and a 2.2 gamma so linear data "
                               "looks natural.\nDisplay only - the CCM computation is not affected."));
    fImg->addRow(rawDisplay_);
    auto* dispRow = new QHBoxLayout;
    autoStretch_ = new QCheckBox(tr("Auto"));
    autoStretch_->setChecked(true);
    displayGain_ = doubleSpin(0.1, 100.0, 1.0, 2, 0.1);
    displayGain_->setEnabled(false);
    autoStretch_->setEnabled(false);
    dispRow->addWidget(autoStretch_);
    dispRow->addWidget(displayGain_);
    fImg->addRow(tr("Display gain"), dispRow);
    connect(rawDisplay_, &QCheckBox::toggled, this, [this](bool raw) {
        autoStretch_->setEnabled(!raw);
        displayGain_->setEnabled(!raw && !autoStretch_->isChecked());
        updatePreview();
        if (pointMode()) updatePointTable();
    });
    v->addWidget(gImg);
    connect(openBtn, &QPushButton::clicked, this, &MainWindow::openImage);
    connect(rawBtn, &QPushButton::clicked, this, &MainWindow::openRaw);
    connect(dataBits_, &QSpinBox::valueChanged, this, [this] {
        invalidateResult();
        updatePreview();
        updateOverlay();
    });
    connect(autoStretch_, &QCheckBox::toggled, this, [this](bool on) {
        displayGain_->setEnabled(!on && !rawDisplay_->isChecked());
        updatePreview();
    });
    connect(displayGain_, &QDoubleSpinBox::valueChanged, this, &MainWindow::updatePreview);

    // Black level
    auto* gBlack = new QGroupBox(tr("2. Black level / clipping"));
    auto* fBlack = new QFormLayout(gBlack);
    blackMode_ = new QComboBox;
    blackMode_->addItem(tr("Manual (per channel DN)"), int(BlackMode::Manual));
    blackMode_->addItem(tr("Estimate from chart (regression)"), int(BlackMode::FromChart));
    blackMode_->addItem(tr("Zero"), int(BlackMode::Zero));
    fBlack->addRow(tr("Mode"), blackMode_);
    auto* blackRow = new QHBoxLayout;
    const char* names[3] = {"R", "G", "B"};
    for (int i = 0; i < 3; ++i) {
        black_[i] = doubleSpin(0, 65535, 0, 1);
        black_[i]->setPrefix(QString(names[i]) + " ");
        blackRow->addWidget(black_[i]);
        connect(black_[i], &QDoubleSpinBox::valueChanged, this, [this] {
            invalidateResult();
            updatePreview();
            updateOverlay();
        });
    }
    fBlack->addRow(tr("Black"), blackRow);
    satPct_ = doubleSpin(50, 100, 98, 1);
    satPct_->setSuffix(" %");
    satPct_->setToolTip(tr("Pixels at or above this fraction of the white level count as clipped.\n"
                           "Patches with more than 1% clipped pixels are excluded."));
    fBlack->addRow(tr("Clip threshold"), satPct_);
    v->addWidget(gBlack);
    connect(blackMode_, &QComboBox::currentIndexChanged, this, [this] {
        const bool manual = BlackMode(blackMode_->currentData().toInt()) == BlackMode::Manual;
        for (auto* b : black_) b->setEnabled(manual);
        if (BlackMode(blackMode_->currentData().toInt()) == BlackMode::Zero)
            for (auto* b : black_) b->setValue(0);
        invalidateResult();
    });
    connect(satPct_, &QDoubleSpinBox::valueChanged, this, [this] {
        invalidateResult();
        updateOverlay();
    });

    // Illuminant
    auto* gIll = new QGroupBox(tr("3. Illuminant"));
    auto* fIll = new QFormLayout(gIll);
    illuminant_ = new QComboBox;
    for (const auto& il : illuminantPresets()) illuminant_->addItem(trCore(il.name), qs(il.id));
    illuminant_->addItem(tr("Custom CCT…"), "CCT");
    illuminant_->setCurrentIndex(illuminant_->findData("D65"));
    fIll->addRow(tr("Light"), illuminant_);
    customCct_ = intSpin(1700, 25000, 3200);
    customCct_->setSuffix(" K");
    fIll->addRow(tr("Custom CCT"), customCct_);
    illumInfo_ = new QLabel;
    illumInfo_->setWordWrap(true);
    fIll->addRow(illumInfo_);
    adaptWhite_ = new QCheckBox(tr("White balance to output white (recommended)"));
    adaptWhite_->setChecked(true);
    adaptWhite_->setToolTip(tr("On: neutrals map to neutral output (CCM includes white balance).\n"
                               "Off: keep the color cast of the illuminant in the target."));
    fIll->addRow(adaptWhite_);
    outSpace_ = new QComboBox;
    for (const auto& s : rgbSpaces()) outSpace_->addItem(trCore(s.name), qs(s.id));
    fIll->addRow(tr("Output space"), outSpace_);
    v->addWidget(gIll);
    connect(illuminant_, &QComboBox::currentIndexChanged, this, &MainWindow::onIlluminantChanged);
    connect(customCct_, &QSpinBox::valueChanged, this, &MainWindow::onIlluminantChanged);
    connect(adaptWhite_, &QCheckBox::toggled, this, &MainWindow::invalidateResult);
    connect(outSpace_, &QComboBox::currentIndexChanged, this, &MainWindow::invalidateResult);

    // Chart
    auto* gChart = new QGroupBox(tr("4. Color chart"));
    chartGroup_ = gChart;
    auto* fChart = new QFormLayout(gChart);
    chart_ = new QComboBox;
    for (const auto& c : chartPresets()) chart_->addItem(trCore(c.name), qs(c.id));
    fChart->addRow(tr("Chart"), chart_);
    auto* gridRow = new QHBoxLayout;
    rows_ = intSpin(1, 64, 4);
    cols_ = intSpin(1, 64, 6);
    rows_->setPrefix(tr("rows "));
    cols_->setPrefix(tr("cols "));
    gridRow->addWidget(rows_);
    gridRow->addWidget(cols_);
    fChart->addRow(tr("Grid"), gridRow);
    auto* refRow = new QHBoxLayout;
    auto* refBtn = new QPushButton(tr("Load reference…"));
    refBtn->setToolTip(tr("CGATS (LAB_L/LAB_A/LAB_B or XYZ, D50) or CSV with [name,]L,a,b rows."));
    dataOrder_ = new QComboBox;
    dataOrder_->addItem(tr("Row-major"), false);
    dataOrder_->addItem(tr("Column-major"), true);
    refRow->addWidget(refBtn);
    refRow->addWidget(dataOrder_);
    fChart->addRow(tr("Reference"), refRow);
    chartInfo_ = new QLabel;
    chartInfo_->setWordWrap(true);
    fChart->addRow(chartInfo_);
    sampleSize_ = doubleSpin(10, 90, 50, 0, 5);
    sampleSize_->setSuffix(tr(" % of pitch"));
    fChart->addRow(tr("Sample size"), sampleSize_);
    auto* orientRow = new QHBoxLayout;
    auto* autoBtn = new QPushButton(tr("Auto orientation"));
    autoBtn->setToolTip(tr("Tries all 8 corner orders (rotations and mirror) and keeps the one whose\n"
                           "patch colors fit the chart reference best."));
    auto* rotBtn = new QPushButton(tr("Rotate 90°"));
    auto* flipBtn = new QPushButton(tr("Flip"));
    auto* clearBtn = new QPushButton(tr("Clear corners"));
    orientRow->addWidget(autoBtn);
    orientRow->addWidget(rotBtn);
    orientRow->addWidget(flipBtn);
    orientRow->addWidget(clearBtn);
    fChart->addRow(orientRow);
    autoOrient_ = new QCheckBox(tr("Fix the patch order automatically when computing"));
    autoOrient_->setChecked(true);
    autoOrient_->setToolTip(tr("If the clicked corners are rotated or mirrored relative to the chart,\n"
                               "the order that fits the reference best is used instead."));
    fChart->addRow(autoOrient_);
    connect(autoBtn, &QPushButton::clicked, this, &MainWindow::autoOrient);
    connect(autoOrient_, &QCheckBox::toggled, this, &MainWindow::invalidateResult);
    pointGroup_ = buildPointGroup();
    v->addWidget(pointGroup_);
    v->addWidget(gChart);
    connect(chart_, &QComboBox::currentIndexChanged, this, &MainWindow::onChartChanged);
    auto gridChanged = [this] {
        if (!chartSpec_.customGrid) return;
        resizeChart(chartSpec_, rows_->value(), cols_->value());
        excluded_.assign(chartSpec_.count(), false);
        referencePath_.clear();
        onChartChanged();
    };
    connect(rows_, &QSpinBox::valueChanged, this, gridChanged);
    connect(cols_, &QSpinBox::valueChanged, this, gridChanged);
    connect(refBtn, &QPushButton::clicked, this, &MainWindow::loadReference);
    connect(sampleSize_, &QDoubleSpinBox::valueChanged, this, [this] {
        invalidateResult();
        updateOverlay();
    });
    auto reorder = [this](ChartCorners (*fn)(const ChartCorners&)) {
        if (!cornersReady()) return;
        setChartCorners(fn(chartCorners()));
        invalidateResult();
    };
    connect(rotBtn, &QPushButton::clicked, this, [reorder] { reorder([](const ChartCorners& c) { return rotateCorners(c, 1); }); });
    connect(flipBtn, &QPushButton::clicked, this, [reorder] { reorder(flipCorners); });
    connect(clearBtn, &QPushButton::clicked, view_, &ImageView::clearCorners);

    // Model
    auto* gModel = new QGroupBox(tr("5. CCM model"));
    auto* fModel = new QFormLayout(gModel);
    model_ = new QComboBox;
    for (Model m : {Model::M3x3, Model::M3x3RowSum, Model::M3x4, Model::WbM3x3, Model::RootPoly3x6})
        model_->addItem(trCore(modelName(m)), int(m));
    fModel->addRow(tr("Model"), model_);
    objective_ = new QComboBox;
    objective_->addItem(tr("Linear RGB least squares"), int(Objective::LinearLS));
    objective_->addItem(tr("Minimize ΔE2000 (iterative)"), int(Objective::DeltaE2000));
    fModel->addRow(tr("Objective"), objective_);
    separateGain_ = new QCheckBox(tr("Separate exposure gain k from matrix"));
    separateGain_->setChecked(true);
    separateGain_->setToolTip(tr("Report the exposure gain separately so that matrix coefficients stay small.\n"
                                 "Apply k with the sensor/ISP gain."));
    fModel->addRow(separateGain_);
    auto* boundRow = new QHBoxLayout;
    boundCoef_ = new QCheckBox(tr("Limit"));
    coefMin_ = doubleSpin(-1000, 0, -16, 3);
    coefMax_ = doubleSpin(0, 1000, 16, 3);
    boundRow->addWidget(boundCoef_);
    boundRow->addWidget(coefMin_);
    boundRow->addWidget(new QLabel("…"));
    boundRow->addWidget(coefMax_);
    fModel->addRow(tr("Coefficients"), boundRow);
    offsetMode_ = new QComboBox;
    offsetMode_->addItem(tr("Free (±)"), int(OffsetMode::Free));
    offsetMode_->addItem(tr("Non-negative only"), int(OffsetMode::NonNegative));
    fModel->addRow(tr("Offset"), offsetMode_);
    neutralWeight_ = doubleSpin(0.1, 100, 1, 1, 0.5);
    neutralWeight_->setToolTip(tr("Relative weight of gray patches in the fit."));
    fModel->addRow(tr("Neutral weight"), neutralWeight_);
    v->addWidget(gModel);
    for (QWidget* w : std::initializer_list<QWidget*>{model_, objective_, offsetMode_})
        connect(qobject_cast<QComboBox*>(w), &QComboBox::currentIndexChanged, this, [this] {
            updateModelControls();
            invalidateResult();
        });
    connect(separateGain_, &QCheckBox::toggled, this, &MainWindow::invalidateResult);
    connect(boundCoef_, &QCheckBox::toggled, this, [this] {
        updateModelControls();
        invalidateResult();
    });

    // Quantization
    auto* gQ = new QGroupBox(tr("6. Hardware registers"));
    auto* fQ = new QFormLayout(gQ);
    quantEnable_ = new QCheckBox(tr("Quantize to fixed point"));
    fQ->addRow(quantEnable_);
    auto* bitsRow = new QHBoxLayout;
    intBits_ = intSpin(0, 16, 4);
    fracBits_ = intSpin(0, 24, 8);
    intBits_->setPrefix(tr("int "));
    fracBits_->setPrefix(tr("frac "));
    quantSigned_ = new QCheckBox(tr("signed"));
    quantSigned_->setChecked(true);
    bitsRow->addWidget(intBits_);
    bitsRow->addWidget(fracBits_);
    bitsRow->addWidget(quantSigned_);
    fQ->addRow(tr("Format"), bitsRow);
    preserveSum_ = new QCheckBox(tr("Preserve row sums when rounding"));
    preserveSum_->setChecked(true);
    fQ->addRow(preserveSum_);
    offsetBits_ = intSpin(8, 16, 12);
    offsetBits_->setSuffix(tr(" bit"));
    fQ->addRow(tr("Offset DN depth"), offsetBits_);
    v->addWidget(gQ);
    connect(quantEnable_, &QCheckBox::toggled, this, [this] {
        updateModelControls();
        invalidateResult();
    });
    for (QSpinBox* s : {intBits_, fracBits_, offsetBits_})
        connect(s, &QSpinBox::valueChanged, this, &MainWindow::invalidateResult);
    for (QCheckBox* c : {quantSigned_, preserveSum_}) connect(c, &QCheckBox::toggled, this, &MainWindow::invalidateResult);
    for (QDoubleSpinBox* s : {coefMin_, coefMax_, neutralWeight_})
        connect(s, &QDoubleSpinBox::valueChanged, this, &MainWindow::invalidateResult);

    auto* computeBtn = new QPushButton(tr("Compute CCM"));
    computeBtn->setMinimumHeight(36);
    QFont f = computeBtn->font();
    f.setBold(true);
    computeBtn->setFont(f);
    connect(computeBtn, &QPushButton::clicked, this, &MainWindow::compute);
    v->addWidget(computeBtn);

    auto* exportBox = new QGroupBox(tr("Export"));
    auto* eg = new QGridLayout(exportBox);
    auto addBtn = [&](const QString& text, int r, int c, void (MainWindow::*slot)()) {
        auto* b = new QPushButton(text);
        connect(b, &QPushButton::clicked, this, slot);
        eg->addWidget(b, r, c);
    };
    addBtn(tr("JSON"), 0, 0, &MainWindow::exportJson);
    addBtn(tr("CSV"), 0, 1, &MainWindow::exportCsv);
    addBtn(tr("C header"), 1, 0, &MainWindow::exportHeader);
    addBtn(tr("Registers"), 1, 1, &MainWindow::exportRegisters);
    addBtn(tr("Save corrected image…"), 2, 0, &MainWindow::saveCorrectedImage);
    v->addWidget(exportBox);
    v->addStretch();
    return panel;
}

void MainWindow::buildMenus() {
    auto* file = menuBar()->addMenu(tr("&File"));
    file->addAction(tr("Open image…"), QKeySequence::Open, this, &MainWindow::openImage);
    file->addAction(tr("Open RAW…"), this, &MainWindow::openRaw);
    file->addSeparator();
    file->addAction(tr("Load session…"), this, &MainWindow::loadSession);
    file->addAction(tr("Save session…"), QKeySequence::Save, this, &MainWindow::saveSession);
    file->addSeparator();
    file->addAction(tr("Quit"), QKeySequence::Quit, qApp, &QApplication::quit);
    auto* viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(tr("Fit image"), QKeySequence(Qt::Key_F), view_, &ImageView::fitToWindow);
    auto* langMenu = viewMenu->addMenu(tr("Language"));
    auto* langGroup = new QActionGroup(this);
    const Language current = currentLanguage();
    for (Language lang : {Language::English, Language::Korean}) {
        QAction* a = langMenu->addAction(languageName(lang));
        a->setCheckable(true);
        a->setChecked(lang == current);
        langGroup->addAction(a);
        connect(a, &QAction::triggered, this, [this, lang] { switchLanguage(int(lang)); });
    }
    auto* help = menuBar()->addMenu(tr("&Help"));
    help->addAction(tr("How to use"), this, [this] {
        QMessageBox::information(
            this, tr("How to use"),
            tr("1. Capture the chart with the camera's CCM and gamma disabled, evenly lit, no clipping.\n"
               "2. Open the image and set the data bits and black level.\n"
               "3. Choose the illuminant and chart; load the chart's reference file if required.\n"
               "4. Click the CENTERS of four corner patches in order:\n"
               "   1 = first patch (top-left), 2 = end of first row,\n"
               "   3 = last patch, 4 = start of last row.\n"
               "   Drag handles to adjust; use Rotate/Flip if the grid is mis-ordered.\n"
               "5. Choose the model and constraints, then Compute.\n\n"
               "Zoom: mouse wheel. Pan: right/middle drag or Ctrl+drag."));
    });
}

void MainWindow::switchLanguage(int langValue) {
    const Language lang = Language(langValue);
    if (lang == currentLanguage()) return;
    saveLanguage(lang);
    installLanguage(lang);
    // Widgets are built with translated text, so rebuild the window and carry the state over.
    auto* w = new MainWindow;
    w->setAttribute(Qt::WA_DeleteOnClose);
    w->restoreState(settingsJson(), haveResult_);
    if (isMaximized()) w->showMaximized();
    else {
        w->setGeometry(geometry());
        w->show();
    }
    close();
}

void MainWindow::restoreState(const QJsonObject& settings, bool recompute) {
    applySettingsJson(settings, QString());
    if (recompute && cornersReady()) {
        batch_ = true;
        compute();
        batch_ = false;
    }
}

// ---------------------------------------------------------------- image

void MainWindow::openImage() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Open chart image"), QString(),
                                                      tr("Images (*.bmp *.png *.tif *.tiff *.jpg *.jpeg *.pgm *.ppm);;All files (*)"));
    if (path.isEmpty()) return;
    LoadedImage img;
    std::string err;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool ok = loadImageFile(path.toStdString(), img, err);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::warning(this, tr("Open image"), qs(err));
        return;
    }
    imageIsRaw_ = false;
    setImage(img);
}

void MainWindow::openRaw() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Open RAW"), QString(), tr("RAW (*.raw *.bin);;All files (*)"));
    if (path.isEmpty()) return;
    RawImportDialog dlg(rawParams_, this);
    if (dlg.exec() != QDialog::Accepted) return;
    rawParams_ = dlg.params();
    LoadedImage img;
    std::string err;
    if (!loadRawFile(path.toStdString(), rawParams_, img, err)) {
        QMessageBox::warning(this, tr("Open RAW"), qs(err));
        return;
    }
    imageIsRaw_ = true;
    setImage(img);
}

void MainWindow::setImage(const LoadedImage& img) {
    image_ = img;
    preview_ = downscale(img.rgb, kPreviewMaxDim);
    previewScale_ = double(preview_.cols) / img.rgb.cols;
    dataBits_->blockSignals(true);
    dataBits_->setValue(img.containerBits);
    dataBits_->blockSignals(false);
    imageInfo_->setText(tr("%1\n%2 x %3, %4-bit container")
                            .arg(QFileInfo(qs(img.path)).fileName())
                            .arg(img.rgb.cols)
                            .arg(img.rgb.rows)
                            .arg(img.containerBits));
    view_->clearCorners();
    invalidateResult();
    updatePreview();
    // fit once the view has its final size (also right after startup)
    QTimer::singleShot(0, view_, &ImageView::fitToWindow);
    QTimer::singleShot(0, camView_, &ImageView::fitToWindow);
}

double MainWindow::whiteLevel() const { return std::ldexp(1.0, dataBits_->value()) - 1.0; }

cv::Vec3d MainWindow::blackLevel() const { return cv::Vec3d(black_[0]->value(), black_[1]->value(), black_[2]->value()); }

void MainWindow::updatePreview() {
    if (preview_.empty()) return;
    const cv::Vec3d black = blackLevel();
    const double white = whiteLevel();
    cv::Mat f;
    if (rawDisplay_->isChecked()) {
        // Stored values as-is (like other image viewers): DN / white level, no black, gain or gamma
        preview_.convertTo(f, CV_32FC3, 1.0 / std::max(white, 1.0));
        cv::max(f, 0.0, f);
        cv::min(f, 1.0, f);
    } else {
        preview_.convertTo(f, CV_32FC3);
        f -= cv::Scalar(black[0], black[1], black[2]);
        f = f.mul(cv::Scalar(1.0 / std::max(white - black[0], 1.0), 1.0 / std::max(white - black[1], 1.0),
                             1.0 / std::max(white - black[2], 1.0)));
        double gain = displayGain_->value();
        if (autoStretch_->isChecked()) {
            cv::Mat flat = f.reshape(1, 1).clone();
            const int n = flat.cols;
            const int k = std::clamp(int(n * 0.995), 0, n - 1);
            std::nth_element(flat.ptr<float>(), flat.ptr<float>() + k, flat.ptr<float>() + n);
            const double p = flat.at<float>(k);
            gain = p > 1e-6 ? 0.95 / p : 1.0;
            displayGain_->blockSignals(true);
            displayGain_->setValue(std::clamp(gain, 0.1, 100.0));
            displayGain_->blockSignals(false);
        }
        f *= gain;
        cv::max(f, 0.0, f);
        cv::min(f, 1.0, f);
        cv::pow(f, 1.0 / 2.2, f);
    }
    cv::Mat u8;
    f.convertTo(u8, CV_8UC3, 255.0);
    QImage qi(u8.data, u8.cols, u8.rows, int(u8.step), QImage::Format_RGB888);
    view_->setImage(qi.copy(), previewScale_);
    camView_->setImage(qi.copy(), previewScale_);
    updateOverlay();
}

// ---------------------------------------------------------------- illuminant / chart

Illuminant MainWindow::currentIlluminant() const {
    const QString id = illuminant_->currentData().toString();
    if (id == "CCT") return illuminantFromCct(customCct_->value());
    const Illuminant* il = findIlluminant(id.toStdString());
    return il ? *il : illuminantPresets().front();
}

void MainWindow::onIlluminantChanged() {
    customCct_->setEnabled(illuminant_->currentData().toString() == "CCT");
    const Illuminant il = currentIlluminant();
    illumInfo_->setText(tr("x = %1, y = %2, CCT ≈ %3 K\n%4")
                            .arg(il.x, 0, 'f', 4)
                            .arg(il.y, 0, 'f', 4)
                            .arg(il.cct, 0, 'f', 0)
                            .arg(trCore(il.note)));
    invalidateResult();
}

void MainWindow::onChartChanged() {
    const QString id = chart_->currentData().toString();
    if (chartSpec_.id != id.toStdString()) {
        for (const auto& c : chartPresets())
            if (c.id == id.toStdString()) chartSpec_ = c;
        referencePath_.clear();
    }
    rows_->blockSignals(true);
    cols_->blockSignals(true);
    rows_->setValue(chartSpec_.rows);
    cols_->setValue(chartSpec_.cols);
    rows_->setEnabled(chartSpec_.customGrid);
    cols_->setEnabled(chartSpec_.customGrid);
    rows_->blockSignals(false);
    cols_->blockSignals(false);
    excluded_.assign(chartSpec_.count(), false);

    QString status;
    if (!referencePath_.isEmpty()) status = tr("Reference: %1").arg(QFileInfo(referencePath_).fileName());
    else if (chartSpec_.builtinReference) status = tr("Built-in reference.");
    else status = tr("<b>Reference file required.</b>");
    chartInfo_->setText(tr("%1 patches, %2 neutral. %3<br>%4")
                            .arg(chartSpec_.count())
                            .arg(chartSpec_.neutralIndices().size())
                            .arg(status, trCore(chartSpec_.note)));
    invalidateResult();
    updateOverlay();
}

void MainWindow::loadReference() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Load chart reference"), QString(),
                                                      tr("Reference (*.txt *.cgats *.it8 *.cie *.csv);;All files (*)"));
    if (path.isEmpty()) return;
    ChartSpec c = chartSpec_;
    std::string err;
    if (!loadReferenceFile(path.toStdString(), dataOrder_->currentData().toBool(), c, err)) {
        QMessageBox::warning(this, tr("Load reference"), qs(err));
        return;
    }
    if (!err.empty()) QMessageBox::information(this, tr("Load reference"), qs(err));
    chartSpec_ = c;
    referencePath_ = path;
    onChartChanged();
}

// ---------------------------------------------------------------- sampling

bool MainWindow::cornersReady() const { return !image_.rgb.empty() && view_->corners().size() == 4; }

ChartCorners MainWindow::chartCorners() const {
    const auto c = view_->corners();
    ChartCorners out;
    for (int i = 0; i < 4; ++i) out[i] = cv::Point2f(float(c[i].x()), float(c[i].y()));
    return out;
}

SampleSettings MainWindow::sampleSettings() const {
    SampleSettings s;
    s.rows = chartSpec_.rows;
    s.cols = chartSpec_.cols;
    s.sampleFraction = sampleSize_->value() / 100.0;
    s.black = blackLevel();
    s.fullScale = whiteLevel();
    s.saturationFraction = satPct_->value() / 100.0;
    return s;
}

std::vector<bool> MainWindow::includedMask() const {
    if (pointMode()) {
        std::vector<bool> inc(pairs_.size(), false);
        for (size_t i = 0; i < pairs_.size(); ++i)
            inc[i] = pairs_[i].enabled && i < stats_.size() && i < refSamples_.size() && stats_[i].pixelCount > 0 &&
                     refSamples_[i].pixelCount > 0 && stats_[i].saturatedRatio <= 0.01 &&
                     refSamples_[i].saturatedRatio <= 0.01;
        return inc;
    }
    std::vector<bool> inc(chartSpec_.count(), false);
    for (int i = 0; i < chartSpec_.count(); ++i) {
        const bool sampled = i < int(stats_.size()) && stats_[i].pixelCount > 0 && stats_[i].saturatedRatio <= 0.01;
        inc[i] = sampled && chartSpec_.patches[i].hasRef && !(i < int(excluded_.size()) && excluded_[i]);
    }
    return inc;
}

std::vector<bool> MainWindow::neutralMask() const {
    if (pointMode()) {
        std::vector<bool> n(pairs_.size(), false);
        for (size_t i = 0; i < pairs_.size(); ++i) n[i] = pairs_[i].neutral;
        return n;
    }
    std::vector<bool> n(chartSpec_.count(), false);
    for (int i : chartSpec_.neutralIndices()) n[i] = true;
    return n;
}

void MainWindow::updateOverlay() {
    if (pointMode()) {  // black level / clip / image changes resample the point pairs instead
        samplePoints();
        updatePointViews();
        updatePointTable();
        return;
    }
    if (!cornersReady()) {
        view_->setOverlay({}, {});
        stats_.clear();
        return;
    }
    stats_ = samplePatches(image_.rgb, chartCorners(), sampleSettings());
    QVector<QPolygonF> quads;
    QVector<ImageView::PatchState> states;
    for (int i = 0; i < int(stats_.size()); ++i) {
        QPolygonF poly;
        for (const auto& p : stats_[i].quad) poly << QPointF(p.x, p.y);
        quads << poly;
        if (i < int(excluded_.size()) && excluded_[i]) states << ImageView::PatchState::Excluded;
        else if (stats_[i].saturatedRatio > 0.01) states << ImageView::PatchState::Saturated;
        else if (!chartSpec_.patches[i].hasRef) states << ImageView::PatchState::NoReference;
        else states << ImageView::PatchState::Ok;
    }
    view_->setOverlay(quads, states);
}

// ---------------------------------------------------------------- fitting

void MainWindow::updateModelControls() {
    const Model m = Model(model_->currentData().toInt());
    offsetMode_->setEnabled(hasOffset(m));
    separateGain_->setEnabled(m == Model::M3x3 || m == Model::M3x4 || m == Model::RootPoly3x6);
    coefMin_->setEnabled(boundCoef_->isChecked());
    coefMax_->setEnabled(boundCoef_->isChecked());
    for (QWidget* w : std::initializer_list<QWidget*>{intBits_, fracBits_, quantSigned_, preserveSum_, offsetBits_})
        w->setEnabled(quantEnable_->isChecked());
    offsetBits_->setEnabled(quantEnable_->isChecked() && hasOffset(m));
}

FitOptions MainWindow::fitOptions() const {
    FitOptions o;
    o.model = Model(model_->currentData().toInt());
    o.objective = Objective(objective_->currentData().toInt());
    o.separateGain = separateGain_->isChecked();
    o.boundCoefficients = boundCoef_->isChecked();
    o.coefMin = coefMin_->value();
    o.coefMax = coefMax_->value();
    o.offsetMode = OffsetMode(offsetMode_->currentData().toInt());
    o.neutralWeight = neutralWeight_->value();
    o.outputSpace = outSpace_->currentData().toString().toStdString();
    o.quant.enabled = quantEnable_->isChecked();
    o.quant.intBits = intBits_->value();
    o.quant.fracBits = fracBits_->value();
    o.quant.isSigned = quantSigned_->isChecked();
    o.quant.preserveRowSum = preserveSum_->isChecked();
    o.quant.offsetBits = offsetBits_->value();
    return o;
}

void MainWindow::invalidateResult() {
    if (!haveResult_) return;
    haveResult_ = false;
    statusBar()->showMessage(tr("Settings changed - press Compute CCM to update the result."));
}

void MainWindow::compute() {
    if (image_.rgb.empty()) {
        notify(tr("Compute"), tr("Open a chart image first."));
        return;
    }
    orientationWarning_.clear();
    orientationNote_.clear();
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool prepared = pointMode() ? preparePointData() : prepareChartData();
    if (!prepared) {
        QApplication::restoreOverrideCursor();
        return;
    }

    const std::vector<bool> included = includedMask();
    const std::vector<bool> neutral = neutralMask();
    std::vector<cv::Vec3d> cam;
    std::vector<double> weight;
    for (size_t i = 0; i < stats_.size(); ++i) {
        cam.push_back(stats_[i].mean);
        weight.push_back(included[i] ? 1.0 : 0.0);
    }
    const FitOptions opt = fitOptions();
    fit_ = fitCcm(cam, targets_.linearRgb, targets_.lab, neutral, weight, opt);
    QApplication::restoreOverrideCursor();
    if (!fit_.ok) {
        notify(tr("Compute"), qs(fit_.message));
        return;
    }
    eval_ = evaluate(fit_, cam, targets_, included, neutral, opt.outputSpace, fit_.quantized);
    separability_ = analyzeSeparability(stats_, targets_, included, neutral);
    haveResult_ = true;

    results_->showResult(names_, gridCols_, stats_, fit_, eval_, buildSummary(), buildDiagnostics());
    updateCorrectedPreview();
    statusBar()->showMessage(tr("ΔE2000 mean %1, max %2 over %3 patches (WB only: %4)")
                                 .arg(eval_.de00.mean, 0, 'f', 2)
                                 .arg(eval_.de00.max, 0, 'f', 2)
                                 .arg(eval_.de00.count)
                                 .arg(eval_.de00Baseline.mean, 0, 'f', 2) +
                             (orientationNote_.isEmpty() ? QString() : "  |  " + orientationNote_));
    if (!orientationWarning_.isEmpty()) notify(tr("Patch order"), orientationWarning_);
}

bool MainWindow::prepareChartData() {
    if (!cornersReady()) {
        notify(tr("Compute"), tr("Click the centers of the four corner patches first."));
        return false;
    }
    if (chartSpec_.neutralIndices().empty() && !chartSpec_.hasAllReferences()) {
        notify(tr("Compute"), tr("This chart needs a reference file (Load reference…)."));
        return false;
    }
    targets_ = currentTargets();

    // A mis-ordered chart (rotated/mirrored corner clicks) fits far worse than the right order.
    // Checked first and independent of the black level, which a wrong order would corrupt.
    const auto ranked = rankOrientations(image_.rgb, chartCorners(), sampleSettings(), chartSpec_, targets_, excluded_);
    const auto clicked = std::find_if(ranked.begin(), ranked.end(), [](const OrientationCandidate& c) {
        return c.quarterTurns == 0 && !c.flipped;
    });
    if (clicked != ranked.end() && clicked != ranked.begin() &&
        ranked.front().meanDeltaE < 0.6 * clicked->meanDeltaE && clicked->meanDeltaE - ranked.front().meanDeltaE > 2.0) {
        const OrientationCandidate& best = ranked.front();
        if (autoOrient_->isChecked()) {
            setChartCorners(best.corners);
            orientationNote_ = tr("Auto orientation applied: rotated %1 x 90°%2 (chart fit ΔE2000 %3 instead of %4).")
                                   .arg(best.quarterTurns)
                                   .arg(best.flipped ? tr(", mirrored") : QString())
                                   .arg(best.meanDeltaE, 0, 'f', 2)
                                   .arg(clicked->meanDeltaE, 0, 'f', 2);
        } else {
            orientationWarning_ = tr("The patch order looks wrong (chart rotated or mirrored?). With 'Auto orientation' "
                                     "the chart fits with ΔE2000 %1 instead of %2.")
                                      .arg(best.meanDeltaE, 0, 'f', 2)
                                      .arg(clicked->meanDeltaE, 0, 'f', 2);
        }
    }

    // Black level from the chart: sample without black, regress DN on the reference, resample.
    if (BlackMode(blackMode_->currentData().toInt()) == BlackMode::FromChart && orientationWarning_.isEmpty()) {
        SampleSettings s0 = sampleSettings();
        s0.black = cv::Vec3d(0, 0, 0);
        stats_ = samplePatches(image_.rgb, chartCorners(), s0);
        cv::Vec3d black;
        if (estimateBlackFromChart(stats_, targets_, includedMask(), black))
            for (int c = 0; c < 3; ++c) {
                black_[c]->blockSignals(true);
                black_[c]->setValue(black[c]);
                black_[c]->blockSignals(false);
            }
        updatePreview();
    }
    updateOverlay();

    // Linearity diagnostics on DN values
    linearity_ = analyzeLinearity(stats_, chartSpec_, includedMask());
    names_.clear();
    for (const auto& p : chartSpec_.patches) names_.push_back(p.name);
    gridCols_ = chartSpec_.cols;
    return true;
}

void MainWindow::setChartCorners(const ChartCorners& c) {
    view_->setCorners({QPointF(c[0].x, c[0].y), QPointF(c[1].x, c[1].y), QPointF(c[2].x, c[2].y), QPointF(c[3].x, c[3].y)});
    updateOverlay();
}

Targets MainWindow::currentTargets() const {
    TargetOptions to;
    to.outputSpace = outSpace_->currentData().toString().toStdString();
    to.adaptToOutputWhite = adaptWhite_->isChecked();
    return computeTargets(chartSpec_, currentIlluminant(), to);
}

void MainWindow::autoOrient() {
    if (!cornersReady()) {
        notify(tr("Auto orientation"), tr("Click the centers of the four corner patches first."));
        return;
    }
    if (chartSpec_.neutralIndices().empty() && !chartSpec_.hasAllReferences()) {
        notify(tr("Auto orientation"), tr("This chart needs a reference file (Load reference…)."));
        return;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const auto ranked = rankOrientations(image_.rgb, chartCorners(), sampleSettings(), chartSpec_, currentTargets(), excluded_);
    QApplication::restoreOverrideCursor();
    const OrientationCandidate& best = ranked.front();
    setChartCorners(best.corners);
    invalidateResult();
    compute();
    statusBar()->showMessage(tr("Auto orientation: rotated %1 x 90°%2, chart fit ΔE2000 %3 (next best %4)")
                                 .arg(best.quarterTurns)
                                 .arg(best.flipped ? tr(", mirrored") : QString())
                                 .arg(best.meanDeltaE, 0, 'f', 2)
                                 .arg(ranked.size() > 1 ? ranked[1].meanDeltaE : 0.0, 0, 'f', 2));
}

cv::Mat MainWindow::correctImage(const cv::Mat& native, bool srgb) const {
    const cv::Vec3d black = blackLevel();
    const double white = whiteLevel();
    const cv::Vec3d inv(1.0 / std::max(white - black[0], 1.0), 1.0 / std::max(white - black[1], 1.0),
                        1.0 / std::max(white - black[2], 1.0));
    const bool useQ = fit_.quantized;
    const cv::Mat& coef = useQ ? fit_.coefQuantized : fit_.coef;
    const bool linear = fit_.model != Model::RootPoly3x6;

    // 3x4 affine on normalized input
    cv::Matx34f m = cv::Matx34f::zeros();
    if (linear)
        for (int r = 0; r < 3; ++r) {
            for (int c = 0; c < 3; ++c) m(r, c) = float(fit_.gain * coef.at<double>(r, c) * fit_.wbGains[c] * inv[c]);
            double off = hasOffset(fit_.model) ? fit_.gain * coef.at<double>(r, 3) : 0.0;
            for (int c = 0; c < 3; ++c) off -= m(r, c) * black[c];
            m(r, 3) = float(off);
        }

    std::vector<uchar> lut(4097);
    for (int i = 0; i <= 4096; ++i) {
        const double v = i / 4096.0;
        lut[i] = uchar(std::lround(255.0 * (srgb ? srgbEncode(v) : v)));
    }

    cv::Mat out(native.size(), CV_8UC3);
    const int chunk = 256;
    for (int y0 = 0; y0 < native.rows; y0 += chunk) {
        const cv::Range rr(y0, std::min(y0 + chunk, native.rows));
        cv::Mat f;
        native.rowRange(rr).convertTo(f, CV_32FC3);
        cv::Mat o;
        if (linear) {
            cv::transform(f, o, m);
        } else {
            o.create(f.size(), CV_32FC3);
            for (int y = 0; y < f.rows; ++y)
                for (int x = 0; x < f.cols; ++x) {
                    const cv::Vec3f p = f.at<cv::Vec3f>(y, x);
                    cv::Vec3d cam((p[0] - black[0]) * inv[0], (p[1] - black[1]) * inv[1], (p[2] - black[2]) * inv[2]);
                    o.at<cv::Vec3f>(y, x) = cv::Vec3f(applyFit(fit_, cam, useQ));
                }
        }
        for (int y = 0; y < o.rows; ++y) {
            const auto* src = o.ptr<cv::Vec3f>(y);
            auto* dst = out.ptr<cv::Vec3b>(rr.start + y);
            for (int x = 0; x < o.cols; ++x)
                for (int k = 0; k < 3; ++k)
                    dst[x][k] = lut[std::clamp(int(src[x][k] * 4096.0f + 0.5f), 0, 4096)];
        }
    }
    return out;
}

// ---------------------------------------------------------------- reports

QString MainWindow::buildSummary() const {
    QString s;
    if (!orientationWarning_.isEmpty()) s += tr("WARNING: ") + orientationWarning_ + "\n\n";
    if (!orientationNote_.isEmpty()) s += orientationNote_ + "\n\n";
    s += qs(formatMatrixText(fit_));
    s += tr("\nΔE2000 : mean %1  median %2  max %3  (%4 patches)\n")
             .arg(eval_.de00.mean, 0, 'f', 2)
             .arg(eval_.de00.median, 0, 'f', 2)
             .arg(eval_.de00.max, 0, 'f', 2)
             .arg(eval_.de00.count);
    s += tr("ΔE76   : mean %1  max %2\n").arg(eval_.de76.mean, 0, 'f', 2).arg(eval_.de76.max, 0, 'f', 2);
    s += tr("ΔC     : mean %1  max %2\n").arg(eval_.dc.mean, 0, 'f', 2).arg(eval_.dc.max, 0, 'f', 2);
    s += tr("WB only: ΔE2000 mean %1  max %2\n").arg(eval_.de00Baseline.mean, 0, 'f', 2).arg(eval_.de00Baseline.max, 0, 'f', 2);
    s += tr("\nmax |coef| %1   condition number %2   row norms %3 / %4 / %5\n")
             .arg(eval_.maxAbsCoef, 0, 'f', 3)
             .arg(eval_.conditionNumber, 0, 'f', 2)
             .arg(eval_.rowNorms[0], 0, 'f', 2)
             .arg(eval_.rowNorms[1], 0, 'f', 2)
             .arg(eval_.rowNorms[2], 0, 'f', 2);
    if (fit_.quantized) {
        const double range = std::ldexp(1.0, fit_.quant.intBits);
        bool clipped = false;
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < fit_.coefRegisters.cols; ++c) {
                const double v = fit_.coef.at<double>(r, c);
                if (v >= range || v < (fit_.quant.isSigned ? -range : 0.0)) clipped = true;
            }
        if (clipped)
            s += tr("\nWARNING: some coefficients exceed the register range and were clipped. "
                    "Enable 'Limit' with the register range or separate the exposure gain.\n");
    }
    if (pointMode()) {
        s += tr("\nPoint mapping: %1 points, reference %2, output %3, %4\n")
                 .arg(eval_.de00.count)
                 .arg(QFileInfo(qs(refImage_.path)).fileName())
                 .arg(outSpace_->currentText())
                 .arg(matchBrightness_->isChecked() ? tr("brightness matched") : tr("chromaticity only"));
        return s;
    }
    const Illuminant il = currentIlluminant();
    s += tr("\nIlluminant: %1 (%2 K), output %3, %4\n")
             .arg(trCore(il.name))
             .arg(il.cct, 0, 'f', 0)
             .arg(outSpace_->currentText())
             .arg(adaptWhite_->isChecked() ? tr("white balanced") : tr("illuminant cast kept"));
    return s;
}

QString MainWindow::buildDiagnostics() const {
    QString d;
    if (pointMode()) d += pointDiagnostics();
    d += tr("== Linearity (neutral patches) ==\n");
    if (pointMode()) {
        d += tr("Not available in point mapping (reference brightness is not used).\n");
    } else if (linearity_.ok) {
        d += tr("Black estimate (DN): %1 / %2 / %3\n")
                 .arg(linearity_.black[0], 0, 'f', 1)
                 .arg(linearity_.black[1], 0, 'f', 1)
                 .arg(linearity_.black[2], 0, 'f', 1);
        d += tr("Linear fit R² (worst channel): %1\n").arg(linearity_.r2, 0, 'f', 4);
        d += tr("Response exponent: %1 (1.0 = linear)\n").arg(linearity_.exponent, 0, 'f', 3);
        if (linearity_.exponent < 0.8 || linearity_.exponent > 1.25)
            d += tr("WARNING: response is not linear - camera gamma/LUT may be ON. A CCM must be applied to linear data.\n");
        if (linearity_.r2 < 0.995) d += tr("WARNING: poor linear fit - check uneven lighting, glare or clipping.\n");
    } else {
        d += tr("Not enough neutral patches.\n");
    }

    d += tr("\n== Clipping ==\n");
    int sat = 0;
    for (int i = 0; i < int(stats_.size()); ++i)
        if (stats_[i].saturatedRatio > 0.01) {
            d += tr("Patch %1 (%2): %3 % clipped - excluded\n")
                     .arg(i + 1)
                     .arg(qs(names_[i]))
                     .arg(stats_[i].saturatedRatio * 100, 0, 'f', 1);
            ++sat;
        }
    if (!sat) d += tr("None.\n");

    d += tr("\n== Color separability (camera) ==\n");
    d += tr("Camera chroma / expected chroma (median): %1\n").arg(separability_.chromaRatio, 0, 'f', 3);
    if (separability_.chromaRatio > 0 && separability_.chromaRatio < 0.3)
        d += tr("WARNING: the camera sees very little color. Typical causes: halogen/IR-rich lighting without an\n"
                "IR-cut filter, or strong channel crosstalk. A CCM must amplify noise strongly here; fix the\n"
                "optics/lighting first if possible.\n");
    if (separability_.issues.empty()) {
        d += tr("No strongly different color pairs look alike to the camera.\n");
    } else {
        d += tr("Pairs that differ in reality (ΔE00 ≥ 20) but are within 2x pixel noise in camera chromaticity.\n"
                "No 3x3 CCM can separate these; expect them to come out alike:\n");
        int shown = 0;
        for (const auto& is : separability_.issues) {
            d += tr("  %1 (%2)  vs  %3 (%4): distance %5 σ, reference ΔE00 %6\n")
                     .arg(is.a + 1)
                     .arg(qs(names_[is.a]))
                     .arg(is.b + 1)
                     .arg(qs(names_[is.b]))
                     .arg(is.distance, 0, 'f', 2)
                     .arg(is.referenceDE, 0, 'f', 1);
            if (++shown >= 40) break;
        }
    }

    d += tr("\n== Noise amplification ==\n");
    d += tr("Row norms %1 / %2 / %3 (1.0 = no amplification), condition number %4\n")
             .arg(eval_.rowNorms[0], 0, 'f', 2)
             .arg(eval_.rowNorms[1], 0, 'f', 2)
             .arg(eval_.rowNorms[2], 0, 'f', 2)
             .arg(eval_.conditionNumber, 0, 'f', 2);
    if (std::max({eval_.rowNorms[0], eval_.rowNorms[1], eval_.rowNorms[2]}) > 4.0)
        d += tr("WARNING: large coefficients amplify sensor noise strongly (colored noise in flat areas).\n");
    return d;
}

void MainWindow::notify(const QString& title, const QString& message) {
    lastError_ = message;
    if (batch_) statusBar()->showMessage(message);
    else QMessageBox::information(this, title, message);
}

ExportInfo MainWindow::exportInfo() const {
    ExportInfo info;
    info.chartName = pointMode() ? "Point mapping: " + QFileInfo(qs(refImage_.path)).fileName().toStdString()
                                 : chartSpec_.name;
    const Illuminant il = currentIlluminant();
    info.illuminantName = il.name;
    info.cct = il.cct;
    info.outputSpaceName = outSpace_->currentText().toStdString();
    info.imagePath = image_.path;
    return info;
}

bool MainWindow::writeText(const QString& filter, const QString& suffix, const std::string& text) {
    if (!haveResult_) {
        QMessageBox::information(this, tr("Export"), tr("Compute the CCM first."));
        return false;
    }
    QString path = QFileDialog::getSaveFileName(this, tr("Export"), "ccm." + suffix, filter);
    if (path.isEmpty()) return false;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Export"), tr("Cannot write %1").arg(path));
        return false;
    }
    f.write(QByteArray::fromStdString(text));
    statusBar()->showMessage(tr("Saved %1").arg(path));
    return true;
}

void MainWindow::exportCsv() { writeText(tr("CSV (*.csv)"), "csv", ccm::exportCsv(fit_, eval_, names_)); }
void MainWindow::exportHeader() { writeText(tr("C/C++ header (*.h)"), "h", ccm::exportCHeader(fit_, exportInfo())); }
void MainWindow::exportRegisters() { writeText(tr("Text (*.txt)"), "txt", ccm::exportRegisters(fit_, exportInfo())); }

void MainWindow::exportJson() { writeText(tr("JSON (*.json)"), "json", resultJson().toStdString()); }

QByteArray MainWindow::resultJson() const {
    QJsonObject res;
    res["model"] = modelName(fit_.model);
    res["matrix"] = matToJson(fit_.coef);
    res["exposure_gain"] = fit_.gain;
    res["wb_gains"] = vecToJson(fit_.wbGains);
    if (fit_.quantized) {
        res["registers"] = matToJson(fit_.coefRegisters);
        res["matrix_quantized"] = matToJson(fit_.coefQuantized);
        QJsonArray off;
        for (int v : fit_.offsetDN) off.append(v);
        res["offset_dn"] = off;
    }
    QJsonObject stats;
    stats["de2000_mean"] = eval_.de00.mean;
    stats["de2000_median"] = eval_.de00.median;
    stats["de2000_max"] = eval_.de00.max;
    stats["de76_mean"] = eval_.de76.mean;
    stats["wb_only_de2000_mean"] = eval_.de00Baseline.mean;
    stats["condition_number"] = eval_.conditionNumber;
    stats["camera_chroma_ratio"] = separability_.chromaRatio;
    res["stats"] = stats;
    QJsonArray warnings;
    if (!orientationWarning_.isEmpty()) warnings.append(orientationWarning_);
    res["warnings"] = warnings;
    res["orientation_note"] = orientationNote_;
    QJsonArray patches;
    for (const auto& p : eval_.patches) {
        QJsonObject o;
        o["index"] = p.index + 1;
        o["name"] = qs(names_[p.index]);
        o["included"] = p.included;
        o["camera"] = vecToJson(p.cam);
        o["target"] = vecToJson(p.target);
        o["output"] = vecToJson(p.predicted);
        o["de2000"] = p.de00;
        patches.append(o);
    }
    res["patches"] = patches;
    QJsonObject root;
    root["settings"] = settingsJson();
    root["result"] = res;
    return QJsonDocument(root).toJson();
}

void MainWindow::saveCorrectedImage() {
    if (!haveResult_) {
        QMessageBox::information(this, tr("Save"), tr("Compute the CCM first."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(this, tr("Save corrected image"), "corrected.png",
                                                      tr("Images (*.png *.bmp *.tif)"));
    if (path.isEmpty()) return;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    const bool srgb = !results_->correctedLinear();  // save what the tab shows
    cv::Mat out = correctImage(image_.rgb, srgb);
    cv::cvtColor(out, out, cv::COLOR_RGB2BGR);
    const bool ok = cv::imwrite(path.toStdString(), out);
    QApplication::restoreOverrideCursor();
    if (!ok) QMessageBox::warning(this, tr("Save"), tr("Cannot write %1").arg(path));
    else if (srgb) statusBar()->showMessage(tr("Saved %1 (sRGB-encoded, 8-bit)").arg(path));
    else statusBar()->showMessage(tr("Saved %1 (linear, no gamma, 8-bit)").arg(path));
}

void MainWindow::updateCorrectedPreview() {
    if (!haveResult_ || preview_.empty()) return;
    cv::Mat corr = correctImage(preview_, !results_->correctedLinear());
    QImage qi(corr.data, corr.cols, corr.rows, int(corr.step), QImage::Format_RGB888);
    results_->showCorrected(qi.copy());
}

// ---------------------------------------------------------------- session

QJsonObject MainWindow::settingsJson() const {
    QJsonObject o;
    o["image"] = qs(image_.path);
    o["image_is_raw"] = imageIsRaw_;
    if (imageIsRaw_) {
        QJsonObject r;
        r["width"] = rawParams_.width;
        r["height"] = rawParams_.height;
        r["bytes_per_sample"] = rawParams_.bytesPerSample;
        r["header_bytes"] = rawParams_.headerBytes;
        r["layout"] = int(rawParams_.layout);
        o["raw"] = r;
    }
    o["data_bits"] = dataBits_->value();
    o["black_mode"] = blackMode_->currentData().toInt();
    o["black"] = vecToJson(blackLevel());
    o["clip_percent"] = satPct_->value();
    o["illuminant"] = illuminant_->currentData().toString();
    o["custom_cct"] = customCct_->value();
    o["adapt_to_output_white"] = adaptWhite_->isChecked();
    o["output_space"] = outSpace_->currentData().toString();
    o["chart"] = qs(chartSpec_.id);
    o["rows"] = chartSpec_.rows;
    o["cols"] = chartSpec_.cols;
    o["reference_file"] = referencePath_;
    o["reference_column_major"] = dataOrder_->currentData().toBool();
    o["sample_percent"] = sampleSize_->value();
    o["auto_orientation"] = autoOrient_->isChecked();
    QJsonArray corners;
    for (const auto& c : view_->corners()) corners.append(QJsonArray{c.x(), c.y()});
    o["corners"] = corners;
    QJsonArray excl;
    for (int i = 0; i < int(excluded_.size()); ++i)
        if (excluded_[i]) excl.append(i);
    o["excluded"] = excl;
    o["mode"] = mode_->currentData().toInt();
    o["point_mapping"] = pointSettingsJson();
    o["model"] = model_->currentData().toInt();
    o["objective"] = objective_->currentData().toInt();
    o["separate_gain"] = separateGain_->isChecked();
    o["bound_coefficients"] = boundCoef_->isChecked();
    o["coef_min"] = coefMin_->value();
    o["coef_max"] = coefMax_->value();
    o["offset_mode"] = offsetMode_->currentData().toInt();
    o["neutral_weight"] = neutralWeight_->value();
    o["quantize"] = quantEnable_->isChecked();
    o["int_bits"] = intBits_->value();
    o["frac_bits"] = fracBits_->value();
    o["signed"] = quantSigned_->isChecked();
    o["preserve_row_sum"] = preserveSum_->isChecked();
    o["offset_bits"] = offsetBits_->value();
    return o;
}

void MainWindow::applySettingsJson(const QJsonObject& o, const QString& baseDir) {
    const QDir dir(baseDir);  // relative paths in a session are relative to the session file
    auto setCombo = [](QComboBox* c, const QVariant& v) {
        const int i = c->findData(v);
        if (i >= 0) c->setCurrentIndex(i);
    };
    // image first (it resets corners)
    const QString img = o["image"].toString().isEmpty() ? QString() : dir.absoluteFilePath(o["image"].toString());
    if (!img.isEmpty()) {
        LoadedImage li;
        std::string err;
        bool ok;
        if (o["image_is_raw"].toBool()) {
            const QJsonObject r = o["raw"].toObject();
            rawParams_.width = r["width"].toInt();
            rawParams_.height = r["height"].toInt();
            rawParams_.bytesPerSample = r["bytes_per_sample"].toInt(1);
            rawParams_.headerBytes = r["header_bytes"].toInt();
            rawParams_.layout = RawLayout(r["layout"].toInt());
            ok = loadRawFile(img.toStdString(), rawParams_, li, err);
            imageIsRaw_ = true;
        } else {
            ok = loadImageFile(img.toStdString(), li, err);
            imageIsRaw_ = false;
        }
        if (ok) setImage(li);
        else notify(tr("Load session"), qs(err));
    }
    dataBits_->setValue(o["data_bits"].toInt(8));
    setCombo(blackMode_, o["black_mode"].toInt());
    const QJsonArray b = o["black"].toArray();
    for (int i = 0; i < 3 && i < b.size(); ++i) black_[i]->setValue(b[i].toDouble());
    satPct_->setValue(o["clip_percent"].toDouble(98));
    customCct_->setValue(o["custom_cct"].toInt(3200));
    setCombo(illuminant_, o["illuminant"].toString());
    adaptWhite_->setChecked(o["adapt_to_output_white"].toBool(true));
    setCombo(outSpace_, o["output_space"].toString());

    setCombo(chart_, o["chart"].toString());
    if (chartSpec_.customGrid) {
        rows_->setValue(o["rows"].toInt(chartSpec_.rows));
        cols_->setValue(o["cols"].toInt(chartSpec_.cols));
    }
    setCombo(dataOrder_, o["reference_column_major"].toBool());
    const QString ref = o["reference_file"].toString().isEmpty() ? QString() : dir.absoluteFilePath(o["reference_file"].toString());
    if (!ref.isEmpty()) {
        std::string err;
        ChartSpec c = chartSpec_;
        if (loadReferenceFile(ref.toStdString(), dataOrder_->currentData().toBool(), c, err)) {
            chartSpec_ = c;
            referencePath_ = ref;
            onChartChanged();
        }
    }
    sampleSize_->setValue(o["sample_percent"].toDouble(50));
    autoOrient_->setChecked(o["auto_orientation"].toBool(true));
    excluded_.assign(chartSpec_.count(), false);
    for (const auto& e : o["excluded"].toArray())
        if (e.toInt() < int(excluded_.size())) excluded_[e.toInt()] = true;

    setCombo(model_, o["model"].toInt());
    setCombo(objective_, o["objective"].toInt());
    separateGain_->setChecked(o["separate_gain"].toBool(true));
    boundCoef_->setChecked(o["bound_coefficients"].toBool());
    coefMin_->setValue(o["coef_min"].toDouble(-16));
    coefMax_->setValue(o["coef_max"].toDouble(16));
    setCombo(offsetMode_, o["offset_mode"].toInt());
    neutralWeight_->setValue(o["neutral_weight"].toDouble(1));
    quantEnable_->setChecked(o["quantize"].toBool());
    intBits_->setValue(o["int_bits"].toInt(4));
    fracBits_->setValue(o["frac_bits"].toInt(8));
    quantSigned_->setChecked(o["signed"].toBool(true));
    preserveSum_->setChecked(o["preserve_row_sum"].toBool(true));
    offsetBits_->setValue(o["offset_bits"].toInt(12));

    applyPointSettingsJson(o["point_mapping"].toObject(), baseDir);
    mode_->setCurrentIndex(mode_->findData(o["mode"].toInt(0)));

    QVector<QPointF> corners;
    for (const auto& c : o["corners"].toArray()) {
        const QJsonArray p = c.toArray();
        corners << QPointF(p[0].toDouble(), p[1].toDouble());
    }
    view_->setCorners(corners);
    updateOverlay();
}

void MainWindow::saveSession() {
    const QString path = QFileDialog::getSaveFileName(this, tr("Save session"), "session.ccmproj",
                                                      tr("CCM session (*.ccmproj)"));
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        QMessageBox::warning(this, tr("Save session"), tr("Cannot write %1").arg(path));
        return;
    }
    f.write(QJsonDocument(settingsJson()).toJson());
    statusBar()->showMessage(tr("Saved %1").arg(path));
}

void MainWindow::loadSession() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Load session"), QString(), tr("CCM session (*.ccmproj)"));
    if (path.isEmpty()) return;
    QString err;
    if (!loadSessionFile(path, err)) QMessageBox::warning(this, tr("Load session"), err);
}

bool MainWindow::loadSessionFile(const QString& path, QString& error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        error = tr("Cannot open %1").arg(path);
        return false;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject()) {
        error = tr("Invalid session file.");
        return false;
    }
    lastError_.clear();
    applySettingsJson(doc.object(), QFileInfo(path).absolutePath());
    if (!lastError_.isEmpty()) {
        error = lastError_;
        return false;
    }
    return true;
}

bool MainWindow::computeNow(QString& error) {
    if (image_.rgb.empty() || (!pointMode() && !cornersReady())) {
        error = tr("Session has no image or no four corners.");
        return false;
    }
    lastError_.clear();
    compute();
    if (!haveResult_) error = lastError_.isEmpty() ? tr("Compute failed.") : lastError_;
    return haveResult_;
}

bool MainWindow::exportJsonTo(const QString& path, QString& error) {
    QFile f(path);
    if (!haveResult_ || !f.open(QIODevice::WriteOnly)) {
        error = haveResult_ ? tr("Cannot write %1").arg(path) : tr("No result to export.");
        return false;
    }
    f.write(resultJson());
    return true;
}
