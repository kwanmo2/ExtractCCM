#pragma once
#include <QGraphicsView>
#include <QPolygonF>
#include <QVector>

class QGraphicsPixmapItem;

// Image viewer with zoom/pan and four draggable chart-corner handles.
// All public coordinates are full-resolution image pixels.
class ImageView : public QGraphicsView {
    Q_OBJECT
public:
    enum class PatchState { Ok, Excluded, Saturated, NoReference };

    explicit ImageView(QWidget* parent = nullptr);

    void setImage(const QImage& preview, double previewScale);
    void setCorners(const QVector<QPointF>& corners);
    QVector<QPointF> corners() const { return corners_; }
    void clearCorners();
    void setOverlay(const QVector<QPolygonF>& quads, const QVector<PatchState>& states);
    void fitToWindow();

signals:
    void cornersChanged();

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;
    void drawForeground(QPainter* p, const QRectF& rect) override;

private:
    QPointF toImage(const QPoint& viewPos) const;
    QPointF toScene(const QPointF& imagePos) const { return imagePos * scale_; }
    int hitCorner(const QPoint& viewPos) const;

    QGraphicsPixmapItem* pixmap_ = nullptr;
    double scale_ = 1.0;  // scene px per full-res px
    QVector<QPointF> corners_;
    QVector<QPolygonF> quads_;
    QVector<PatchState> states_;
    int dragging_ = -1;
    bool panning_ = false;
    bool userZoomed_ = false;  // keep fitting to the window until the user zooms
    QPoint panStart_;
};
