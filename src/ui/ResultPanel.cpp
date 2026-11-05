#include "ui/ResultPanel.h"

#include <QCheckBox>
#include <QFontDatabase>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPlainTextEdit>
#include <QScrollArea>
#include <QTableWidget>
#include <QVBoxLayout>

#include "core/ColorMath.h"

namespace {

QColor toQColor(const cv::Vec3d& linear) {
    auto enc = [](double v) { return int(std::lround(255.0 * ccm::srgbEncode(v))); };
    return QColor(enc(linear[0]), enc(linear[1]), enc(linear[2]));
}

QTableWidgetItem* numItem(double v, int prec) {
    auto* it = new QTableWidgetItem(QString::number(v, 'f', prec));
    it->setData(Qt::UserRole, v);
    it->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    it->setFlags(it->flags() & ~Qt::ItemIsEditable);
    return it;
}

QColor deltaEColor(double de) {
    if (de < 3) return QColor(200, 240, 200);
    if (de < 6) return QColor(250, 240, 180);
    return QColor(250, 200, 200);
}

}  // namespace

void SwatchWidget::setData(int cols, const ccm::Evaluation& ev) {
    cols_ = std::max(cols, 1);
    rows_ = ev.patches.empty() ? 0 : int((ev.patches.size() + cols_ - 1) / cols_);
    ev_ = ev;
    update();
}

void SwatchWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(60, 60, 60));
    if (rows_ == 0 || cols_ == 0 || ev_.patches.empty()) return;
    const double cw = double(width()) / cols_, ch = double(height()) / rows_;
    QFont f = p.font();
    f.setPointSizeF(std::max(6.0, std::min(cw, ch) / 7.0));
    p.setFont(f);
    for (int i = 0; i < int(ev_.patches.size()); ++i) {
        const auto& r = ev_.patches[i];
        const QRectF cell((i % cols_) * cw + 2, (i / cols_) * ch + 2, cw - 4, ch - 4);
        QRectF top = cell, bottom = cell;
        top.setHeight(cell.height() / 2);
        bottom.setTop(cell.top() + cell.height() / 2);
        p.fillRect(top, toQColor(r.target));
        p.fillRect(bottom, toQColor(r.predicted));
        if (!r.included) {
            p.setPen(QPen(Qt::black, 2));
            p.drawLine(cell.topLeft(), cell.bottomRight());
        }
        const QString label = QString("%1  ΔE %2").arg(i + 1).arg(r.de00, 0, 'f', 1);
        p.setPen(Qt::black);
        p.drawText(bottom.adjusted(3, 1, 0, 0), Qt::AlignLeft | Qt::AlignBottom, label);
        p.setPen(Qt::white);
        p.drawText(bottom.adjusted(2, 0, 0, -1), Qt::AlignLeft | Qt::AlignBottom, label);
    }
}

ResultPanel::ResultPanel(QWidget* parent) : QTabWidget(parent) {
    setMinimumWidth(320);
    const QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);

    matrixText_ = new QPlainTextEdit;
    matrixText_->setReadOnly(true);
    matrixText_->setFont(mono);
    matrixText_->setLineWrapMode(QPlainTextEdit::NoWrap);
    addTab(matrixText_, tr("Matrix"));

    table_ = new QTableWidget;
    const QStringList headers = {tr("#"), tr("Name"), tr("Use"), tr("Cam R"), tr("Cam G"), tr("Cam B"),
                                 tr("Target"), tr("Output"), tr("ΔE00"), tr("ΔE76"), tr("ΔC"),
                                 tr("ΔE00 WB-only"), tr("Sat %"), tr("Pixels")};
    table_->setColumnCount(headers.size());
    table_->setHorizontalHeaderLabels(headers);
    table_->verticalHeader()->setVisible(false);
    table_->setSortingEnabled(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    connect(table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem* it) {
        if (updating_ || it->column() != 2) return;
        emit patchIncludeToggled(it->row(), it->checkState() == Qt::Checked);
    });
    addTab(table_, tr("Patches"));

    swatches_ = new SwatchWidget;
    addTab(swatches_, tr("Swatches (target / output)"));

    corrected_ = new QLabel;
    corrected_->setAlignment(Qt::AlignCenter);
    auto* scroll = new QScrollArea;
    scroll->setWidget(corrected_);
    scroll->setWidgetResizable(true);
    correctedLinear_ = new QCheckBox(tr("Without gamma (linear output)"));
    correctedLinear_->setChecked(true);
    correctedLinear_->setToolTip(tr("On: CCM output as-is, for pipelines that apply no gamma after the CCM.\n"
                                    "Off: sRGB gamma applied, as the image looks after CCM + gamma.\n"
                                    "'Save corrected image' uses the same setting."));
    connect(correctedLinear_, &QCheckBox::toggled, this, &ResultPanel::correctedGammaToggled);
    auto* correctedPage = new QWidget;
    auto* correctedLayout = new QVBoxLayout(correctedPage);
    correctedLayout->setContentsMargins(0, 0, 0, 0);
    correctedLayout->addWidget(correctedLinear_);
    correctedLayout->addWidget(scroll, 1);
    addTab(correctedPage, tr("Corrected image"));

    diagText_ = new QPlainTextEdit;
    diagText_->setReadOnly(true);
    diagText_->setFont(mono);
    addTab(diagText_, tr("Diagnostics"));
}

