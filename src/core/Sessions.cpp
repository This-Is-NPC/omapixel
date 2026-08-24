#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "Sessions.h"
#include "Document.h"
#include "TextSafety.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QHash>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QElapsedTimer>
#include <QDebug>

#include <cmath>
#include <limits>
#include <algorithm>
#include <cerrno>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <cstring>
#include <unistd.h>

namespace omapixel {
namespace sessions {

namespace {

constexpr int maxStudioProcesses = 4096;
constexpr qint64 maxIpcReplyBytes = 64 * 1024;
constexpr int ipcTimeoutMs = 1000;
constexpr int discoveryTimeoutMs = 1000;
constexpr int discoveryEndpointTimeoutMs = 250;

void debugSession(const QString &message)
{
    if (qEnvironmentVariableIsSet("OMAPIXEL_SESSION_DEBUG"))
        qInfo().noquote() << "session:" << message;
}

bool makeNonblocking(int fd)
{
    const int flags = ::fcntl(fd, F_GETFL, 0);
    return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

int remainingMs(const QElapsedTimer &timer)
{
    return qMax(0, ipcTimeoutMs - int(timer.elapsed()));
}

bool waitForSocket(int fd, short events, const QElapsedTimer &timer)
{
    while (remainingMs(timer) > 0) {
        struct pollfd wait{fd, events, 0};
        const int result = ::poll(&wait, 1, remainingMs(timer));
        if (result < 0 && errno == EINTR)
            continue;
        if (result <= 0)
            return false;
        if (wait.revents & (events | POLLERR | POLLHUP | POLLNVAL))
            return (wait.revents & events) != 0;
    }
    return false;
}

QByteArray endpointName(qint64 pid)
{
    QByteArray name(1, '\0');
    name += QByteArrayLiteral("omapixel-studio-");
    name += QByteArray::number(pid);
    return name;
}

bool addressFor(qint64 pid, sockaddr_un *address, socklen_t *length)
{
    const QByteArray name = endpointName(pid);
    if (name.size() >= int(sizeof(address->sun_path)))
        return false;
    memset(address, 0, sizeof(*address));
    address->sun_family = AF_UNIX;
    memcpy(address->sun_path, name.constData(), size_t(name.size()));
    *length = offsetof(sockaddr_un, sun_path) + socklen_t(name.size());
    return true;
}

QString studioExecutable()
{
    const QString applicationDir = QCoreApplication::applicationDirPath();
    const QStringList candidates{
        applicationDir + QStringLiteral("/omapixel-studio"),
        applicationDir + QStringLiteral("/../bin/omapixel-studio"),
        applicationDir + QStringLiteral("/../build/bin/omapixel-studio")};
    for (const QString &candidate : candidates) {
        const QString canonical = QFileInfo(candidate).canonicalFilePath();
        if (!canonical.isEmpty())
            return canonical;
    }
    return QString();
}

bool isStudioProcess(qint64 pid)
{
    const QString executable = executableOf(pid);
    if (executable.isEmpty())
        return false;
    const QString expected = studioExecutable();
    return !expected.isEmpty() && executable == expected;
}

/// The runtime base both families of files hang off: `XDG_RUNTIME_DIR`
/// first, then Qt's runtime location, which falls back rather than
/// returning nothing. Empty means nowhere sane.
QString runtimeBase()
{
    QString base = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    const QFileInfo info(base);
    if (!info.exists() || !info.isDir() || info.ownerId() != uint(::geteuid()))
        return QString();
    const QFileDevice::Permissions unsafe = QFileDevice::ReadGroup
        | QFileDevice::WriteGroup | QFileDevice::ExeGroup
        | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
    return (info.permissions() & unsafe) ? QString() : info.absoluteFilePath();
}

/// Tri-state on purpose: `Dead` may prune, `Unknown` must not. A stat that
/// cannot be READ -- hidepid, containers, seccomp -- is not evidence of
/// death, and deleting a healthy studio's session over it would answer
/// "nobody" while somebody was looking.
enum class Liveness { Dead, Alive, Unknown };

Liveness livenessOf(qint64 pid, qint64 *started)
{
    *started = 0;
    // /proc/<pid>/stat: the second field is the command in parens, and a
    // command may contain anything -- including spaces and other parens. So
    // the fields are counted from after the LAST ')', where field 3 (state)
    // is the first token; starttime is field 22, the nineteenth from there.
    const QString path = QStringLiteral("/proc/%1/stat").arg(pid);
    if (!QFileInfo::exists(path))
        return Liveness::Dead;               // no such process: prunable
    QFile stat(path);
    if (!stat.open(QIODevice::ReadOnly))
        return Liveness::Unknown;            // exists but unreadable: not evidence
    const QString line = QString::fromLatin1(stat.readAll());
    const qsizetype close = line.lastIndexOf(QLatin1Char(')'));
    if (close < 0)
        return Liveness::Unknown;
    const QStringList fields =
        line.mid(close + 1).simplified().split(QLatin1Char(' '));
    if (fields.size() <= 19)
        return Liveness::Unknown;
    bool whole = false;
    const qint64 value = fields.at(19).toLongLong(&whole);
    if (!whole || value <= 0)
        return Liveness::Unknown;
    *started = value;
    return Liveness::Alive;
}

} // namespace

QString directory()
{
    const QString base = runtimeBase();
    if (base.isEmpty())
        return QString();
    const QString path = base + QStringLiteral("/omapixel/sessions");
    if (!QDir().mkpath(path))
        return QString();
    const QFileDevice::Permissions unsafe = QFileDevice::ReadGroup
        | QFileDevice::WriteGroup | QFileDevice::ExeGroup
        | QFileDevice::ReadOther | QFileDevice::WriteOther | QFileDevice::ExeOther;
    const QFileInfo info(path);
    if (info.ownerId() != uint(::geteuid()) || (info.permissions() & unsafe))
        QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                   | QFileDevice::ExeOwner);
    const QFileInfo secured(path);
    return secured.ownerId() == uint(::geteuid())
               && !(secured.permissions() & unsafe)
           ? path
           : QString();
}

QString scratchDirectory()
{
    const QString base = runtimeBase();
    if (base.isEmpty())
        return QString();
    const QString path = base + QStringLiteral("/omapixel/scratch");
    if (!QDir().mkpath(path))
        return QString();
    QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                               | QFileDevice::ExeOwner);
    return path;
}

QString scratchPath(qint64 pid)
{
    const QString dir = scratchDirectory();
    if (dir.isEmpty())
        return QString();
    return dir + QStringLiteral("/%1.json").arg(pid);
}

qint64 startTimeOf(qint64 pid)
{
    // Reading /proc is Linux-only. That is accepted, not accidental: the
    // distribution target is Omarchy/Arch.
    qint64 started = 0;
    livenessOf(pid, &started);
    return started;
}

QString executableOf(qint64 pid)
{
    if (pid <= 0)
        return QString();
    return QFileInfo(QStringLiteral("/proc/%1/exe").arg(pid)).canonicalFilePath();
}

QString expectedStudioExecutable()
{
    return studioExecutable();
}

int openPublisher(qint64 pid, QString *error)
{
    sockaddr_un address;
    socklen_t length = 0;
    if (!addressFor(pid, &address, &length)) {
        if (error)
            *error = QStringLiteral("session IPC endpoint name is too long");
        return -1;
    }
    const int server = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (server < 0) {
        if (error)
            *error = QStringLiteral("session IPC socket: %1").arg(QString::fromLocal8Bit(strerror(errno)));
        return -1;
    }
    if (::bind(server, reinterpret_cast<sockaddr *>(&address), length) < 0
        || ::listen(server, 8) < 0) {
        if (error)
            *error = QStringLiteral("session IPC bind/listen: %1")
                         .arg(QString::fromLocal8Bit(strerror(errno)));
        ::close(server);
        return -1;
    }
    if (!makeNonblocking(server)) {
        if (error)
            *error = QStringLiteral("session IPC server is not nonblocking");
        ::close(server);
        return -1;
    }
    return server;
}

int acceptPublisher(int server, qint64 *peerPid, uint *peerUid, QString *error)
{
    const int client = ::accept4(server, nullptr, nullptr, SOCK_CLOEXEC);
    if (client < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)
            return -1;
        if (error)
            *error = QStringLiteral("session IPC accept: %1")
                         .arg(QString::fromLocal8Bit(strerror(errno)));
        return -1;
    }
    struct ucred credentials;
    socklen_t length = sizeof(credentials);
    if (::getsockopt(client, SOL_SOCKET, SO_PEERCRED, &credentials, &length) < 0) {
        if (error)
            *error = QStringLiteral("session IPC peer credentials: %1")
                         .arg(QString::fromLocal8Bit(strerror(errno)));
        ::close(client);
        return -1;
    }
    if (peerPid)
        *peerPid = credentials.pid;
    if (peerUid)
        *peerUid = credentials.uid;
    if (!makeNonblocking(client)) {
        if (error)
            *error = QStringLiteral("session IPC client is not nonblocking");
        ::close(client);
        return -1;
    }
    return client;
}

