#pragma once

#include <QList>
#include <QRect>
#include <QString>
#include <QByteArray>

namespace omapixel {
namespace sessions {

/// What one running studio has said about itself. `selection` is invalid when
/// the session JSON carries the required null selection. The view fields are
/// always present, including for a session without a pixel selection.
struct Entry {
    qint64 pid = 0;
    qint64 started = 0;
    QString executable;
    QString path;
    bool dirty = false;
    QString clip;
    int frame = -1;
    QString layerId;
    QString layerName;
    QString scope;
    QRect selection;
    QString selectionClip;
    int selectionFrame = -1;
    QString selectionLayerId;
    QString selectionLayerName;
};

/// Where session files live: `<runtime>/omapixel/sessions`. `XDG_RUNTIME_DIR`
/// first -- tmpfs, per user, swept at logout -- then Qt's runtime location,
/// which is the deliberate fallback when no desktop session set it. Empty
/// means nowhere sane: the publisher publishes nowhere and `where` reports
/// nothing rather than guessing.
QString directory();

/// Where scratch backing files live: `<runtime>/omapixel/scratch`, the same
/// runtime base and the same fallback chain as sessions, and empty for the
/// same reason. A scratch file gives an untitled studio window a real
/// address on disk; it is an address, not a save -- tmpfs dies with the
/// session, which is exactly why the window keeps saying unsaved.
QString scratchDirectory();

/// One process's scratch file: `<scratchDirectory>/<pid>.json`, or empty
/// when there is nowhere sane.
QString scratchPath(qint64 pid);

/// Field 22 of `/proc/<pid>/stat`: jiffies since boot this process began.
/// Zero when the process cannot be seen -- dead, or somebody else's.
///
/// Reading /proc is Linux-only. That is accepted, not accidental: the
/// distribution target is Omarchy/Arch.
/// Linux `/proc` start time, measured in clock ticks (jiffies) since boot.
qint64 startTimeOf(qint64 pid);
QString executableOf(qint64 pid);
/// Canonical executable identity expected for Studio discovery. Empty means
/// the running build cannot establish a trusted sibling identity.
QString expectedStudioExecutable();

/// Process-bound Studio IPC. The endpoint is an abstract Unix socket, so there
/// is no mutable filesystem record to impersonate. All calls are Linux-only.
int openPublisher(qint64 pid, QString *error = nullptr);
int acceptPublisher(int server, qint64 *peerPid, uint *peerUid,
                    QString *error = nullptr);
bool sendPublisher(int client, const QByteArray &bytes, QString *error = nullptr);

/// Reads one process-bound publisher snapshot. This is the transport half of
/// `live()` without the Studio executable/path filtering, useful to callers
/// that already own the process identity they are querying.
bool readPublisher(qint64 pid, QByteArray *bytes, QString *error = nullptr);

/// Every session currently alive, the dead ones pruned as a side effect so
/// the directory cleans itself. When `path` is non-empty, only sessions
/// holding that exact document come back.
///
/// A session is alive when its file parses, names a visible process, and
/// records that process's true start time. A recycled PID fails the last
/// test -- the case a name check alone would pass -- and is deleted with the
/// rest.
QList<Entry> live(const QString &path = QString());

} // namespace sessions
} // namespace omapixel
