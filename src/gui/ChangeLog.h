#pragma once

#include "Document.h"

#include <QAbstractListModel>
#include <QList>
#include <QDateTime>

namespace omapixel {

class DocumentModel;

/// What changed this session, newest last, as plain sentences.
///
/// A RECORD, not a second history system: entries name their origin -- the
/// studio's own hand or an outside write through the CLI -- and say what
/// moved, and nothing more. There is deliberately no way to click an entry
/// back into being; restoring is undo's job, and undo's rules are the only
/// rules. The log keeps no document snapshots, so it costs nothing like what
/// the undo stack costs and answers something the undo stack cannot: what
/// happened here while I was not the one holding the pen.
///
/// Changes arriving faster than `coalesceMs` from the same origin collapse
/// into one growing entry -- a dragged stroke is one sentence that swells,
/// not one sentence per pixel.
class ChangeLog : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        OriginRole = Qt::UserRole + 1,
        DescriptionRole,
        WhenRole
    };

    explicit ChangeLog(QObject *parent = nullptr);

    /// `coalesceWindowMs` overrides how long same-origin changes keep
    /// revising the newest entry. Tests pass zero so every change lands as
    /// its own entry regardless of how fast the machine runs them.
    ChangeLog(int coalesceWindowMs, QObject *parent = nullptr);

    /// How many entries the session has accumulated.
    int count() const { return m_entries.size(); }

    /// Hooks the model this log watches. Every `changed()` the model sends
    /// becomes zero or one entries: none when the documents are equal, one
    /// otherwise, described through core's `documentDifferences`.
    void follow(const DocumentModel *model);

    /// Aligns the baseline with the model's current document WITHOUT writing
    /// an entry. For whole-document swaps that are not changes to a drawing
    /// -- opening another file, starting a new one -- which the status bar
    /// already announces in its own words.
    void sync();

    // QAbstractListModel
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void countChanged();

private:
    /// One logged change.
    struct Entry {
        QString origin;
        QString description;
        qint64 when = 0;
    };

    void record();

    const DocumentModel *m_model = nullptr;
    QList<Entry> m_entries;
    /// The last document observed, so a signal that changed nothing is
    /// recognised and skipped.
    Document m_lastSeen;
    /// Where the newest entry's burst began: the state its description is
    /// measured from, for as long as the burst keeps going.
    Document m_burstBase;

    /// Same-origin changes inside this window revise the newest entry rather
    /// than adding one. Long enough to swallow a drag; short enough that two
    /// deliberate actions stay two entries.
    static constexpr int coalesceMs = 800;
    int m_coalesceWindowMs = coalesceMs;
};

} // namespace omapixel
