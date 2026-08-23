#include "Sessions.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace omapixel {
namespace sessions {

namespace {

/// The runtime base both families of files hang off: `XDG_RUNTIME_DIR`
/// first, then Qt's runtime location, which falls back rather than
/// returning nothing. Empty means nowhere sane.
QString runtimeBase()
{
    QString base = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    return base;
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
    return base + QStringLiteral("/omapixel/sessions");
}

QString scratchDirectory()
{
    const QString base = runtimeBase();
    if (base.isEmpty())
        return QString();
    return base + QStringLiteral("/omapixel/scratch");
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

QList<Entry> live(const QString &path)
{
    QList<Entry> found;
    const QString dir = directory();
    if (dir.isEmpty())
        return found;

    QDir where(dir);
    const QStringList files = where.entryList({QStringLiteral("*.json")},
                                              QDir::Files, QDir::Name);
    for (const QString &name : files) {
        QFile file(where.filePath(name));
        if (!file.open(QIODevice::ReadOnly)) {
            file.remove();
            continue;
        }
        const QJsonObject session =
            QJsonDocument::fromJson(file.readAll()).object();
        if (!session.contains(QStringLiteral("selection"))) {
            file.remove();
            continue;
        }
        const qint64 pid = qint64(session.value(QStringLiteral("pid")).toDouble());
        const qint64 started =
            qint64(session.value(QStringLiteral("started")).toDouble());

        qint64 actual = 0;
        const Liveness alive = pid > 0 && started > 0
                                   ? livenessOf(pid, &actual)
                                   : Liveness::Dead;
        if (alive == Liveness::Dead
            || (alive == Liveness::Alive && actual != started)) {
            // Dead, or recycled -- the impostor case a name check alone
            // would pass. Either way the file lies, and a lying file cleans
            // itself out of the way. UNREADABLE is different: not evidence,
            // so the file stays and the session is simply not reported.
            file.remove();
            continue;
        }
        if (alive == Liveness::Unknown)
            continue;

        Entry entry;
        entry.pid = pid;
        entry.started = started;
        entry.path = session.value(QStringLiteral("path")).toString();
        entry.dirty = session.value(QStringLiteral("dirty")).toBool();
        const QJsonValue selected = session.value(QStringLiteral("selection"));
        if (!selected.isNull()) {
            const QJsonObject selection = selected.toObject();
            entry.clip = selection.value(QStringLiteral("clip")).toString();
            entry.frame = selection.value(QStringLiteral("frame")).toInt(-1);
            const int x = selection.value(QStringLiteral("x")).toInt(-1);
            const int y = selection.value(QStringLiteral("y")).toInt(-1);
            const int width = selection.value(QStringLiteral("width")).toInt();
            const int height = selection.value(QStringLiteral("height")).toInt();
            if (entry.clip.isEmpty() || entry.frame < 0 || x < 0 || y < 0
                || width <= 0 || height <= 0) {
                file.remove();
                continue;
            }
            entry.selection = QRect(x, y, width, height);
        }
        if (!path.isEmpty() && entry.path != path)
            continue;
        found.append(entry);
    }
    return found;
}

} // namespace sessions
} // namespace omapixel