bool sendPublisher(int client, const QByteArray &bytes, QString *error)
{
    if (bytes.size() > maxIpcReplyBytes) {
        if (error)
            *error = QStringLiteral("session IPC response is too large");
        return false;
    }
    QElapsedTimer timer;
    timer.start();
    qsizetype written = 0;
    while (written < bytes.size()) {
        const ssize_t count = ::send(client, bytes.constData() + written,
                                     size_t(bytes.size() - written), MSG_NOSIGNAL);
        if (count > 0) {
            written += count;
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            if (waitForSocket(client, POLLOUT, timer))
                continue;
        }
        if (error)
            *error = QStringLiteral("session IPC response: %1")
                         .arg(QString::fromLocal8Bit(strerror(errno)));
        return false;
    }
    if (written != bytes.size()) {
        if (error)
            *error = QStringLiteral("session IPC response deadline exceeded");
        return false;
    }
    return true;
}

bool readPublisher(qint64 pid, QByteArray *bytes, QString *error)
{
    sockaddr_un address;
    socklen_t addressLength = 0;
    if (!addressFor(pid, &address, &addressLength)) {
        if (error)
            *error = QStringLiteral("session IPC endpoint name is too long");
        return false;
    }
    QElapsedTimer timer;
    timer.start();
    const int client = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (client < 0) {
        if (error)
            *error = QStringLiteral("session IPC socket: %1")
                         .arg(QString::fromLocal8Bit(strerror(errno)));
        return false;
    }
    if (!makeNonblocking(client)) {
        if (error)
            *error = QStringLiteral("session IPC client is not nonblocking");
        ::close(client);
        return false;
    }
    int connected = ::connect(client, reinterpret_cast<sockaddr *>(&address),
                              addressLength);
    if (connected < 0 && errno == EINPROGRESS) {
        if (!waitForSocket(client, POLLOUT, timer)) {
            if (error)
                *error = QStringLiteral("session IPC connect deadline exceeded");
            ::close(client);
            return false;
        }
        int socketError = 0;
        socklen_t socketErrorLength = sizeof(socketError);
        if (::getsockopt(client, SOL_SOCKET, SO_ERROR, &socketError,
                         &socketErrorLength) < 0 || socketError != 0) {
            if (error)
                *error = QStringLiteral("session IPC connect: %1")
                             .arg(QString::fromLocal8Bit(strerror(socketError == 0
                                                                       ? errno
                                                                       : socketError)));
            ::close(client);
            return false;
        }
        connected = 0;
    }
    if (connected < 0) {
        if (error)
            *error = QStringLiteral("session IPC connect: %1")
                         .arg(QString::fromLocal8Bit(strerror(errno)));
        ::close(client);
        return false;
    }
    struct ucred peer;
    socklen_t peerLength = sizeof(peer);
    if (::getsockopt(client, SOL_SOCKET, SO_PEERCRED, &peer, &peerLength) < 0
        || peer.uid != ::geteuid() || peer.pid != pid) {
        if (error)
            *error = QStringLiteral("session IPC peer identity mismatch");
        ::close(client);
        return false;
    }
    // The publisher sends one complete snapshot as soon as it accepts a
    // connection. Do not send a request here: a fast publisher may already
    // have sent and closed before a client-side request is scheduled.
    QByteArray response;
    bool complete = false;
    while (response.size() <= maxIpcReplyBytes) {
        if (!waitForSocket(client, POLLIN, timer))
            break;
        char buffer[4096];
        const ssize_t count = ::read(client, buffer, sizeof(buffer));
        if (count == 0) {
            complete = true;
            break;
        }
        if (count < 0 && errno == EINTR)
            continue;
        if (count <= 0)
            break;
        response.append(buffer, int(count));
        if (response.endsWith('\n')) {
            complete = true;
            break;
        }
    }
    ::close(client);
    if (!complete || response.size() > maxIpcReplyBytes) {
        if (error)
            *error = QStringLiteral("session IPC response was incomplete or too large");
        return false;
    }
    if (bytes)
        *bytes = response;
    return true;
}

