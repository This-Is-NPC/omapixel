#pragma once

#include "Palette.h"

#include <QAbstractListModel>

namespace omapixel {

/// The palette, as a list model.
///
/// It exists for one reason: cost. A QML view fed a plain list rebuilds every
/// delegate whenever that list is assigned, and the palette is assigned again
/// every time a colour is added -- which, holding down a key that adds one, is
/// several hundred items destroyed and remade per press. This announces what
/// actually changed, so adding a colour costs one row.
///
/// The empty slot is row zero and belongs to the view rather than the document:
/// a transparent square is a square the colour of the background, and nobody
/// guesses that is the eraser.
class PaletteModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role { SlotRole = Qt::UserRole + 1, ColourRole };

    explicit PaletteModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    /// Brings the model in line with `palette`, saying as little as it can:
    /// an append when slots were only added, a dataChanged when one colour
    /// moved, and a reset only when the shape genuinely changed.
    void sync(const Palette &palette);

private:
    QList<Palette::Slot> m_rows;   // the document's slots; the empty one is not here
};

} // namespace omapixel
