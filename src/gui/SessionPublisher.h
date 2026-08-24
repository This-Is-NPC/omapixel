#pragma once

#include "DocumentModel.h"

#include <QObject>
#include <QList>
#include <QString>

class QSocketNotifier;
class QTimer;

namespace omapixel {

/// Publishes what this studio has open over a process-bound abstract Unix
/// socket. The command line validates the peer PID, UID, start time, and
/// executable before accepting the snapshot; no mutable sidecar is trusted.
///
/// The versioned record always includes the clip, frame, active layer identity,
/// and edit scope. A selected rectangle repeats its owner so it cannot be
/// mistaken for a different layer. Caret and playback remain local; frame
/// changes during playback publish the latest atomic snapshot.
///
/// The IPC contract itself lives in core (`sessions::`), because the CLI's
/// `where` queries the same endpoint: one description of a session, two front
/// ends, or a behaviour right in one and wrong in the other.
class SessionPublisher : public QObject
{
    Q_OBJECT

public:
    explicit SessionPublisher(QObject *parent = nullptr);
    ~SessionPublisher() override;

    /// Follows a model and keeps this process's session file current. The
    /// model announces file and selection changes through separate signals.
    void follow(const DocumentModel *model);

    /// Closes the process-bound endpoint. Called on clean exit; a studio that
    /// died without retiring leaves no mutable record for `where` to trust.
    void retire();

    QByteArray snapshot() const { return m_snapshot; }

signals:
    void publicationFailed(const QString &error);

private:
    struct PendingClient {
        int fd = -1;
        QByteArray bytes;
        qsizetype written = 0;
        QSocketNotifier *notifier = nullptr;
        QTimer *timer = nullptr;
    };

    void acceptClient();
    void trySendClient(int client);
    void finishClient(int client);
    void write();

    const DocumentModel *m_model = nullptr;
    int m_server = -1;
    QSocketNotifier *m_notifier = nullptr;
    QByteArray m_snapshot;
    QList<PendingClient> m_pending;
};

} // namespace omapixel
