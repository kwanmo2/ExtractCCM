// Point-mapping mode of MainWindow: calibrate without a chart by clicking the same surface
// points in a reference image (e.g. a phone photo) and in the camera image.
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QShortcut>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStandardItemModel>
#include <QStatusBar>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <cmath>
#include <opencv2/imgproc.hpp>

#include "core/ColorMath.h"
#include "ui/ImageView.h"
#include "ui/MainWindow.h"

using namespace ccm;

namespace {

constexpr int kPreviewMaxDim = 3000;
constexpr int kBlackFromChartIndex = 1;  // position of "Estimate from chart" in the black-level combo
constexpr double kHighSigma = 0.10;      // relative σ above which a sample is flagged (texture/edge)

enum Column { ColIndex, ColName, ColGray, ColUse, ColRef, ColCam, ColRefSigma, ColCamSigma, ColClip, ColCount };

QString qs(const std::string& s) { return QString::fromStdString(s); }

QColor linearToColor(const cv::Vec3d& v) {
    auto enc = [](double x) { return int(std::lround(255.0 * srgbEncode(x))); };
    return QColor(enc(v[0]), enc(v[1]), enc(v[2]));
}

double relativeSigma(const cv::Vec3d& sd, const cv::Vec3d& mean) {
    const double m = (mean[0] + mean[1] + mean[2]) / 3.0;
    return m > 1e-6 ? (sd[0] + sd[1] + sd[2]) / 3.0 / m : 0.0;
}

QPointF toQ(const cv::Point2f& p) { return QPointF(p.x, p.y); }
cv::Point2f toCv(const QPointF& p) { return cv::Point2f(float(p.x()), float(p.y())); }

}  // namespace

bool MainWindow::pointMode() const { return mode_ && mode_->currentData().toInt() == 1; }

void MainWindow::setupPointViews() {
    refView_ = new ImageView;
    camView_ = new ImageView;
    refView_->setMode(ImageView::Mode::Points);
    camView_->setMode(ImageView::Mode::Points);
    for (int side = 0; side < 2; ++side) {
        ImageView* v = side == 0 ? refView_ : camView_;
        connect(v, &ImageView::pointClicked, this, [this, side](QPointF p) { onPointClicked(side, p); });
        connect(v, &ImageView::markerPressed, this, [this](int i) {
            if (i < int(pairs_.size())) selectPair(i);
        });
        connect(v, &ImageView::markerMoved, this, [this, side](int i, QPointF p) { onMarkerMoved(side, i, p); });
    }
    auto* esc = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    connect(esc, &QShortcut::activated, this, &MainWindow::cancelPendingPoint);
    auto* del = new QShortcut(QKeySequence::Delete, this);
    connect(del, &QShortcut::activated, this, [this] {
        if (pointMode()) deleteSelectedPair();
    });
}

