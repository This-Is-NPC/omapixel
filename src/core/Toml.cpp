#include "Toml.h"

namespace omapixel {
namespace toml {
namespace {

/// Cuts a `#` comment off the end of a line, respecting quotes.
///
/// A value may contain one -- `accent = "#7aa2f7"` is the obvious case, and it
/// is exactly the kind of value a theme-following program has -- so the hash
/// only starts a comment outside a string.
QString withoutComment(const QString &line)
{
    QChar quote;
    for (int i = 0; i < line.size(); ++i) {
        const QChar ch = line.at(i);
        if (quote.isNull()) {
            if (ch == QLatin1Char('"') || ch == QLatin1Char('\''))
                quote = ch;
            else if (ch == QLatin1Char('#'))
                return line.left(i);
        } else if (ch == quote) {
            quote = QChar();
        }
    }
    return line;
}

/// One scalar. Returns an invalid QVariant for anything unrecognised, which is
/// what turns into a Problem upstairs.
QVariant scalar(const QString &raw)
{
    const QString text = raw.trimmed();
    if (text.isEmpty())
        return {};

    // A quoted string, basic or literal. Both are read verbatim: escapes are
    // TOML's, and nothing in this file's vocabulary -- key names, colours,
    // paths -- needs one.
    if (text.size() >= 2
        && ((text.startsWith(QLatin1Char('"')) && text.endsWith(QLatin1Char('"')))
            || (text.startsWith(QLatin1Char('\'')) && text.endsWith(QLatin1Char('\''))))) {
        return text.mid(1, text.size() - 2);
    }

    if (text == QLatin1String("true"))
        return true;
    if (text == QLatin1String("false"))
        return false;

    bool whole = false;
    const qlonglong number = text.toLongLong(&whole);
    if (whole)
        return number;

    bool fractional = false;
    const double real = text.toDouble(&fractional);
    if (fractional)
        return real;

    return {};
}

/// Splits `[a, b, c]` on the commas that are not inside a string.
QStringList items(const QString &inside)
{
    QStringList out;
    QString current;
    QChar quote;
    for (const QChar ch : inside) {
        if (!quote.isNull()) {
            if (ch == quote)
                quote = QChar();
            current.append(ch);
            continue;
        }
        if (ch == QLatin1Char('"') || ch == QLatin1Char('\'')) {
            quote = ch;
            current.append(ch);
            continue;
        }
        if (ch == QLatin1Char(',')) {
            out.append(current);
            current.clear();
            continue;
        }
        current.append(ch);
    }
    if (!current.trimmed().isEmpty())
        out.append(current);
    return out;
}

} // namespace

Table read(const QString &text)
{
    Table table;
    QString section;

    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int number = 0; number < lines.size(); ++number) {
        const QString line = withoutComment(lines.at(number)).trimmed();
        if (line.isEmpty())
            continue;

        if (line.startsWith(QLatin1Char('['))) {
            if (!line.endsWith(QLatin1Char(']'))) {
                table.problems.append({number + 1,
                                       QStringLiteral("a section header with no closing ]")});
                continue;
            }
            section = line.mid(1, line.size() - 2).trimmed();
            // `[[thing]]` is an array of tables, which this subset does not do.
            if (section.startsWith(QLatin1Char('['))) {
                table.problems.append({number + 1,
                                       QStringLiteral("arrays of tables are not read here")});
                section.clear();
            }
            continue;
        }

        const int equals = line.indexOf(QLatin1Char('='));
        if (equals < 0) {
            table.problems.append({number + 1, QStringLiteral("not a setting: ") + line});
            continue;
        }

        const QString name = line.left(equals).trimmed();
        const QString raw = line.mid(equals + 1).trimmed();
        if (name.isEmpty()) {
            table.problems.append({number + 1, QStringLiteral("a value with no name")});
            continue;
        }
        const QString key = section.isEmpty() ? name : section + QLatin1Char('.') + name;

        QVariant value;
        if (raw.startsWith(QLatin1Char('[')) && raw.endsWith(QLatin1Char(']'))) {
            QVariantList list;
            bool sound = true;
            const QStringList parts = items(raw.mid(1, raw.size() - 2));
            for (const QString &part : parts) {
                const QVariant one = scalar(part);
                if (!one.isValid()) {
                    table.problems.append({number + 1,
                                           key + QStringLiteral(": ") + part.trimmed()
                                               + QStringLiteral(" is not a value")});
                    sound = false;
                    break;
                }
                list.append(one);
            }
            if (!sound)
                continue;
            value = list;
        } else {
            value = scalar(raw);
            if (!value.isValid()) {
                table.problems.append(
                    {number + 1,
                     key + QStringLiteral(": ") + raw
                         + QStringLiteral(" is not a value — strings need quotes")});
                continue;
            }
        }

        if (!table.values.contains(key))
            table.order.append(key);
        table.values.insert(key, value);
        table.lines.insert(key, number + 1);
    }

    return table;
}

} // namespace toml
} // namespace omapixel