struct PendingPublisher
{
    qint64 pid = 0;
    int fd = -1;
    qint64 deadline = 0;
    bool connecting = true;
    QByteArray response;
};

bool authenticatePublisher(int fd, qint64 pid, QString *error)
{
    struct ucred peer;
    socklen_t peerLength = sizeof(peer);
    if (::getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &peerLength) < 0
        || peer.uid != ::geteuid() || peer.pid != pid) {
        if (error)
            *error = QStringLiteral("session IPC peer identity mismatch");
        return false;
    }
    return true;
}

bool startPublisherRead(qint64 pid, qint64 deadline, PendingPublisher *pending,
                        QString *error)
{
    sockaddr_un address;
    socklen_t addressLength = 0;
    if (!addressFor(pid, &address, &addressLength)) {
        if (error)
            *error = QStringLiteral("session IPC endpoint name is too long");
        return false;
    }
    const int client = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (client < 0) {
        if (error)
            *error = QStringLiteral("session IPC socket: %1")
                         .arg(QString::fromLocal8Bit(strerror(errno)));
        return false;
    }
    if (!makeNonblocking(client)) {
        if (error)
            *error = QStringLiteral("session IPC client is not nonblocking");
        ::close(client);
        return false;
    }
    const int connected = ::connect(client, reinterpret_cast<sockaddr *>(&address),
                                    addressLength);
    if (connected < 0 && errno != EINPROGRESS) {
        if (error)
            *error = QStringLiteral("session IPC connect: %1")
                         .arg(QString::fromLocal8Bit(strerror(errno)));
        ::close(client);
        return false;
    }
    pending->pid = pid;
    pending->fd = client;
    pending->deadline = deadline;
    pending->connecting = connected < 0;
    if (!pending->connecting && !authenticatePublisher(client, pid, error)) {
        ::close(client);
        pending->fd = -1;
        return false;
    }
    return true;
}