QGroupBox* MainWindow::buildPointGroup() {
    auto* g = new QGroupBox(tr("4. Reference image / point pairs"));
    auto* f = new QFormLayout(g);

    auto* openBtn = new QPushButton(tr("Open reference image…"));
    f->addRow(openBtn);
    refInfo_ = new QLabel(tr("No reference image"));
    refInfo_->setWordWrap(true);
    f->addRow(refInfo_);
    refEncoding_ = new QComboBox;
    refEncoding_->addItem(tr("sRGB (phone JPEG/PNG)"), int(RefEncoding::SRGB));
    refEncoding_->addItem(tr("Gamma 2.2"), int(RefEncoding::Gamma22));
    refEncoding_->addItem(tr("Linear"), int(RefEncoding::Linear));
    f->addRow(tr("Reference encoding"), refEncoding_);

    auto* sizeRow = new QHBoxLayout;
    refHalf_ = new QSpinBox;
    refHalf_->setRange(1, 500);
    refHalf_->setValue(4);
    refHalf_->setPrefix(tr("ref ±"));
    refHalf_->setSuffix(" px");
    camHalf_ = new QSpinBox;
    camHalf_->setRange(1, 2000);
    camHalf_->setValue(8);
    camHalf_->setPrefix(tr("cam ±"));
    camHalf_->setSuffix(" px");
    sizeRow->addWidget(refHalf_);
    sizeRow->addWidget(camHalf_);
    f->addRow(tr("Sample area"), sizeRow);

    neutralizeRef_ = new QCheckBox(tr("Neutralize the reference with gray points (recommended)"));
    neutralizeRef_->setChecked(true);
    neutralizeRef_->setToolTip(tr("Removes the color cast of the reference image: points marked as gray\n"
                                  "become neutral in the target."));
    f->addRow(neutralizeRef_);
    matchBrightness_ = new QCheckBox(tr("Match brightness too"));
    matchBrightness_->setToolTip(tr("Off (recommended): only the reference color (hue/saturation) is used;\n"
                                    "brightness follows the camera. Reflections and exposure differences\n"
                                    "in the reference image make its brightness unreliable."));
    f->addRow(matchBrightness_);

    auto* help = new QLabel(tr("Click a point in one image, then the same surface point in the other image.\n"
                               "Esc cancels, Delete removes the selected pair, drag a marker to move it.\n"
                               "Mark white/gray surfaces as 'Gray'. Use flat, evenly lit areas."));
    help->setWordWrap(true);
    f->addRow(help);

    pointTable_ = new QTableWidget(0, ColCount);
    pointTable_->setHorizontalHeaderLabels({tr("#"), tr("Name"), tr("Gray"), tr("Use"), tr("Ref"), tr("Cam"),
                                            tr("Ref σ %"), tr("Cam σ %"), tr("Clip %")});
    pointTable_->verticalHeader()->setVisible(false);
    pointTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
    pointTable_->setSelectionMode(QAbstractItemView::SingleSelection);
    pointTable_->setMinimumHeight(220);
    f->addRow(pointTable_);

    auto* btnRow = new QHBoxLayout;
    auto* delBtn = new QPushButton(tr("Delete pair"));
    auto* clearBtn = new QPushButton(tr("Clear all"));
    btnRow->addWidget(delBtn);
    btnRow->addWidget(clearBtn);
    f->addRow(btnRow);

    connect(openBtn, &QPushButton::clicked, this, &MainWindow::openReferenceImage);
    connect(refEncoding_, &QComboBox::currentIndexChanged, this, [this] {
        samplePoints();
        updatePointTable();
        invalidateResult();
    });
    for (QSpinBox* s : {refHalf_, camHalf_})
        connect(s, &QSpinBox::valueChanged, this, [this] {
            samplePoints();
            updatePointViews();
            updatePointTable();
            invalidateResult();
        });
    for (QCheckBox* c : {neutralizeRef_, matchBrightness_}) connect(c, &QCheckBox::toggled, this, &MainWindow::invalidateResult);
    connect(delBtn, &QPushButton::clicked, this, &MainWindow::deleteSelectedPair);
    connect(clearBtn, &QPushButton::clicked, this, &MainWindow::clearPairs);
    connect(pointTable_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
        if (updatingTable_ || it->row() >= int(pairs_.size())) return;
        PointPair& p = pairs_[it->row()];
        switch (it->column()) {
            case ColName: p.name = it->text().toStdString(); break;
            case ColGray: p.neutral = it->checkState() == Qt::Checked; break;
            case ColUse: p.enabled = it->checkState() == Qt::Checked; break;
            default: return;
        }
        invalidateResult();
        updatePointViews();
    });
    connect(pointTable_, &QTableWidget::currentCellChanged, this, [this](int row) {
        if (!updatingTable_ && row >= 0) selectPair(row);
    });
    return g;
}

