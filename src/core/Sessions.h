#pragma once

#include <QList>
#include <QRect>
#include <QString>

namespace omapixel {
namespace sessions {

/// What one running studio has said about itself. `selection` is invalid when
/// the session JSON carries the required null selection.
struct Entry {
    qint64 pid = 0;
    qint64 started = 0;
    QString path;
    bool dirty = false;
    QString clip;
    int frame = -1;
    QRect selection;
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
qint64 startTimeOf(qint64 pid);

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