QHash<qint64, QByteArray> readPublishers(const QList<qint64> &pids)
{
    QHash<qint64, QByteArray> responses;
    QElapsedTimer timer;
    timer.start();
    QList<PendingPublisher> pending;
    for (const qint64 pid : pids) {
        PendingPublisher candidate;
        QString error;
        if (startPublisherRead(pid, timer.elapsed() + discoveryEndpointTimeoutMs,
                               &candidate, &error)) {
            pending.append(candidate);
        } else {
            debugSession(QStringLiteral("pid=%1 async endpoint rejected: %2")
                             .arg(pid).arg(error));
        }
    }

    while (!pending.isEmpty() && timer.elapsed() < discoveryTimeoutMs) {
        QList<pollfd> waits;
        waits.reserve(pending.size());
        int timeout = discoveryTimeoutMs - int(timer.elapsed());
        for (const PendingPublisher &candidate : pending) {
            waits.append({candidate.fd,
                          short(candidate.connecting ? POLLOUT : POLLIN), 0});
            timeout = qMin(timeout, int(candidate.deadline - timer.elapsed()));
        }
        if (timeout <= 0)
            break;
        const int result = ::poll(waits.data(), nfds_t(waits.size()), timeout);
        if (result < 0 && errno == EINTR)
            continue;
        if (result < 0)
            break;
        for (int index = pending.size() - 1; index >= 0; --index) {
            PendingPublisher &candidate = pending[index];
            const short events = waits.at(index).revents;
            bool remove = false;
            if (events == 0) {
                remove = timer.elapsed() >= candidate.deadline;
            } else if (candidate.connecting) {
                int socketError = 0;
                socklen_t socketErrorLength = sizeof(socketError);
                if (::getsockopt(candidate.fd, SOL_SOCKET, SO_ERROR,
                                 &socketError, &socketErrorLength) < 0
                    || socketError != 0
                    || !authenticatePublisher(candidate.fd, candidate.pid, nullptr)) {
                    remove = true;
                } else {
                    candidate.connecting = false;
                }
            } else if (events & POLLIN) {
                char buffer[4096];
                const ssize_t count = ::read(candidate.fd, buffer, sizeof(buffer));
                if (count > 0) {
                    candidate.response.append(buffer, int(count));
                    if (candidate.response.size() > maxIpcReplyBytes
                        || candidate.response.endsWith('\n')) {
                        if (candidate.response.size() <= maxIpcReplyBytes)
                            responses.insert(candidate.pid, candidate.response);
                        remove = true;
                    }
                } else if (count == 0
                           || (count < 0 && errno != EINTR
                               && errno != EAGAIN && errno != EWOULDBLOCK)) {
                    if (count == 0 && !candidate.response.isEmpty())
                        responses.insert(candidate.pid, candidate.response);
                    remove = true;
                }
            } else if (events & (POLLERR | POLLHUP | POLLNVAL)) {
                remove = true;
            }
            if (remove) {
                ::close(candidate.fd);
                pending.removeAt(index);
            }
        }
    }
    for (const PendingPublisher &candidate : pending)
        ::close(candidate.fd);
    return responses;
}