void MainWindow::setMode(int mode) {
    const bool points = mode == 1;
    centerStack_->setCurrentIndex(points ? 1 : 0);
    chartGroup_->setVisible(!points);
    pointGroup_->setVisible(points);
    // the illuminant only matters for chart reference values
    for (QWidget* w : std::initializer_list<QWidget*>{illuminant_, customCct_, adaptWhite_}) w->setEnabled(!points);
    if (!points) onIlluminantChanged();
    // estimating black from the reference needs reliable reference brightness: chart mode only
    if (auto* model = qobject_cast<QStandardItemModel*>(blackMode_->model()))
        model->item(kBlackFromChartIndex)->setEnabled(!points);
    if (points && blackMode_->currentIndex() == kBlackFromChartIndex) blackMode_->setCurrentIndex(0);
    invalidateResult();
    updateOverlay();
    statusBar()->showMessage(points ? tr("Point mapping: open the reference image (left) and the camera image (right), "
                                         "then click matching points.")
                                    : tr("Open a chart image (camera CCM and gamma OFF), then click the centers of "
                                         "patches 1 → 2 → 3 → 4 as shown in the chart panel."));
    QTimer::singleShot(0, this, [this] {
        refView_->fitToWindow();
        camView_->fitToWindow();
    });
}

// ---------------------------------------------------------------- reference image

void MainWindow::openReferenceImage() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Open reference image"), QString(),
                                                      tr("Images (*.bmp *.png *.tif *.tiff *.jpg *.jpeg *.pgm *.ppm);;All files (*)"));
    if (path.isEmpty()) return;
    LoadedImage img;
    std::string err;
    if (!loadImageFile(path.toStdString(), img, err)) {
        QMessageBox::warning(this, tr("Open image"), qs(err));
        return;
    }
    setReferenceImage(img);
    refHalf_->setValue(std::max(2, img.rgb.cols / 250));
}

void MainWindow::setReferenceImage(const LoadedImage& img) {
    refImage_ = img;
    refPreview_ = downscale(img.rgb, kPreviewMaxDim);
    refPreviewScale_ = double(refPreview_.cols) / img.rgb.cols;
    refInfo_->setText(tr("%1\n%2 x %3").arg(QFileInfo(qs(img.path)).fileName()).arg(img.rgb.cols).arg(img.rgb.rows));
    updateReferencePreview();
    samplePoints();
    updatePointViews();
    updatePointTable();
    invalidateResult();
    QTimer::singleShot(0, refView_, &ImageView::fitToWindow);
}

void MainWindow::updateReferencePreview() {
    if (refPreview_.empty()) return;
    cv::Mat u8;
    if (refPreview_.depth() == CV_8U) u8 = refPreview_;
    else refPreview_.convertTo(u8, CV_8U, 1.0 / 257.0);
    QImage qi(u8.data, u8.cols, u8.rows, int(u8.step), QImage::Format_RGB888);
    refView_->setImage(qi.copy(), refPreviewScale_);
}

RefEncoding MainWindow::refEncoding() const { return RefEncoding(refEncoding_->currentData().toInt()); }

// ---------------------------------------------------------------- clicking

void MainWindow::onPointClicked(int side, QPointF pos) {
    const bool haveImage = side == 0 ? !refImage_.rgb.empty() : !image_.rgb.empty();
    if (!haveImage) return;
    if (pendingSide_ < 0 || pendingSide_ == side) {
        // start a new pair (or move the pending point within the same image)
        pendingSide_ = side;
        pendingPos_ = pos;
        statusBar()->showMessage(side == 0 ? tr("Now click the same point in the camera image (Esc cancels).")
                                           : tr("Now click the same point in the reference image (Esc cancels)."));
    } else {
        PointPair p;
        p.name = "P" + std::to_string(pairs_.size() + 1);
        p.ref = toCv(side == 0 ? pos : pendingPos_);
        p.cam = toCv(side == 1 ? pos : pendingPos_);
        pairs_.push_back(p);
        pendingSide_ = -1;
        selectedPair_ = int(pairs_.size()) - 1;
        samplePoints();
        updatePointTable();
        invalidateResult();
        statusBar()->showMessage(tr("Pair %1 added. Mark it as 'Gray' in the table if it is a white/gray surface.")
                                     .arg(pairs_.size()));
    }
    updatePointViews();
}

void MainWindow::onMarkerMoved(int side, int index, QPointF pos) {
    if (index < int(pairs_.size())) {
        (side == 0 ? pairs_[index].ref : pairs_[index].cam) = toCv(pos);
        samplePoints();
        updatePointTable();
        invalidateResult();
    } else if (pendingSide_ == side) {
        pendingPos_ = pos;
    }
    updatePointViews();
}

