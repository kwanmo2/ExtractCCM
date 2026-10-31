#include "ui/ImageView.h"

#include <QGraphicsPixmapItem>
#include <QMouseEvent>
#include <QScrollBar>
#include <QWheelEvent>
#include <cmath>

namespace {
const char* kCornerLabels[4] = {"1", "2", "3", "4"};
constexpr double kHandleRadius = 7.0;  // screen pixels
}

ImageView::ImageView(QWidget* parent) : QGraphicsView(parent) {
    setScene(new QGraphicsScene(this));
    setBackgroundBrush(QColor(40, 40, 40));
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setRenderHint(QPainter::Antialiasing);
    setMouseTracking(true);
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
}

void ImageView::setImage(const QImage& preview, double previewScale) {
    // A new image (not just a display-gain refresh) goes back to fit-to-window
    if (!pixmap_ || pixmap_->pixmap().size() != preview.size()) userZoomed_ = false;
    scene()->clear();
    pixmap_ = scene()->addPixmap(QPixmap::fromImage(preview));
    pixmap_->setTransformationMode(Qt::SmoothTransformation);
    scale_ = previewScale;
    scene()->setSceneRect(pixmap_->boundingRect());
    viewport()->update();
}

void ImageView::setMarkers(const QVector<Marker>& markers) {
    markers_ = markers;
    viewport()->update();
}

void ImageView::fitToWindow() {
    if (pixmap_) fitInView(pixmap_, Qt::KeepAspectRatio);
}

void ImageView::setCorners(const QVector<QPointF>& corners) {
    corners_ = corners;
    viewport()->update();
}

void ImageView::clearCorners() {
    corners_.clear();
    quads_.clear();
    states_.clear();
    viewport()->update();
    emit cornersChanged();
}

void ImageView::setOverlay(const QVector<QPolygonF>& quads, const QVector<PatchState>& states) {
    quads_ = quads;
    states_ = states;
    viewport()->update();
}

QPointF ImageView::toImage(const QPoint& viewPos) const { return mapToScene(viewPos) / scale_; }

int ImageView::hitMarker(const QPoint& viewPos) const {
    for (int i = int(markers_.size()) - 1; i >= 0; --i) {
        const QPoint c = mapFromScene(toScene(markers_[i].pos));
        if (std::hypot(c.x() - viewPos.x(), c.y() - viewPos.y()) <= kHandleRadius + 3) return i;
    }
    return -1;
}

int ImageView::hitCorner(const QPoint& viewPos) const {
    for (int i = 0; i < corners_.size(); ++i) {
        const QPoint c = mapFromScene(toScene(corners_[i]));
        if (std::hypot(c.x() - viewPos.x(), c.y() - viewPos.y()) <= kHandleRadius + 3) return i;
    }
    return -1;
}

void ImageView::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::MiddleButton || e->button() == Qt::RightButton ||
        (e->button() == Qt::LeftButton && (e->modifiers() & Qt::ControlModifier))) {
        panning_ = true;
        panStart_ = e->pos();
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (e->button() != Qt::LeftButton || !pixmap_) return;
    if (mode_ == Mode::Points) {
        dragging_ = hitMarker(e->pos());
        if (dragging_ >= 0) emit markerPressed(dragging_);
        else emit pointClicked(toImage(e->pos()));
        return;
    }
    dragging_ = hitCorner(e->pos());
    if (dragging_ < 0 && corners_.size() < 4) {
        corners_.push_back(toImage(e->pos()));
        viewport()->update();
        if (corners_.size() == 4) emit cornersChanged();
    }
}

void ImageView::mouseMoveEvent(QMouseEvent* e) {
    if (panning_) {
        const QPoint d = e->pos() - panStart_;
        panStart_ = e->pos();
        horizontalScrollBar()->setValue(horizontalScrollBar()->value() - d.x());
        verticalScrollBar()->setValue(verticalScrollBar()->value() - d.y());
        return;
    }
    if (dragging_ >= 0 && mode_ == Mode::Points) {
        emit markerMoved(dragging_, toImage(e->pos()));
        return;
    }
    if (dragging_ >= 0) {
        corners_[dragging_] = toImage(e->pos());
        viewport()->update();
        if (corners_.size() == 4) emit cornersChanged();
        return;
    }
    const bool onHandle = mode_ == Mode::Points ? hitMarker(e->pos()) >= 0 : hitCorner(e->pos()) >= 0;
    setCursor(onHandle ? Qt::SizeAllCursor : Qt::CrossCursor);
}

