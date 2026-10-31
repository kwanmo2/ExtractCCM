#include "ui/RawImportDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QSpinBox>

RawImportDialog::RawImportDialog(const ccm::RawParams& p, QWidget* parent) : QDialog(parent) {
    setWindowTitle(tr("Open headerless RAW"));
    auto* form = new QFormLayout(this);

    width_ = new QSpinBox;
    width_->setRange(1, 65535);
    width_->setValue(p.width > 0 ? p.width : 1920);
    height_ = new QSpinBox;
    height_->setRange(1, 65535);
    height_->setValue(p.height > 0 ? p.height : 1080);
    bytes_ = new QComboBox;
    bytes_->addItem(tr("8-bit (1 byte/sample)"), 1);
    bytes_->addItem(tr("10-16 bit (2 bytes/sample, little endian)"), 2);
    bytes_->setCurrentIndex(p.bytesPerSample == 2 ? 1 : 0);
    header_ = new QSpinBox;
    header_->setRange(0, 1 << 30);
    header_->setValue(p.headerBytes);
    layout_ = new QComboBox;
    layout_->addItem("Bayer RGGB", int(ccm::RawLayout::BayerRGGB));
    layout_->addItem("Bayer BGGR", int(ccm::RawLayout::BayerBGGR));
    layout_->addItem("Bayer GRBG", int(ccm::RawLayout::BayerGRBG));
    layout_->addItem("Bayer GBRG", int(ccm::RawLayout::BayerGBRG));
    layout_->addItem(tr("Interleaved RGB"), int(ccm::RawLayout::RGB));
    layout_->addItem(tr("Interleaved BGR"), int(ccm::RawLayout::BGR));
    layout_->addItem(tr("Mono"), int(ccm::RawLayout::Mono));
    layout_->setCurrentIndex(layout_->findData(int(p.layout)));

    form->addRow(tr("Width"), width_);
    form->addRow(tr("Height"), height_);
    form->addRow(tr("Sample size"), bytes_);
    form->addRow(tr("Header bytes"), header_);
    form->addRow(tr("Layout"), layout_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    form->addRow(buttons);
}

ccm::RawParams RawImportDialog::params() const {
    ccm::RawParams p;
    p.width = width_->value();
    p.height = height_->value();
    p.bytesPerSample = bytes_->currentData().toInt();
    p.headerBytes = header_->value();
    p.layout = ccm::RawLayout(layout_->currentData().toInt());
    return p;
}
