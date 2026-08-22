#pragma once

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVariant>

namespace omapixel {
namespace toml {

/// Something wrong with a line, with the line number, so `omapixel config
/// check` can point at it the way a compiler does.
struct Problem
{
    int line = 0;
    QString message;
};

/// A configuration file, flattened.
///
/// `[keys]` followed by `undo = "ctrl+z"` arrives here as `keys.undo`. Flat
/// because everything that reads it wants one value by name, and a tree would
/// mean every caller walking it.
struct Table
{
    QHash<QString, QVariant> values;
    QStringList order;              ///< keys as the file listed them
    QHash<QString, int> lines;      ///< where each key was set
    QList<Problem> problems;

    bool has(const QString &key) const { return values.contains(key); }
    QVariant value(const QString &key) const { return values.value(key); }
};

/// Reads the subset of TOML that a configuration file uses.
///
/// Section headers, `key = value`, comments, and values that are a string, a
/// number, a boolean or an array of those. Not a TOML library: no dotted keys,
/// no inline tables, no multi-line strings, no dates. A config file that needs
/// those is a config file that has stopped being editable by hand, and pulling
/// in a parser for a hundred lines of settings is a dependency bought with
/// somebody else's build time.
///
/// Anything it does not understand becomes a `Problem` rather than an
/// exception or a silent skip -- a setting that does nothing and says nothing
/// is the worst outcome a config file has.
Table read(const QString &text);

} // namespace toml
} // namespace omapixel
