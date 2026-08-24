#include "Output.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <sys/stat.h>
#include <unistd.h>

namespace omapixel {
namespace output {
namespace {

QString comparisonPath(const QString &path)
{
    const QFileInfo info(path);
    if (info.exists())
        return info.canonicalFilePath();
    const QFileInfo parent(info.absolutePath());
    const QString canonicalParent = parent.canonicalFilePath();
    return QDir(canonicalParent.isEmpty() ? parent.absoluteFilePath() : canonicalParent)
        .filePath(info.fileName());
}

bool fail(QString *error, const QString &path, const QString &message)
{
    if (error)
        *error = QStringLiteral("%1: %2").arg(path, message);
    return false;
}

} // namespace

} // namespace output

namespace input {

namespace {

class ScopedFd
{
public:
    explicit ScopedFd(int fd) : value(fd) {}
    ~ScopedFd()
    {
        if (value >= 0)
            ::close(value);
    }

    int get() const { return value; }

private:
    int value;
};

QString systemError()
{
    return QString::fromLocal8Bit(std::strerror(errno));
}

} // namespace

bool readRegularFile(const QString &path, qint64 maxBytes, QByteArray *bytes,
                     QString *error)
{
    if (bytes)
        bytes->clear();
    if (path.isEmpty()) {
        if (error)
            *error = QStringLiteral("input path is empty");
        return false;
    }
    if (maxBytes < 0 || maxBytes >= std::numeric_limits<int>::max()) {
        if (error)
            *error = QStringLiteral("%1: invalid input limit").arg(path);
        return false;
    }

    const int fd = ::open(QFile::encodeName(path).constData(),
                          O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0) {
        if (error) {
            if (errno == ELOOP)
                *error = QStringLiteral("%1: input is not a regular file").arg(path);
            else
                *error = QStringLiteral("%1: %2").arg(path, systemError());
        }
        return false;
    }
    const ScopedFd descriptor(fd);

    struct stat status;
    if (::fstat(descriptor.get(), &status) != 0) {
        if (error)
            *error = QStringLiteral("%1: %2").arg(path, systemError());
        return false;
    }
    if (!S_ISREG(status.st_mode)) {
        if (error)
            *error = QStringLiteral("%1: input is not a regular file").arg(path);
        return false;
    }

    QByteArray result;
    result.reserve(qMin<qint64>(maxBytes + 1, 64 * 1024));
    char buffer[64 * 1024];
    qint64 total = 0;
    while (total <= maxBytes) {
        const qint64 remaining = maxBytes + 1 - total;
        const size_t requested = size_t(qMin<qint64>(remaining, sizeof(buffer)));
        const ssize_t count = ::read(descriptor.get(), buffer, requested);
        if (count > 0) {
            result.append(buffer, int(count));
            total += count;
            continue;
        }
        if (count == 0)
            break;
        if (errno == EINTR)
            continue;
        if (error)
            *error = QStringLiteral("%1: %2").arg(path, systemError());
        return false;
    }
    if (bytes)
        *bytes = result;
    return true;
}

} // namespace input

namespace output {

bool validate(const QString &path, const QStringList &sources, QString *error)
{
    if (path.isEmpty())
        return fail(error, path, QStringLiteral("output path is empty"));
    const QFileInfo output(path);
    if (output.isSymLink())
        return fail(error, path, QStringLiteral("refusing to write through a symlink"));
    if (output.exists() && !output.isFile())
        return fail(error, path, QStringLiteral("output is not a regular file"));

    const QString outputPath = comparisonPath(path);
    if (outputPath.isEmpty())
        return fail(error, path, QStringLiteral("could not resolve output path"));
    for (const QString &source : sources) {
        if (source.isEmpty())
            continue;
        const QString sourcePath = comparisonPath(source);
        if (!sourcePath.isEmpty() && sourcePath == outputPath)
            return fail(error, path, QStringLiteral("output must differ from source"));
    }
    return true;
}

bool validateAll(const QStringList &paths, const QStringList &sources, QString *error)
{
    QStringList seen;
    for (const QString &path : paths) {
        if (path.isEmpty())
            continue;
        if (!validate(path, sources, error))
            return false;
        const QString resolved = comparisonPath(path);
        if (seen.contains(resolved))
            return fail(error, path, QStringLiteral("output aliases another output"));
        seen.append(resolved);
    }
    return true;
}

bool writeAtomically(const QString &path, const QByteArray &bytes,
                     const QStringList &sources, QString *error)
{
    if (!validateAll({path}, sources, error))
        return false;
    QSaveFile file(path);
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly))
        return fail(error, path, file.errorString());
    if (file.write(bytes) != bytes.size()) {
        const QString reason = file.errorString();
        file.cancelWriting();
        return fail(error, path, reason);
    }
    // Recheck immediately before the rename. QSaveFile keeps the write atomic,
    // while this closes the ordinary validation/write gap for aliases and links.
    if (!validateAll({path}, sources, error)) {
        file.cancelWriting();
        return false;
    }
    if (!file.commit())
        return fail(error, path, file.errorString());
    return true;
}

} // namespace output
} // namespace omapixel
