#pragma once

#include "Document.h"

#include <QByteArray>
#include <QString>

namespace omapixel {

/// Reads and writes the document format.
///
/// The format:
///
///   {
///     "size":    { "w": 32, "h": 24 },
///     "palette": [ { "slot": "I", "colour": "#1A1B26" }, ... ],
///     "clips":   [ { "name": "idle", "fps": 8, "frames": [ [ "....", ... ] ] } ]
///   }
///
/// Palette and clips are ARRAYS, and that is the one thing worth explaining.
/// The obvious shape is an object keyed by slot and by clip name, and the
/// Python version used exactly that. Qt's QJsonObject sorts its keys, so
/// round-tripping a document through it silently reorders the palette -- and
/// the palette's order is the order the swatch strip draws in, which is content
/// rather than presentation. Arrays keep the order without a hand-written
/// parser, which is the other way this could have gone and a far worse one.
///
/// Documents written by the Python version, with objects, still open: `read`
/// accepts both shapes and `write` always emits the array one.
class Codec
{
public:
    struct Result {
        Document document;
        bool ok = false;
        QString error;

        explicit operator bool() const { return ok; }
    };

    static Result read(const QByteArray &json);
    static Result readFile(const QString &path);

    static QByteArray write(const Document &document);

    /// Writes through a temporary and renames over the target. The studio
    /// watches the file it has open, and it must not read half a document
    /// because a CLI command is halfway through a write.
    static bool writeFile(const QString &path, const Document &document,
                          QString *error = nullptr);
};

} // namespace omapixel
