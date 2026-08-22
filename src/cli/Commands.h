#pragma once

#include "Document.h"

#include <QString>
#include <QStringList>

class QCommandLineParser;

namespace omapixel {
namespace cli {

/// What one command produced.
///
/// A CLI an agent drives has to fail with a sentence and an exit code, never
/// with a dialog and never silently. `output` is what belongs on stdout and
/// `error` what belongs on stderr, kept apart so a batch can decide where each
/// line goes rather than having them printed out from under it.
struct Outcome {
    int code = 0;
    bool changed = false;
    QString output;
    QString error;

    static Outcome ok(const QString &output = QString())
    {
        return {0, false, output, QString()};
    }
    /// It ran and the answer was no.
    static Outcome refused(const QString &error) { return {1, false, QString(), error}; }
    /// The command itself was wrong -- a missing flag, an unknown sub-command.
    static Outcome wrong(const QString &error) { return {2, false, QString(), error}; }
    static Outcome edited(const QString &output = QString())
    {
        return {0, true, output, QString()};
    }
};

/// Declares every option, once, for every command.
///
/// A per-command parser reads better in isolation and worse in use: the same
/// idea would end up spelled `--frame` here and `--at` there. Options are
/// looked up by name, so nothing has to carry the option objects around.
void addOptions(QCommandLineParser &parser);

/// Runs one command against an already-loaded document.
///
/// This is every command that works on the open document and nothing else --
/// which is what makes `batch` possible, and what keeps a single command and a
/// batched one from drifting apart. Commands that touch a second file
/// (`new`, `import`, `export`, `render`, `diff`) stay in main, because a batch
/// operating on one document has no business writing others.
///
/// `words` is the command's positional arguments with the command and the file
/// already taken off the front.
Outcome applyCommand(Document &doc, const QString &command, QStringList words,
                     const QCommandLineParser &parser);

/// True if `applyCommand` knows this command. Used to tell "not in a batch"
/// from "not a command at all", which are different mistakes.
bool isDocumentCommand(const QString &command);

/// Describes every structural difference between two documents.
///
/// The labels are included in the frame diagnostics so callers comparing files
/// can say which side owns a value without duplicating the comparison logic.
QStringList documentDifferences(const Document &left, const Document &right,
                               const QString &leftLabel = QStringLiteral("left"),
                               const QString &rightLabel = QStringLiteral("right"));

} // namespace cli
} // namespace omapixel