void ResultPanel::clear() {
    matrixText_->clear();
    table_->setRowCount(0);
    swatches_->setData(1, ccm::Evaluation{});
    corrected_->clear();
    diagText_->clear();
}

void ResultPanel::showCorrected(const QImage& img) { corrected_->setPixmap(QPixmap::fromImage(img)); }

bool ResultPanel::correctedLinear() const { return correctedLinear_->isChecked(); }

void ResultPanel::showResult(const std::vector<std::string>& names, int gridCols, const std::vector<ccm::PatchStat>& stats,
                             const ccm::FitResult&, const ccm::Evaluation& ev, const QString& summary,
                             const QString& diagnostics) {
    matrixText_->setPlainText(summary);
    diagText_->setPlainText(diagnostics);
    swatches_->setData(gridCols, ev);

    updating_ = true;
    table_->setRowCount(int(ev.patches.size()));
    for (int i = 0; i < int(ev.patches.size()); ++i) {
        const auto& r = ev.patches[i];
        table_->setItem(i, 0, numItem(i + 1, 0));
        auto* name = new QTableWidgetItem(QString::fromStdString(i < int(names.size()) ? names[i] : std::to_string(i + 1)));
        name->setFlags(name->flags() & ~Qt::ItemIsEditable);
        table_->setItem(i, 1, name);
        auto* use = new QTableWidgetItem;
        use->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        use->setCheckState(r.included ? Qt::Checked : Qt::Unchecked);
        table_->setItem(i, 2, use);
        for (int k = 0; k < 3; ++k) table_->setItem(i, 3 + k, numItem(r.cam[k], 4));
        auto* tgt = new QTableWidgetItem;
        tgt->setBackground(toQColor(r.target));
        tgt->setFlags(Qt::ItemIsEnabled);
        table_->setItem(i, 6, tgt);
        auto* out = new QTableWidgetItem;
        out->setBackground(toQColor(r.predicted));
        out->setFlags(Qt::ItemIsEnabled);
        table_->setItem(i, 7, out);
        auto* de = numItem(r.de00, 2);
        de->setBackground(deltaEColor(r.de00));
        table_->setItem(i, 8, de);
        table_->setItem(i, 9, numItem(r.de76, 2));
        table_->setItem(i, 10, numItem(r.dc, 2));
        table_->setItem(i, 11, numItem(r.de00Baseline, 2));
        const double sat = i < int(stats.size()) ? stats[i].saturatedRatio * 100.0 : 0.0;
        auto* satItem = numItem(sat, 1);
        if (sat > 1.0) satItem->setBackground(QColor(250, 200, 200));
        table_->setItem(i, 12, satItem);
        table_->setItem(i, 13, numItem(i < int(stats.size()) ? stats[i].pixelCount : 0, 0));
    }
    table_->resizeColumnsToContents();
    updating_ = false;
}
