#pragma once

#include "Document.h"

#include <QString>
#include <QStringList>

namespace omapixel {

/// Consequences shared by merge-down and flatten-visible.
struct LayerOperationReport {
    qint64 frames = 0;
    qint64 affectedPixels = 0;
    qint64 exactMatches = 0;
    qint64 approximatedPixels = 0;
    qint64 newSlots = 0;
    qint64 removedLayers = 0;
    QStringList diagnostics;

    bool hasPaletteLoss() const { return approximatedPixels > 0; }
};

struct LayerOperationResult {
    bool ok = false;
    Document document;
    LayerOperationReport report;
    QString error;

    explicit operator bool() const { return ok; }
};

/// Stages the layer immediately below `sourceLayer` and returns a new document.
/// The input is never changed. A hidden source or locked participant refuses
/// the operation before any result is committed.
LayerOperationResult previewMergeDown(const Document &source,
                                      const QString &sourceLayer);

/// Applies the same staged operation atomically to `document`.
bool applyMergeDown(Document *document, const QString &sourceLayer,
                    LayerOperationReport *report = nullptr,
                    QString *error = nullptr);

/// Produces a separate document containing only the visible composite in one
/// normal, fully opaque layer. Hidden layers are deliberately not copied.
LayerOperationResult flattenVisible(const Document &source, bool anyway = false);

/// Alias named for callers that make the non-mutating preview step explicit.
LayerOperationResult previewFlattenVisible(const Document &source, bool anyway = false);

/// Applies a staged flatten to the supplied document. The source is unchanged
/// when this returns false.
bool applyFlattenVisible(Document *document,
                         LayerOperationReport *report = nullptr,
                         QString *error = nullptr);

} // namespace omapixel
