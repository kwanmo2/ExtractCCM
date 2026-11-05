#pragma once
#include <QTabWidget>
#include <QWidget>

#include "core/PatchSampler.h"
#include "core/Report.h"

class QCheckBox;
class QPlainTextEdit;
class QTableWidget;
class QLabel;

// Draws the chart grid: each cell shows the target (top) and the CCM output (bottom).
class SwatchWidget : public QWidget {
    Q_OBJECT
public:
    explicit SwatchWidget(QWidget* parent = nullptr) : QWidget(parent) { setMinimumSize(300, 200); }
    void setData(int cols, const ccm::Evaluation& ev);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    int rows_ = 0, cols_ = 0;
    ccm::Evaluation ev_;
};

class ResultPanel : public QTabWidget {
    Q_OBJECT
public:
    explicit ResultPanel(QWidget* parent = nullptr);

    // names: one per patch/point; gridCols: columns of the swatch grid
    void showResult(const std::vector<std::string>& names, int gridCols, const std::vector<ccm::PatchStat>& stats,
                    const ccm::FitResult& fit, const ccm::Evaluation& ev, const QString& summary,
                    const QString& diagnostics);
    void showCorrected(const QImage& img);
    bool correctedLinear() const;  // corrected image without gamma (linear output)
    void clear();

signals:
    void patchIncludeToggled(int index, bool include);
    void correctedGammaToggled();

private:
    QPlainTextEdit* matrixText_;
    QTableWidget* table_;
    SwatchWidget* swatches_;
    QLabel* corrected_;
    QCheckBox* correctedLinear_;
    QPlainTextEdit* diagText_;
    bool updating_ = false;
};
