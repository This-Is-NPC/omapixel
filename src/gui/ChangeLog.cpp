#include "ChangeLog.h"

#include "DocumentModel.h"
#include "Differences.h"
#include "Strings.h"

namespace omapixel {

ChangeLog::ChangeLog(QObject *parent)
    : QAbstractListModel(parent), m_coalesceWindowMs(coalesceMs)
{
}

ChangeLog::ChangeLog(int coalesceWindowMs, QObject *parent)
    : QAbstractListModel(parent), m_coalesceWindowMs(coalesceWindowMs)
{
}

void ChangeLog::follow(const DocumentModel *model)
{
    m_model = model;
    sync();
    connect(m_model, &DocumentModel::changed, this, &ChangeLog::record);
}

void ChangeLog::sync()
{
    if (!m_model)
        return;
    m_lastSeen = m_model->document();
    m_burstBase = m_lastSeen;
}

int ChangeLog::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QVariant ChangeLog::data(const QModelIndex &index, int role) const
{
    if (index.row() < 0 || index.row() >= m_entries.size())
        return QVariant();
    const Entry &entry = m_entries.at(index.row());
    switch (role) {
    case OriginRole:
        return entry.origin;
    case DescriptionRole:
        return entry.description;
    case WhenRole:
        return entry.when;
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> ChangeLog::roleNames() const
{
    return {{OriginRole, "origin"},
            {DescriptionRole, "description"},
            {WhenRole, "whenMs"}};
}

void ChangeLog::record()
{
    if (!m_model)
        return;

    const Document &current = m_model->document();
    if (current == m_lastSeen) {
        // A signal with nothing to say for it -- the palette moving, a view
        // that changed shape without the drawing changing. Not history.
        return;
    }

    // Every entry describes the whole burst it belongs to, measured from
    // where that burst started rather than from the previous pixel. The same
    // summarizer the status bar uses caps the sentence, so a long diff
    // claims its tail instead of reciting it.
    const QString description =
        summarizeDifferences(documentDifferences(m_burstBase, current,
                                                 QStringLiteral("before"),
                                                 QStringLiteral("after")))
            .join(QStringLiteral("; "));

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const bool external = m_model->lastChangeWasExternal();
    // `cli` and `studio` are identifiers, not sentences -- the same class of
    // label as `clip[i] (a/b)` inside a diff line, and classified as such
    // rather than keyed into the catalogue. Everything around them speaks
    // through it.
    const QString origin =
        external ? QStringLiteral("cli") : QStringLiteral("studio");

    if (!m_entries.isEmpty()) {
        Entry &newest = m_entries.last();
        if (m_coalesceWindowMs >= 0 && newest.origin == origin
            && now - newest.when <= m_coalesceWindowMs) {
            // The same hand, still moving: one sentence that swells rather
            // than one sentence per pixel.
            newest.description = description;
            newest.when = now;
            const QModelIndex last = index(m_entries.size() - 1, 0);
            emit dataChanged(last, last);
            m_lastSeen = current;
            return;
        }
    }

    beginInsertRows(QModelIndex(), m_entries.size(), m_entries.size());
    m_entries.append({origin, description, now});
    endInsertRows();
    emit countChanged();
    // The new entry's burst began where the last observation ended, so any
    // same-origin change that follows inside the window revises THIS
    // sentence against the state it started from.
    m_burstBase = m_lastSeen;
    m_lastSeen = current;
}

} // namespace omapixel
