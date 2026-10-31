#pragma once
#include <QDialog>

#include "core/ImageIO.h"

class QSpinBox;
class QComboBox;

class RawImportDialog : public QDialog {
    Q_OBJECT
public:
    explicit RawImportDialog(const ccm::RawParams& initial, QWidget* parent = nullptr);
    ccm::RawParams params() const;

private:
    QSpinBox* width_;
    QSpinBox* height_;
    QComboBox* bytes_;
    QSpinBox* header_;
    QComboBox* layout_;
};