QList<Entry> live(const QString &path)
{
    QList<Entry> found;
    QElapsedTimer discoveryTimer;
    discoveryTimer.start();
    debugSession(QStringLiteral("live start path=%1 expected=%2")
                     .arg(path, studioExecutable()));
    // Old versions left JSON records behind. They are garbage only: never parse
    // or trust them as session authority, and bound cleanup so an attacker
    // cannot make discovery allocate an unbounded metadata set.
    const QString oldDir = directory();
    if (!oldDir.isEmpty()) {
        QDirIterator legacy(oldDir, {QStringLiteral("*.json")}, QDir::Files);
        int cleaned = 0;
        while (legacy.hasNext() && cleaned++ < 128) {
            const QString filePath = legacy.next();
            // State-bearing sidecars belong to the pre-IPC protocol. Remove
            // them without parsing: their contents are never authority.
            QFile::remove(filePath);
        }
    }

    const QString wanted = path.isEmpty()
                               ? QString()
                               : (QFileInfo(path).canonicalFilePath().isEmpty()
                                      ? QFileInfo(path).absoluteFilePath()
                                      : QFileInfo(path).canonicalFilePath());
    QDirIterator proc(QStringLiteral("/proc"), QDir::Dirs | QDir::NoDotAndDotDot);
    QList<qint64> candidates;
    int inspected = 0;
    while (proc.hasNext() && inspected < maxStudioProcesses) {
        const QString name = QFileInfo(proc.next()).fileName();
        bool numeric = false;
        const qint64 pid = name.toLongLong(&numeric);
        ++inspected;
        if (!numeric || pid <= 0 || !isStudioProcess(pid))
            continue;
        debugSession(QStringLiteral("candidate pid=%1 executable=%2 elapsed=%3ms")
                         .arg(pid)
                         .arg(executableOf(pid))
                         .arg(discoveryTimer.elapsed()));

        const qint64 started = startTimeOf(pid);
        if (started <= 0) {
            debugSession(QStringLiteral("pid=%1 rejected: no start time").arg(pid));
            continue;
        }
        candidates.append(pid);
    }

    const QHash<qint64, QByteArray> responses = readPublishers(candidates);
    for (const qint64 pid : candidates) {
        const auto response = responses.constFind(pid);
        if (response == responses.constEnd())
            continue;
        const QByteArray &bytes = response.value();
        const qint64 authenticatedPeerPid = pid;

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
        const QJsonObject session = document.object();
        const QJsonValue version = session.value(QStringLiteral("version"));
        const QJsonValue pidValue = session.value(QStringLiteral("pid"));
        const QJsonValue startedValue = session.value(QStringLiteral("started"));
        const QJsonValue executableValue = session.value(QStringLiteral("executable"));
        const QJsonValue pathValue = session.value(QStringLiteral("path"));
        const QJsonValue dirtyValue = session.value(QStringLiteral("dirty"));
        const QJsonObject view = session.value(QStringLiteral("view")).toObject();
        const auto safeString = [](const QJsonValue &value, int maximum,
                                   bool emptyAllowed = false) {
            if (!value.isString() || (!emptyAllowed && value.toString().isEmpty())
                || value.toString().size() > maximum)
                return false;
            return text::isSafe(value.toString(), emptyAllowed);
        };
        const auto exactInteger = [](const QJsonValue &value, qint64 minimum,
                                     qint64 maximum, qint64 *out = nullptr) {
            if (!value.isDouble())
                return false;
            const double number = value.toDouble();
            if (!std::isfinite(number) || std::floor(number) != number
                || number < double(minimum) || number > double(maximum))
                return false;
            if (out)
                *out = qint64(number);
            return true;
        };
        const auto exactKeys = [](const QJsonObject &object,
                                  const QStringList &keys) {
            if (object.size() != keys.size())
                return false;
            for (const QString &key : keys) {
                if (!object.contains(key))
                    return false;
            }
            return true;
        };
        const bool validRoot = parseError.error == QJsonParseError::NoError
                               && document.isObject() && version.isDouble()
                               && version.toInt(-1) == 2 && pathValue.isString()
                               && dirtyValue.isBool()
                                 && exactKeys(session, {QStringLiteral("version"),
                                                         QStringLiteral("pid"),
                                                         QStringLiteral("started"),
                                                         QStringLiteral("executable"),
                                                         QStringLiteral("path"),
                                                       QStringLiteral("dirty"),
                                                       QStringLiteral("view"),
                                                       QStringLiteral("selection")})
                               && exactKeys(view, {QStringLiteral("clip"),
                                                   QStringLiteral("frame"),
                                                   QStringLiteral("layerId"),
                                                   QStringLiteral("layerName"),
                                                   QStringLiteral("scope")})
                                && safeString(view.value(QStringLiteral("clip")), 128)
                                && safeString(view.value(QStringLiteral("layerId")), 64)
                                && safeString(view.value(QStringLiteral("layerName")), 128)
                                && safeString(view.value(QStringLiteral("scope")), 16)
                                  && safeString(executableValue, 4096)
                                && exactInteger(pidValue, 1, 1'000'000'000)
                                && exactInteger(startedValue, 1, std::numeric_limits<qint64>::max())
                                && (pathValue.toString().isEmpty()
                                    || (QFileInfo(pathValue.toString()).isAbsolute()
                                        && text::isSafe(pathValue.toString())))
                                && exactInteger(view.value(QStringLiteral("frame")), 0,
                                                std::numeric_limits<int>::max())
                                && safeString(view.value(QStringLiteral("layerId")), 64)
                                && safeString(view.value(QStringLiteral("layerName")), 128)
                               && (view.value(QStringLiteral("scope"))
                                       == QJsonValue(QStringLiteral("frame"))
                                   || view.value(QStringLiteral("scope"))
                                          == QJsonValue(QStringLiteral("all-frames")))
                               && session.contains(QStringLiteral("selection"));
          if (!validRoot) {
              continue;
          }
         qint64 reportedPid = 0;
         qint64 reportedStarted = 0;
         exactInteger(pidValue, 1, 1'000'000'000, &reportedPid);
         exactInteger(startedValue, 1, std::numeric_limits<qint64>::max(), &reportedStarted);

           if (reportedPid != authenticatedPeerPid
               || reportedStarted != qint64(startTimeOf(authenticatedPeerPid))
               || executableOf(authenticatedPeerPid) != executableValue.toString())
               {
               debugSession(QStringLiteral("pid=%1 rejected: identity record=%2/%3/%4 elapsed=%5ms")
                                .arg(pid)
                                .arg(reportedPid)
                                .arg(reportedStarted)
                                .arg(executableValue.toString())
                                .arg(discoveryTimer.elapsed()));
               continue;
               }

        Entry entry;
        entry.pid = reportedPid;
        entry.started = reportedStarted;
        entry.executable = executableValue.toString();
        entry.path = pathValue.toString();
        entry.dirty = dirtyValue.toBool();
        entry.clip = view.value(QStringLiteral("clip")).toString();
        entry.frame = view.value(QStringLiteral("frame")).toInt(-1);
        entry.layerId = view.value(QStringLiteral("layerId")).toString();
        entry.layerName = view.value(QStringLiteral("layerName")).toString();
        entry.scope = view.value(QStringLiteral("scope")).toString();
        const QJsonValue selected = session.value(QStringLiteral("selection"));
        if (!selected.isNull()) {
            const QJsonObject selection = selected.toObject();
             if (!exactKeys(selection, {QStringLiteral("clip"),
                                       QStringLiteral("frame"),
                                       QStringLiteral("layerId"),
                                       QStringLiteral("layerName"),
                                       QStringLiteral("x"),
                                       QStringLiteral("y"),
                                       QStringLiteral("width"),
                                       QStringLiteral("height"),
                                       QStringLiteral("count")})) {
                 continue;
            }
            const QJsonValue selectionClip =
                selection.value(QStringLiteral("clip"));
            const QJsonValue selectionFrame =
                selection.value(QStringLiteral("frame"));
            const QJsonValue selectionLayerId =
                selection.value(QStringLiteral("layerId"));
            const QJsonValue selectionLayerName =
                selection.value(QStringLiteral("layerName"));
            qint64 xValue = 0;
            qint64 yValue = 0;
            qint64 widthValue = 0;
            qint64 heightValue = 0;
            qint64 countValue = 0;
            const bool selectionNumbers =
                exactInteger(selection.value(QStringLiteral("x")), 0,
                             Document::maxDimension - 1, &xValue)
                && exactInteger(selection.value(QStringLiteral("y")), 0,
                                Document::maxDimension - 1, &yValue)
                && exactInteger(selection.value(QStringLiteral("width")), 1,
                                Document::maxDimension, &widthValue)
                && exactInteger(selection.value(QStringLiteral("height")), 1,
                                Document::maxDimension, &heightValue)
                && exactInteger(selection.value(QStringLiteral("count")), 1,
                                qint64(Document::maxDimension) * Document::maxDimension,
                                &countValue);
            const int x = int(xValue);
            const int y = int(yValue);
            const int width = int(widthValue);
            const int height = int(heightValue);
            const int count = int(countValue);
             if (!text::isSafe(selectionClip.toString(), false)
                 || !text::isSafe(selectionLayerId.toString(), false)
                 || !text::isSafe(selectionLayerName.toString(), false)
                 || !selectionFrame.isDouble()
                 || selectionFrame.toInt(-1) < 0 || !selectionNumbers
                || x < 0 || y < 0
                || width <= 0 || height <= 0
                || count != qint64(width) * qint64(height)
                || selectionClip.toString() != entry.clip
                || selectionFrame.toInt(-1) != entry.frame
                || selectionLayerId.toString() != entry.layerId
                || selectionLayerName.toString() != entry.layerName) {
                 continue;
            }
            entry.selectionClip = selectionClip.toString();
            entry.selectionFrame = selectionFrame.toInt(-1);
            entry.selectionLayerId = selectionLayerId.toString();
            entry.selectionLayerName = selectionLayerName.toString();
            entry.selection = QRect(x, y, width, height);
        }
           if (!wanted.isEmpty() && entry.path != wanted)
               continue;
           found.append(entry);
           debugSession(QStringLiteral("pid=%1 accepted path=%2 elapsed=%3ms")
                            .arg(pid)
                            .arg(entry.path)
                            .arg(discoveryTimer.elapsed()));
    }
    std::sort(found.begin(), found.end(), [](const Entry &left, const Entry &right) {
        return left.pid < right.pid;
    });
    return found;
}

} // namespace sessions
} // namespace omapixel