void MainWindow::selectPair(int index) {
    selectedPair_ = index;
    updatingTable_ = true;
    pointTable_->selectRow(index);
    updatingTable_ = false;
    updatePointViews();
}

void MainWindow::deleteSelectedPair() {
    if (selectedPair_ < 0 || selectedPair_ >= int(pairs_.size())) return;
    pairs_.erase(pairs_.begin() + selectedPair_);
    selectedPair_ = std::min(selectedPair_, int(pairs_.size()) - 1);
    samplePoints();
    updatePointTable();
    updatePointViews();
    invalidateResult();
}

void MainWindow::cancelPendingPoint() {
    if (pendingSide_ < 0) return;
    pendingSide_ = -1;
    updatePointViews();
    statusBar()->clearMessage();
}

void MainWindow::clearPairs() {
    if (pairs_.empty()) return;
    if (!batch_ && QMessageBox::question(this, tr("Clear all"), tr("Remove all %1 point pairs?").arg(pairs_.size())) !=
                       QMessageBox::Yes)
        return;
    pairs_.clear();
    pendingSide_ = -1;
    selectedPair_ = -1;
    samplePoints();
    updatePointTable();
    updatePointViews();
    invalidateResult();
}

// ---------------------------------------------------------------- sampling / display

void MainWindow::samplePoints() {
    stats_.assign(pairs_.size(), PatchStat{});
    refSamples_.assign(pairs_.size(), ReferenceSample{});
    const SampleSettings s = sampleSettings();
    for (size_t i = 0; i < pairs_.size(); ++i) {
        if (!image_.rgb.empty()) stats_[i] = sampleQuad(image_.rgb, squareAround(pairs_[i].cam, camHalf_->value()), s);
        if (!refImage_.rgb.empty())
            refSamples_[i] = sampleReference(refImage_.rgb, squareAround(pairs_[i].ref, refHalf_->value()), refEncoding());
    }
}

void MainWindow::updatePointViews() {
    QVector<ImageView::Marker> ref, cam;
    for (size_t i = 0; i < pairs_.size(); ++i) {
        const auto& p = pairs_[i];
        ImageView::Marker m;
        m.label = QString::number(i + 1) + (p.neutral ? tr(" G") : QString());  // names are in the table
        m.color = !p.enabled ? QColor(150, 150, 150) : p.neutral ? QColor(0, 220, 255) : QColor(0, 230, 0);
        const bool clipped = (i < stats_.size() && stats_[i].saturatedRatio > 0.01) ||
                             (i < refSamples_.size() && refSamples_[i].saturatedRatio > 0.01);
        if (p.enabled && clipped) m.color = Qt::red;
        m.selected = int(i) == selectedPair_;
        m.pos = toQ(p.ref);
        m.halfSize = refHalf_->value();
        ref << m;
        m.pos = toQ(p.cam);
        m.halfSize = camHalf_->value();
        cam << m;
    }
    if (pendingSide_ >= 0) {
        ImageView::Marker m;
        m.pos = pendingPos_;
        m.label = "?";
        m.pending = true;
        m.color = QColor(255, 140, 0);
        m.halfSize = pendingSide_ == 0 ? refHalf_->value() : camHalf_->value();
        (pendingSide_ == 0 ? ref : cam) << m;
    }
    refView_->setMarkers(ref);
    camView_->setMarkers(cam);
}

