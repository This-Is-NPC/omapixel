#pragma once

#include <QHash>
#include <QObject>
#include <QString>

namespace omapixel {

/// The window's words, from a file.
///
/// A key-and-value JSON catalogue per language, read at startup. Not Qt's own
/// .ts/.qm machinery, deliberately: that needs lupdate and lrelease to produce
/// a binary nobody can read or edit, and the ask here was a file a person can
/// copy, translate and drop in without building anything.
///
/// Keys look like `menu.file.open`. The English catalogue is the canonical list
/// of every string the window can say, so translating is: copy en.json, change
/// the right-hand sides.
///
/// Lookup falls back rather than failing: the chosen language, then English,
/// then the key itself. A half-translated catalogue therefore shows English
/// where it is thin, and a typo in a key shows the key -- visible, and
/// obviously wrong, which is what you want from a mistake.
class Strings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString language READ language CONSTANT)

public:
    explicit Strings(QObject *parent = nullptr);

    /// The catalogue the process is using.
    ///
    /// A shared one because the model has things to say too -- "saved to x",
    /// "the palette is full" -- and threading a pointer through everything
    /// that might one day speak is more ceremony than one catalogue is worth.
    static Strings &shared();

    /// Loads English, then overlays `language` on top of it.
    ///
    /// `language` is a tag like `pt_BR`. It is tried whole and then by its
    /// first part, so a `pt.json` serves a `pt_BR` system until somebody
    /// writes a Brazilian one.
    void load(const QString &language);

    QString language() const { return m_language; }

    /// The string for a key.
    Q_INVOKABLE QString t(const QString &key) const;

    /// Every key the catalogues know, sorted. For the tooling that checks a
    /// translation against the English list.
    Q_INVOKABLE QStringList keys() const;

    /// Where catalogues are looked for, in order. Exposed so `omapixel-studio
    /// --languages` can print it: "it did not pick up my file" is otherwise a
    /// question nobody can answer.
    static QStringList searchPath();

    /// The catalogue directory in a checkout -- where a person editing
    /// translations actually works. Compiled in here so the rest of the
    /// project does not need the build define.
    static QString catalogueDir();

    /// The language to use when nothing was asked for: OMAPIXEL_LANG, else the
    /// system's.
    static QString preferredLanguage();

private:
    bool merge(const QString &language);

    QHash<QString, QString> m_strings;
    QString m_language = QStringLiteral("en");
};

} // namespace omapixel
