#include "PixelGridItem.h"

#include "Render.h"

#include <QQuickWindow>
#include <QSGGeometryNode>
#include <QSGSimpleTextureNode>
#include <QSGTexture>
#include <QSGVertexColorMaterial>

namespace omapixel {

namespace {

class PixelGridNode : public QSGNode
{
public:
    PixelGridNode()
    {
        image = new QSGSimpleTextureNode;
        image->setFiltering(QSGTexture::Nearest);
        appendChildNode(image);

        mesh = new QSGGeometryNode;
        auto *geometry = new QSGGeometry(
            QSGGeometry::defaultAttributes_ColoredPoint2D(), 0);
        geometry->setDrawingMode(QSGGeometry::DrawLines);
        geometry->setLineWidth(1);
        geometry->setVertexDataPattern(QSGGeometry::DynamicPattern);
        mesh->setGeometry(geometry);
        mesh->setFlag(QSGNode::OwnsGeometry);
        mesh->setMaterial(new QSGVertexColorMaterial);
        mesh->setFlag(QSGNode::OwnsMaterial);
        appendChildNode(mesh);
    }

    ~PixelGridNode() override
    {
        delete texture;
    }

    void replaceTexture(QSGTexture *next)
    {
        QSGTexture *previous = texture;
        texture = next;
        image->setTexture(texture);
        delete previous;
    }

    QSGSimpleTextureNode *image = nullptr;
    QSGGeometryNode *mesh = nullptr;
    QSGTexture *texture = nullptr;
};

} // namespace

PixelGridItem::PixelGridItem(QQuickItem *parent) : QQuickItem(parent)
{
    setFlag(ItemHasContents);
    connect(this, &PixelGridItem::specChanged, this, [this] { resize(); update(); });
}

void PixelGridItem::setModel(DocumentModel *model)
{
    if (m_model == model)
        return;
    if (m_model)
        m_model->disconnect(this);
    m_model = model;
    m_textureDirty = true;
    if (m_model) {
        connect(m_model, &DocumentModel::changed, this, [this] { resize(); });
        connect(m_model, &DocumentModel::renderChanged, this,
                [this](const QString &clip, int frame) {
                    if (clip.isEmpty() || (clip == m_clip && frame == m_frame))
                        invalidateTexture();
                });
        connect(m_model, &DocumentModel::paletteChanged, this,
                &PixelGridItem::invalidateTexture);
    }
    emit modelChanged();
    resize();
    update();
}

void PixelGridItem::setClip(const QString &clip)
{
    if (m_clip == clip)
        return;
    m_clip = clip;
    m_textureDirty = true;
    emit specChanged();
}

void PixelGridItem::setFrame(int frame)
{
    if (m_frame == frame)
        return;
    m_frame = frame;
    m_textureDirty = true;
    emit specChanged();
}

void PixelGridItem::setIsolatedLayer(const QString &layer)
{
    if (m_isolatedLayer == layer)
        return;
    m_isolatedLayer = layer;
    m_textureDirty = true;
    emit specChanged();
}

void PixelGridItem::setIncludeHidden(bool includeHidden)
{
    if (m_includeHidden == includeHidden)
        return;
    m_includeHidden = includeHidden;
    m_textureDirty = true;
    emit specChanged();
}

void PixelGridItem::setCell(qreal cell)
{
    if (qFuzzyCompare(m_cell, cell))
        return;
    m_cell = cell;
    emit specChanged();
}

void PixelGridItem::setChecker(bool checker)
{
    if (m_checker == checker)
        return;
    m_checker = checker;
    m_textureDirty = true;
    emit specChanged();
}

void PixelGridItem::setMesh(bool mesh)
{
    if (m_mesh == mesh)
        return;
    m_mesh = mesh;
    emit specChanged();
}

void PixelGridItem::setCheckerDark(const QColor &colour)
{
    if (m_checkerDark == colour)
        return;
    m_checkerDark = colour;
    m_textureDirty = true;
    emit specChanged();
}

void PixelGridItem::setCheckerLight(const QColor &colour)
{
    if (m_checkerLight == colour)
        return;
    m_checkerLight = colour;
    m_textureDirty = true;
    emit specChanged();
}

void PixelGridItem::setMeshColour(const QColor &colour)
{
    if (m_meshColour == colour)
        return;
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

void PixelGridItem::invalidateTexture()
{
    m_textureDirty = true;
    update();
}

QSGNode *PixelGridItem::updatePaintNode(QSGNode *oldNode,
                                        UpdatePaintNodeData *)
{
    auto *node = static_cast<PixelGridNode *>(oldNode);
    if (!node)
        node = new PixelGridNode;
    node->image->setRect(boundingRect());

    if (m_model && (m_textureDirty || !node->texture)) {
        render::Options options;
        options.scale = 1;
        options.checker = m_checker;
        options.checkerDark = m_checkerDark;
        options.checkerLight = m_checkerLight;
        options.isolated = !m_isolatedLayer.isEmpty();
        options.layer = m_isolatedLayer;
        options.includeHidden = m_includeHidden;
        const QImage image =
            render::toImage(m_model->document(), m_clip, m_frame, options);
        if (!image.isNull() && window()) {
            node->replaceTexture(window()->createTextureFromImage(
                image, QQuickWindow::TextureHasAlphaChannel));
        }
        m_textureDirty = false;
    }

    const int columns = m_model ? m_model->columns() : 0;
    const int rows = m_model ? m_model->rows() : 0;
    const bool drawMesh = m_mesh && m_cell >= 4 && columns > 0 && rows > 0;
    QSGGeometry *geometry = node->mesh->geometry();
    geometry->allocate(drawMesh ? (columns + rows + 2) * 2 : 0);
    if (drawMesh) {
        auto *vertices = geometry->vertexDataAsColoredPoint2D();
        auto add = [&](qreal x, qreal y) {
            vertices->set(float(x), float(y), uchar(m_meshColour.red()),
                          uchar(m_meshColour.green()), uchar(m_meshColour.blue()),
                          uchar(m_meshColour.alpha()));
            ++vertices;
        };
        for (int x = 0; x <= columns; ++x) {
            add(x * m_cell, 0);
            add(x * m_cell, rows * m_cell);
        }
        for (int y = 0; y <= rows; ++y) {
            add(0, y * m_cell);
            add(columns * m_cell, y * m_cell);
        }
    }
    geometry->markVertexDataDirty();
    node->mesh->markDirty(QSGNode::DirtyGeometry);

    return node;
}

} // namespace omapixel
