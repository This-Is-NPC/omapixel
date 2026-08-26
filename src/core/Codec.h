#pragma once

#include "Document.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

namespace omapixel {

/// Reads and writes the document format.
///
/// The strict v2 format:
///
///   {
///     "version": 2,
///     "canvas":  { "width": 32, "height": 24 },
///     "palette": [ { "slot": "I", "colour": "#1A1B26FF" }, ... ],
///     "clips":   [ { "id": "idle", "name": "Idle", "fps": 8,
///                    "frameCount": 1 } ],
///     "layers":  [ { "id": "layer", "name": "Layer", ...,
///                    "storage": "shared", "cels": [ ... ] } ]
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
/// v1 documents are intentionally rejected. `write` emits stable arrays and
/// the complete layer metadata in the order held by the model.
class Codec
{
public:
    struct WarningLimits {
        qint64 fileBytes = 16 * 1024 * 1024;
        int clips = 256;
        int framesPerClip = 1024;
        int totalFrames = 4096;
        int paletteSlots = Document::maxPaletteSlots;
    };

    struct Result {
        Document document;
        bool ok = false;
        QString error;
        QStringList warnings;

        explicit operator bool() const { return ok; }
    };

    static Result read(const QByteArray &json);
    static Result read(const QByteArray &json, const WarningLimits &limits);
    /// Reads JSON or a Zstandard-compressed Omapixel document. Compressed input
    /// is detected by its frame magic rather than trusting the file suffix.
    static Result readFile(const QString &path);
    static Result readFile(const QString &path, const WarningLimits &limits);

    /// Rejects malformed JSON and duplicate object keys before Qt normalizes
    /// them into a QJsonObject. Returns true when the input is rejected.
    static bool rejectDuplicateJsonKeys(const QByteArray &json,
                                        QString *error = nullptr);

    static QByteArray write(const Document &document, QString *error = nullptr);

    /// Writes JSON by default and compact JSON in a Zstandard frame when the
    /// target suffix is `.omapixel`. Writes through a temporary and renames
    /// over the target. The studio
    /// watches the file it has open, and the atomic rename means it never
    /// reads half a document because a CLI command is halfway through a write.
    static bool writeFile(const QString &path, const Document &document,
                          QString *error = nullptr);
    static bool writeFile(const QString &path, const Document &document,
                          const QStringList &sources, QString *error = nullptr);
};

} // namespace omapixel
