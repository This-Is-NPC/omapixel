#pragma once

#include "Document.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace omapixel {

/// The translation between this format and omagotchi's catalog.
///
/// It lives here, in one place, and nothing else in the program knows that
/// omagotchi exists. A studio that spreads a consumer's ideas -- species,
/// variants, agent states -- through its model is a studio that serves one
/// program. Another consumer tomorrow gets another class beside this one, and
/// neither the core nor the format changes.
class Bridge
{
public:
    struct Result {
        Document document;
        QJsonObject catalog;
        QStringList skipped;
        bool ok = false;
        QString error;

        explicit operator bool() const { return ok; }
    };

    /// One species and variant of a catalog -> clips. Each state becomes a
    /// clip, and so do the sequences that live outside `fat`.
    static Result importSpecies(const QJsonObject &catalog, const QString &species,
                                const QString &variant);

    /// Clips -> the bank they came from, in place. Only touches what the
    /// document brings, and refuses to invent a sequence the catalog does not
    /// know: a new key there produces art that nothing ever draws, and the
    /// author only finds out when the companion fails to appear.
    static Result exportInto(QJsonObject catalog, const Document &document,
                             const QString &species, const QString &variant);

private:
    /// The sequences that do not live under `fat`. `spawn` and `boom` are a
    /// bare list of frames; the other three are keyed by variant.
    static QStringList flatSequences();
    static QStringList perVariantSequences();
};

} // namespace omapixel
