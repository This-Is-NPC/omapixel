#include "PaletteModel.h"

namespace omapixel {

PaletteModel::PaletteModel(QObject *parent) : QAbstractListModel(parent)
{
}

int PaletteModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid())
        return 0;
    return m_rows.size() + 1;   // plus the empty slot at the front
}

QVariant PaletteModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() > m_rows.size())
        return QVariant();

    if (index.row() == 0)
        return role == SlotRole ? QVariant(QStringLiteral(".")) : QVariant(QColor());

    const Palette::Slot &slot = m_rows.at(index.row() - 1);
    switch (role) {
    case SlotRole:
        return QString(slot.letter);
    case ColourRole:
        return slot.colour;
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> PaletteModel::roleNames() const
{
    return {{SlotRole, "slot"}, {ColourRole, "colour"}};
}

void PaletteModel::sync(const Palette &palette)
{
    const QList<Palette::Slot> next = palette.entries();

    // Only appended: the common case, and the one worth being careful about --
    // it is what holding down a colour-adding key does.
    if (next.size() > m_rows.size()) {
        bool sameStart = true;
        for (int i = 0; i < m_rows.size(); ++i) {
            if (next.at(i).letter != m_rows.at(i).letter
                || next.at(i).colour != m_rows.at(i).colour) {
                sameStart = false;
                break;
            }
        }
        if (sameStart) {
            beginInsertRows(QModelIndex(), m_rows.size() + 1, next.size());
            m_rows = next;
            endInsertRows();
            return;
        }
    }

    // Same slots, one or more colours moved.
    if (next.size() == m_rows.size()) {
        int firstMoved = -1;
        int lastMoved = -1;
        bool sameLetters = true;
        for (int i = 0; i < next.size(); ++i) {
            if (next.at(i).letter != m_rows.at(i).letter) {
                sameLetters = false;
                break;
            }
            if (next.at(i).colour != m_rows.at(i).colour) {
                if (firstMoved < 0)
                    firstMoved = i;
                lastMoved = i;
            }
        }
        if (sameLetters) {
            m_rows = next;
            if (firstMoved >= 0) {
                emit dataChanged(index(firstMoved + 1), index(lastMoved + 1),
                                 {ColourRole});
            }
            return;
        }
    }

    beginResetModel();
    m_rows = next;
    endResetModel();
}

} // namespace omapixel