void ImageView::mouseReleaseEvent(QMouseEvent*) {
    panning_ = false;
    dragging_ = -1;
    setCursor(Qt::CrossCursor);
}

void ImageView::wheelEvent(QWheelEvent* e) {
    const double f = std::pow(1.0015, e->angleDelta().y());
    scale(f, f);
    userZoomed_ = true;
}

void ImageView::resizeEvent(QResizeEvent* e) {
    QGraphicsView::resizeEvent(e);
    if (!userZoomed_) fitToWindow();
}

void ImageView::drawMarkers(QPainter* p, double px) {
    QFont f = p->font();
    f.setPointSizeF(std::max(9.0 * px, 0.1));
    f.setBold(true);
    for (const auto& m : markers_) {
        const QPointF c = toScene(m.pos);
        const double h = m.halfSize * scale_;  // sampling square in scene units
        const QColor col = m.selected ? QColor(255, 220, 0) : m.color;
        p->setBrush(Qt::NoBrush);
        p->setPen(QPen(col, (m.selected ? 2.5 : 1.5) * px, m.pending ? Qt::DashLine : Qt::SolidLine));
        p->drawRect(QRectF(c.x() - h, c.y() - h, 2 * h, 2 * h));
        p->setPen(QPen(Qt::black, 1.0 * px));
        p->setBrush(col);
        p->drawEllipse(c, kHandleRadius * px * 0.6, kHandleRadius * px * 0.6);
        p->setFont(f);
        const QPointF textPos = c + QPointF(std::max(h, kHandleRadius * px) + 3 * px, -3 * px);
        p->setPen(Qt::black);
        p->drawText(textPos + QPointF(px, px), m.label);
        p->setPen(col);
        p->drawText(textPos, m.label);
    }
}

void ImageView::drawForeground(QPainter* p, const QRectF&) {
    const double px = 1.0 / transform().m11();  // one screen pixel in scene units
    if (mode_ == Mode::Points) {
        drawMarkers(p, px);
        return;
    }

    for (int i = 0; i < quads_.size(); ++i) {
        QColor c = Qt::green;
        switch (i < states_.size() ? states_[i] : PatchState::Ok) {
            case PatchState::Excluded: c = QColor(150, 150, 150); break;
            case PatchState::Saturated: c = Qt::red; break;
            case PatchState::NoReference: c = QColor(255, 170, 0); break;
            default: break;
        }
        QPolygonF poly;
        for (const auto& q : quads_[i]) poly << toScene(q);
        p->setPen(QPen(c, 1.5 * px));
        p->setBrush(Qt::NoBrush);
        p->drawPolygon(poly);
        QFont f = p->font();
        f.setPointSizeF(std::max(8.0 * px, 0.1));
        p->setFont(f);
        p->drawText(poly.boundingRect().topLeft() + QPointF(2 * px, -2 * px), QString::number(i + 1));
    }

    if (corners_.size() >= 2) {
        p->setPen(QPen(QColor(0, 200, 255), 1.0 * px, Qt::DashLine));
        QPolygonF poly;
        for (const auto& c : corners_) poly << toScene(c);
        if (corners_.size() == 4) p->drawPolygon(poly);
        else p->drawPolyline(poly);
    }
    for (int i = 0; i < corners_.size(); ++i) {
        const QPointF c = toScene(corners_[i]);
        p->setPen(QPen(Qt::black, 1.0 * px));
        p->setBrush(QColor(0, 200, 255));
        p->drawEllipse(c, kHandleRadius * px, kHandleRadius * px);
        QFont f = p->font();
        f.setPointSizeF(std::max(9.0 * px, 0.1));
        f.setBold(true);
        p->setFont(f);
        p->setPen(Qt::black);
        p->drawText(QRectF(c.x() - kHandleRadius * px, c.y() - kHandleRadius * px, 2 * kHandleRadius * px,
                           2 * kHandleRadius * px),
                    Qt::AlignCenter, kCornerLabels[i]);
    }
}