void MainWindow::updatePointTable() {
    updatingTable_ = true;
    pointTable_->setRowCount(int(pairs_.size()));
    auto readOnly = [](const QString& text) {
        auto* it = new QTableWidgetItem(text);
        it->setFlags(it->flags() & ~Qt::ItemIsEditable);
        return it;
    };
    auto check = [](bool on) {
        auto* it = new QTableWidgetItem;
        it->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        it->setCheckState(on ? Qt::Checked : Qt::Unchecked);
        return it;
    };
    const double displayGain = displayGain_->value();
    for (int i = 0; i < int(pairs_.size()); ++i) {
        const auto& p = pairs_[i];
        pointTable_->setItem(i, ColIndex, readOnly(QString::number(i + 1)));
        pointTable_->setItem(i, ColName, new QTableWidgetItem(qs(p.name)));
        pointTable_->setItem(i, ColGray, check(p.neutral));
        pointTable_->setItem(i, ColUse, check(p.enabled));
        auto* refItem = readOnly(QString());
        auto* camItem = readOnly(QString());
        if (i < int(refSamples_.size()) && refSamples_[i].pixelCount > 0) refItem->setBackground(linearToColor(refSamples_[i].linear));
        if (i < int(stats_.size()) && stats_[i].pixelCount > 0)
            camItem->setBackground(linearToColor(stats_[i].mean * displayGain));
        pointTable_->setItem(i, ColRef, refItem);
        pointTable_->setItem(i, ColCam, camItem);

        const double rs = i < int(refSamples_.size()) ? relativeSigma(refSamples_[i].stdDev, refSamples_[i].linear) : 0;
        const double cs = i < int(stats_.size()) ? relativeSigma(stats_[i].stdDev, stats_[i].mean) : 0;
        auto* rsItem = readOnly(QString::number(rs * 100, 'f', 1));
        auto* csItem = readOnly(QString::number(cs * 100, 'f', 1));
        if (rs > kHighSigma) rsItem->setBackground(QColor(250, 220, 160));
        if (cs > kHighSigma) csItem->setBackground(QColor(250, 220, 160));
        pointTable_->setItem(i, ColRefSigma, rsItem);
        pointTable_->setItem(i, ColCamSigma, csItem);
        const double clip = std::max(i < int(stats_.size()) ? stats_[i].saturatedRatio : 0.0,
                                     i < int(refSamples_.size()) ? refSamples_[i].saturatedRatio : 0.0);
        auto* clipItem = readOnly(QString::number(clip * 100, 'f', 1));
        if (clip > 0.01) clipItem->setBackground(QColor(250, 200, 200));
        pointTable_->setItem(i, ColClip, clipItem);
    }
    pointTable_->resizeColumnsToContents();
    if (selectedPair_ >= 0 && selectedPair_ < int(pairs_.size())) pointTable_->selectRow(selectedPair_);
    updatingTable_ = false;
}

// ---------------------------------------------------------------- compute input / reports

bool MainWindow::preparePointData() {
    if (refImage_.rgb.empty()) {
        notify(tr("Compute"), tr("Open a reference image first."));
        return false;
    }
    int enabled = 0;
    for (const auto& p : pairs_) enabled += p.enabled;
    if (enabled < 4) {
        notify(tr("Compute"), tr("Need at least 4 point pairs (8 or more recommended)."));
        return false;
    }
    samplePoints();
    updatePointTable();
    updatePointViews();

    std::vector<cv::Vec3d> refLinear, cam;
    for (size_t i = 0; i < pairs_.size(); ++i) {
        refLinear.push_back(refSamples_[i].linear);
        cam.push_back(stats_[i].mean);
    }
    PointTargetOptions o;
    o.outputSpace = outSpace_->currentData().toString().toStdString();
    o.neutralizeReference = neutralizeRef_->isChecked();
    o.matchBrightness = matchBrightness_->isChecked();
    const PointTargets pt = buildPointTargets(refLinear, cam, neutralMask(), includedMask(), o);
    if (!pt.ok) {
        notify(tr("Compute"), QCoreApplication::translate("ccm", pt.message.c_str()));
        return false;
    }
    targets_ = pt.targets;
    referenceGains_ = pt.referenceGains;
    linearity_ = LinearityResult{};
    names_.clear();
    for (const auto& p : pairs_) names_.push_back(p.name);
    gridCols_ = 6;
    return true;
}

