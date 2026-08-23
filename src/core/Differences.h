#pragma once

#include "Document.h"

#include <QString>
#include <QStringList>

namespace omapixel {

/// Describes every structural difference between two documents.
///
/// The CLI's `diff` prints these lines and the studio reports them when a file
/// changes behind its back -- which is why this lives in core and not beside
/// either front end. Two descriptions of one difference are two chances to be
/// wrong; the model states it once and both obey.
///
/// The labels name each side inside the sentences, so a caller comparing files
/// can say which side owns a value without duplicating the walk.
///
/// The lines come from the language catalogue (`diff.*` in i18n/en.json):
/// they reach a person through the studio's status bar, and text shown to a
/// person is not a place for a second hardcoded copy of English. The CLI has
/// the catalogues loaded too, so both front ends print the same thing.
QStringList documentDifferences(const Document &left, const Document &right,
                                const QString &leftLabel = QStringLiteral("left"),
                                const QString &rightLabel = QStringLiteral("right"));

/// A walk fit for one line: the first `max` sentences, then a counted tail
/// for the rest. One summarizer for both front ends -- a sentence claiming
/// "+2 more" behind it has to mean it everywhere it is shown.
QStringList summarizeDifferences(const QStringList &lines, int max = 3);

} // namespace omapixel
