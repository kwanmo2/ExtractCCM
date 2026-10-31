#pragma once
#include <QGraphicsView>
#include <QPolygonF>
#include <QVector>

class QGraphicsPixmapItem;

// Image viewer with zoom/pan. Corners mode: four draggable chart-corner handles.
// Points mode: numbered markers with their sampling squares (point-mapping calibration).
// All public coordinates are full-resolution image pixels.
class ImageView : public QGraphicsView {
    Q_OBJECT
public:
    enum class PatchState { Ok, Excluded, Saturated, NoReference };
    enum class Mode { Corners, Points };

    struct Marker {
        QPointF pos;
        QString label;
        double halfSize = 4;  // sampling square half size, image pixels
        QColor color = Qt::green;
        bool pending = false;   // waiting for its partner in the other image
        bool selected = false;
    };

    explicit ImageView(QWidget* parent = nullptr);

    void setMode(Mode m) { mode_ = m; }
    void setImage(const QImage& preview, double previewScale);
    void setMarkers(const QVector<Marker>& markers);
    void setCorners(const QVector<QPointF>& corners);
    QVector<QPointF> corners() const { return corners_; }
    void clearCorners();
    void setOverlay(const QVector<QPolygonF>& quads, const QVector<PatchState>& states);
    void fitToWindow();

signals:
    void cornersChanged();
    void pointClicked(QPointF imagePos);           // Points mode, click on empty image area
    void markerPressed(int index);                 // Points mode, click on a marker
    void markerMoved(int index, QPointF imagePos);  // Points mode, marker dragged

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
    int hitMarker(const QPoint& viewPos) const;
    void drawMarkers(QPainter* p, double px);

    Mode mode_ = Mode::Corners;
    QGraphicsPixmapItem* pixmap_ = nullptr;
    QVector<Marker> markers_;
    double scale_ = 1.0;  // scene px per full-res px
    QVector<QPointF> corners_;
    QVector<QPolygonF> quads_;
    QVector<PatchState> states_;
    int dragging_ = -1;
    bool panning_ = false;
    bool userZoomed_ = false;  // keep fitting to the window until the user zooms
    QPoint panStart_;
};
