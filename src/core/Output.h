#pragma once

#include <QByteArray>
#include <QByteArray>
#include <QString>
#include <QStringList>

namespace omapixel {
namespace input {

/// Opens one descriptor, verifies that descriptor is a regular file, and reads
/// no more than maxBytes + 1 bytes so oversized input is bounded.
bool readRegularFile(const QString &path, qint64 maxBytes, QByteArray *bytes,
                     QString *error = nullptr);

} // namespace input

namespace output {

/// Rejects an output that is a symlink or aliases any source path.
bool validate(const QString &path, const QStringList &sources = {},
              QString *error = nullptr);

/// Validates several outputs as one set before any of them is written. No
/// output may alias another output, a source, or a symlink target.
bool validateAll(const QStringList &paths, const QStringList &sources = {},
                 QString *error = nullptr);

/// Validates first, then replaces the output with one complete atomic write.
bool writeAtomically(const QString &path, const QByteArray &bytes,
                     const QStringList &sources = {}, QString *error = nullptr);

} // namespace output
} // namespace omapixel
