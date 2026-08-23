#pragma once

#include "DocumentModel.h"

#include <QQmlEngine>
#include <QQuickItem>

class QSGNode;

namespace omapixel {

/// Draws one frame of a document.
///
/// It paints through `render::toImage` rather than laying out rectangles of its
/// own. The studio and PNG therefore cannot disagree: there is one renderer and
/// this is a nearest-neighbour blit of its result.
///
/// Everything that shows pixels goes through here: the drawing surface, the
/// timeline thumbnails, the true-size strip, the onion skin.
class PixelGridItem : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(omapixel::DocumentModel *model READ model WRITE setModel NOTIFY modelChanged)
    Q_PROPERTY(QString clip READ clip WRITE setClip NOTIFY specChanged)
    Q_PROPERTY(int frame READ frame WRITE setFrame NOTIFY specChanged)
    Q_PROPERTY(qreal cell READ cell WRITE setCell NOTIFY specChanged)
    Q_PROPERTY(bool checker READ checker WRITE setChecker NOTIFY specChanged)
    Q_PROPERTY(bool mesh READ mesh WRITE setMesh NOTIFY specChanged)
    Q_PROPERTY(QColor checkerDark READ checkerDark WRITE setCheckerDark NOTIFY specChanged)
    Q_PROPERTY(QColor checkerLight READ checkerLight WRITE setCheckerLight NOTIFY specChanged)
    Q_PROPERTY(QColor meshColour READ meshColour WRITE setMeshColour NOTIFY specChanged)

public:
    explicit PixelGridItem(QQuickItem *parent = nullptr);

    DocumentModel *model() const { return m_model; }
    void setModel(DocumentModel *model);

    QString clip() const { return m_clip; }
    void setClip(const QString &clip);
    int frame() const { return m_frame; }
    void setFrame(int frame);
    qreal cell() const { return m_cell; }
    void setCell(qreal cell);
    bool checker() const { return m_checker; }
    void setChecker(bool checker);
    bool mesh() const { return m_mesh; }
    void setMesh(bool mesh);
    QColor checkerDark() const { return m_checkerDark; }
    void setCheckerDark(const QColor &colour);
    QColor checkerLight() const { return m_checkerLight; }
    void setCheckerLight(const QColor &colour);
    QColor meshColour() const { return m_meshColour; }
    void setMeshColour(const QColor &colour);

signals:
    void modelChanged();
    void specChanged();

private:
    void resize();
    void invalidateTexture();
    QSGNode *updatePaintNode(QSGNode *oldNode,
                             UpdatePaintNodeData *data) override;

    DocumentModel *m_model = nullptr;
    QString m_clip;
    int m_frame = 0;
    qreal m_cell = 1;
    bool m_checker = false;
    bool m_mesh = false;
    QColor m_checkerDark{"#1B1C26"};
    QColor m_checkerLight{"#22242F"};
    QColor m_meshColour{255, 255, 255, 16};
    bool m_textureDirty = true;
};

} // namespace omapixel
