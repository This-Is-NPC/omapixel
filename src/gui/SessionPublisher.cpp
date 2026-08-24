#include "SessionPublisher.h"

#include "Sessions.h"
#include "Output.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSocketNotifier>
#include <QTimer>

#include <sys/socket.h>
#include <cerrno>
#include <unistd.h>

namespace omapixel {

SessionPublisher::SessionPublisher(QObject *parent) : QObject(parent)
{
}

SessionPublisher::~SessionPublisher()
{
    retire();
}

void SessionPublisher::follow(const DocumentModel *model)
{
    m_model = model;
    const qint64 pid = QCoreApplication::applicationPid();
    const QString executable = sessions::executableOf(pid);
    const QString expected = sessions::expectedStudioExecutable();
    if (sessions::startTimeOf(pid) <= 0 || executable.isEmpty() || expected.isEmpty()) {
        emit publicationFailed(QStringLiteral(
            "Studio executable identity unavailable; session publication refused"));
        return;
    }
    m_server = sessions::openPublisher(QCoreApplication::applicationPid());
    if (m_server < 0) {
        emit publicationFailed(QStringLiteral("Studio session IPC could not start"));
        return;
    }
    m_notifier = new QSocketNotifier(m_server, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this,
            &SessionPublisher::acceptClient);
    connect(m_model, &DocumentModel::fileChanged, this, &SessionPublisher::write);
    connect(m_model, &DocumentModel::viewChanged, this, &SessionPublisher::write);
    connect(m_model, &DocumentModel::selectionChanged, this,
            &SessionPublisher::write);
    connect(m_model, &DocumentModel::documentReplaced, this,
            &SessionPublisher::write);
    write();
}

void SessionPublisher::retire()
{
    if (m_notifier)
        m_notifier->setEnabled(false);
    if (m_server >= 0) {
        ::close(m_server);
        m_server = -1;
    }
    while (!m_pending.isEmpty())
        finishClient(m_pending.first().fd);
}

void SessionPublisher::acceptClient()
{
    if (m_server < 0)
        return;
    qint64 peerPid = 0;
    uint peerUid = 0;
    QString error;
    const int client = sessions::acceptPublisher(m_server, &peerPid, &peerUid, &error);
    if (client < 0) {
        if (!error.isEmpty())
            emit publicationFailed(error);
        return;
    }
    if (peerUid != uint(::geteuid())) {
        ::close(client);
        return;
    }
    if (m_snapshot.isEmpty()) {
        ::close(client);
        return;
    }
    PendingClient pending;
    pending.fd = client;
    pending.bytes = m_snapshot;
    pending.notifier = new QSocketNotifier(client, QSocketNotifier::Write, this);
    pending.timer = new QTimer(this);
    pending.timer->setSingleShot(true);
    m_pending.append(pending);
    connect(m_pending.last().notifier, &QSocketNotifier::activated, this,
            [this, client] { trySendClient(client); });
    connect(m_pending.last().timer, &QTimer::timeout, this,
            [this, client] { finishClient(client); });
    m_pending.last().timer->start(1000);
    trySendClient(client);
}

void SessionPublisher::trySendClient(int client)
{
    for (int index = 0; index < m_pending.size(); ++index) {
        PendingClient &pending = m_pending[index];
        if (pending.fd != client)
            continue;
        const ssize_t count = ::send(client, pending.bytes.constData() + pending.written,
                                     size_t(pending.bytes.size() - pending.written),
                                     MSG_DONTWAIT | MSG_NOSIGNAL);
        if (count > 0) {
            pending.written += count;
            if (pending.written == pending.bytes.size())
                finishClient(client);
        } else if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK
                   && errno != EINTR) {
            finishClient(client);
        }
        return;
    }
}

void SessionPublisher::finishClient(int client)
{
    for (int index = 0; index < m_pending.size(); ++index) {
        if (m_pending.at(index).fd != client)
            continue;
        PendingClient pending = m_pending.takeAt(index);
        if (pending.notifier)
            pending.notifier->deleteLater();
        if (pending.timer)
            pending.timer->deleteLater();
        ::shutdown(client, SHUT_WR);
        ::close(client);
        return;
    }
}

void SessionPublisher::write()
{
    if (m_server < 0 || !m_model)
        return;

    // Absolute, so `omapixel where heart.json` matches from any working
    // directory. An untitled window advertises its scratch backing -- the
    // whole point of the file -- and stays an empty string when there is
    // nothing to advertise. Renaming over the target keeps a reader -- the
    // very agents this file serves -- off a half-written document.
    const QString advertised = m_model->followedPath();
    QJsonObject session;
    session.insert(QStringLiteral("version"), 2);
    session.insert(QStringLiteral("pid"), QCoreApplication::applicationPid());
    session.insert(QStringLiteral("started"),
                   sessions::startTimeOf(QCoreApplication::applicationPid()));
    session.insert(QStringLiteral("executable"),
                   sessions::executableOf(QCoreApplication::applicationPid()));
    session.insert(QStringLiteral("path"),
                   advertised.isEmpty()
                        ? QString()
                        : (QFileInfo(advertised).canonicalFilePath().isEmpty()
                               ? QFileInfo(advertised).absoluteFilePath()
                               : QFileInfo(advertised).canonicalFilePath()));
    session.insert(QStringLiteral("dirty"), m_model->dirty());
    QJsonObject view;
    view.insert(QStringLiteral("clip"), m_model->clip());
    view.insert(QStringLiteral("frame"), m_model->frame());
    view.insert(QStringLiteral("layerId"), m_model->activeLayerId());
    view.insert(QStringLiteral("layerName"), m_model->activeLayerName());
    view.insert(QStringLiteral("scope"), m_model->editScope());
    session.insert(QStringLiteral("view"), view);
    if (m_model->hasSelection()) {
        QJsonObject selection;
        selection.insert(QStringLiteral("clip"), m_model->clip());
        selection.insert(QStringLiteral("frame"), m_model->frame());
        selection.insert(QStringLiteral("layerId"), m_model->activeLayerId());
        selection.insert(QStringLiteral("layerName"), m_model->activeLayerName());
        selection.insert(QStringLiteral("x"), m_model->selectionX());
        selection.insert(QStringLiteral("y"), m_model->selectionY());
        selection.insert(QStringLiteral("width"), m_model->selectionWidth());
        selection.insert(QStringLiteral("height"), m_model->selectionHeight());
        selection.insert(QStringLiteral("count"), m_model->selectionCount());
        session.insert(QStringLiteral("selection"), selection);
    } else {
        session.insert(QStringLiteral("selection"), QJsonValue::Null);
    }

    m_snapshot = QJsonDocument(session).toJson(QJsonDocument::Compact) + '\n';
    if (m_snapshot.size() > 64 * 1024) {
        m_snapshot.clear();
        emit publicationFailed(QStringLiteral("Studio session snapshot exceeds IPC limit"));
    }
}

} // namespace omapixel
