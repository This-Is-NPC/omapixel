#include "PixelGridItem.h"

#include "Render.h"

#include <QPainter>

namespace omapixel {

PixelGridItem::PixelGridItem(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    // Nearest-neighbour, always. Smoothing a pixel sprite is the one thing that
    // cannot be allowed to happen anywhere in this program.
    setSmooth(false);
    setAntialiasing(false);
    connect(this, &PixelGridItem::specChanged, this, [this] { resize(); update(); });
}

void PixelGridItem::setModel(DocumentModel *model)
{
    if (m_model == model)
        return;
    if (m_model)
        m_model->disconnect(this);
    m_model = model;
    if (m_model) {
        connect(m_model, &DocumentModel::changed, this, [this] { resize(); update(); });
        connect(m_model, &DocumentModel::viewChanged, this, [this] { update(); });
    }
    emit modelChanged();
    resize();
    update();
}

void PixelGridItem::setClip(const QString &clip)
{
    if (m_clip == clip) return;
    m_clip = clip;
    emit specChanged();
}

void PixelGridItem::setFrame(int frame)
{
    if (m_frame == frame) return;
    m_frame = frame;
    emit specChanged();
}

void PixelGridItem::setCell(qreal cell)
{
    if (qFuzzyCompare(m_cell, cell)) return;
    m_cell = cell;
    emit specChanged();
}

void PixelGridItem::setChecker(bool checker)
{
    if (m_checker == checker) return;
    m_checker = checker;
    emit specChanged();
}

void PixelGridItem::setMesh(bool mesh)
{
    if (m_mesh == mesh) return;
    m_mesh = mesh;
    emit specChanged();
}

void PixelGridItem::setCheckerDark(const QColor &colour)
{
    if (m_checkerDark == colour) return;
    m_checkerDark = colour;
    emit specChanged();
}

void PixelGridItem::setCheckerLight(const QColor &colour)
{
    if (m_checkerLight == colour) return;
    m_checkerLight = colour;
    emit specChanged();
}

void PixelGridItem::setMeshColour(const QColor &colour)
{
    if (m_meshColour == colour) return;
    m_meshColour = colour;
    emit specChanged();
}

void PixelGridItem::resize()
{
    if (!m_model)
        return;
    setImplicitWidth(m_model->columns() * m_cell);
    setImplicitHeight(m_model->rows() * m_cell);
}

void PixelGridItem::paint(QPainter *painter)
{
    if (!m_model)
        return;

    // Rendered at one screen pixel per sprite pixel and then scaled up, rather
    // than rendered at the final size. It keeps the image small whatever the
    // zoom, and the scaling is a nearest-neighbour blit either way.
    render::Options options;
    options.scale = 1;
    options.checker = m_checker;
    options.checkerDark = m_checkerDark;
    options.checkerLight = m_checkerLight;
    const QImage image =
        render::toImage(m_model->document(), m_clip, m_frame, options);
    if (image.isNull())
        return;

    painter->setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter->drawImage(QRectF(0, 0, image.width() * m_cell, image.height() * m_cell),
                       image);

    if (m_mesh && m_cell >= 4) {
        // Fades out on its own once the pixels are too small to fit a line
        // between them, where it would be noise rather than a ruler.
        QPen pen(m_meshColour);
        pen.setWidth(1);
        painter->setPen(pen);
        for (int x = 0; x <= image.width(); ++x)
            painter->drawLine(QPointF(x * m_cell, 0), QPointF(x * m_cell, image.height() * m_cell));
        for (int y = 0; y <= image.height(); ++y)
            painter->drawLine(QPointF(0, y * m_cell), QPointF(image.width() * m_cell, y * m_cell));
    }
}

} // namespace omapixel