QString MainWindow::pointDiagnostics() const {
    const std::vector<bool> inc = includedMask();
    int used = 0, gray = 0;
    QStringList noisy, clipped;
    for (size_t i = 0; i < pairs_.size(); ++i) {
        if (!pairs_[i].enabled) continue;
        if (inc[i]) {
            ++used;
            gray += pairs_[i].neutral;
        } else {
            clipped << qs(pairs_[i].name);
        }
        if (relativeSigma(refSamples_[i].stdDev, refSamples_[i].linear) > kHighSigma ||
            relativeSigma(stats_[i].stdDev, stats_[i].mean) > kHighSigma)
            noisy << qs(pairs_[i].name);
    }
    QString d = tr("== Point mapping ==\n");
    d += tr("Points used: %1 (gray: %2)\n").arg(used).arg(gray);
    if (neutralizeRef_->isChecked())
        d += tr("Reference neutralization gains R/G/B: %1 / %2 / %3\n")
                 .arg(referenceGains_[0], 0, 'f', 3)
                 .arg(referenceGains_[1], 0, 'f', 3)
                 .arg(referenceGains_[2], 0, 'f', 3);
    if (gray == 0)
        d += tr("WARNING: no gray point - white balance is taken from all points (gray world) and may be off.\n");
    if (used < 8)
        d += tr("WARNING: only %1 points - the CCM is poorly constrained. Use 8+ points with different colors.\n")
                 .arg(used);
    if (!clipped.isEmpty()) d += tr("Excluded (clipped in either image): %1\n").arg(clipped.join(", "));
    if (!noisy.isEmpty())
        d += tr("High variation inside the sample area (texture/edge?): %1\n").arg(noisy.join(", "));
    return d + "\n";
}

QJsonObject MainWindow::pointSettingsJson() const {
    QJsonObject o;
    o["reference_image"] = qs(refImage_.path);
    o["reference_encoding"] = refEncoding_->currentData().toInt();
    o["ref_half_size"] = refHalf_->value();
    o["cam_half_size"] = camHalf_->value();
    o["neutralize_reference"] = neutralizeRef_->isChecked();
    o["match_brightness"] = matchBrightness_->isChecked();
    QJsonArray pairs;
    for (const auto& p : pairs_) {
        QJsonObject j;
        j["name"] = qs(p.name);
        j["ref"] = QJsonArray{p.ref.x, p.ref.y};
        j["cam"] = QJsonArray{p.cam.x, p.cam.y};
        j["neutral"] = p.neutral;
        j["enabled"] = p.enabled;
        pairs.append(j);
    }
    o["pairs"] = pairs;
    return o;
}

void MainWindow::applyPointSettingsJson(const QJsonObject& o, const QString& baseDir) {
    if (o.isEmpty()) return;
    const QString ref = o["reference_image"].toString();
    if (!ref.isEmpty()) {
        LoadedImage img;
        std::string err;
        if (loadImageFile(QDir(baseDir).absoluteFilePath(ref).toStdString(), img, err)) setReferenceImage(img);
        else notify(tr("Load session"), qs(err));
    }
    refEncoding_->setCurrentIndex(refEncoding_->findData(o["reference_encoding"].toInt(int(RefEncoding::SRGB))));
    refHalf_->setValue(o["ref_half_size"].toInt(4));
    camHalf_->setValue(o["cam_half_size"].toInt(8));
    neutralizeRef_->setChecked(o["neutralize_reference"].toBool(true));
    matchBrightness_->setChecked(o["match_brightness"].toBool(false));
    pairs_.clear();
    for (const auto& v : o["pairs"].toArray()) {
        const QJsonObject j = v.toObject();
        PointPair p;
        p.name = j["name"].toString().toStdString();
        const QJsonArray r = j["ref"].toArray(), c = j["cam"].toArray();
        p.ref = cv::Point2f(float(r[0].toDouble()), float(r[1].toDouble()));
        p.cam = cv::Point2f(float(c[0].toDouble()), float(c[1].toDouble()));
        p.neutral = j["neutral"].toBool();
        p.enabled = j["enabled"].toBool(true);
        pairs_.push_back(p);
    }
    pendingSide_ = -1;
    selectedPair_ = -1;
    samplePoints();
    updatePointTable();
    updatePointViews();
}
