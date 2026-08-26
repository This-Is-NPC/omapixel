#pragma once

#include <QFileSystemWatcher>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

namespace omapixel {

/// The user's settings and keybindings, from `~/.config/omapixel/config.toml`.
///
/// The shape omarchy's own applications use: one commented TOML file per
/// program under `~/.config/<program>/config.toml`, every setting present but
/// commented out at its default, and a `[keys]` table where an action is
/// named on the left and the keys that fire it on the right. herdr, voxtype
/// and cliamp are all configured this way, and a studio that invented a fourth
/// arrangement would be the one window on this desktop you have to learn
/// separately.
///
/// With no file at all the program runs on the defaults below -- the file is
/// how you disagree with them, not how you start.
///
/// Live, like the theme: the file is watched, and saving it re-reads the
/// settings and rebuilds the keymap without a restart. Rebinding a key and
/// then having to relaunch to find out whether you liked it is how a
/// keybinding file goes unedited.
class Config : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString file READ file NOTIFY changed)

    /// The three tables QML binds against, rather than calling a function it
    /// cannot re-evaluate: a Q_INVOKABLE result is not a binding, so a menu
    /// built from one would keep the shortcut the file had at startup. A
    /// property that changes wholesale re-runs everything that reads it, which
    /// is what makes saving the config file redraw the menus.
    Q_PROPERTY(QVariantMap settings READ settingsMap NOTIFY changed)
    Q_PROPERTY(QVariantMap shortcuts READ shortcuts NOTIFY changed)
    Q_PROPERTY(QVariantMap keys READ labels NOTIFY changed)

public:
    explicit Config(QObject *parent = nullptr);

    static constexpr qint64 maxConfigBytes = 1024 * 1024;
    static constexpr int maxHistoryDepth = 256;

    /// The one the process is using. Shared for the same reason the catalogue
    /// is: both front ends read it, and so does anything that grows a setting.
    static Config &shared();

    /// `$OMAPIXEL_CONFIG_PATH`, else `$XDG_CONFIG_HOME/omapixel/config.toml`.
    static QString file();

    /// The annotated default that ships with the program -- the thing
    /// `omapixel --default-config` prints and `omapixel config write` copies.
    /// Empty if the checkout or the install is missing it.
    static QString defaultText();
    static QStringList defaultSearchPath();

    /// Reads the file over the built-in defaults. Safe to call again.
    void load();

    // ---------------------------------------------------------- the settings

    Q_INVOKABLE QVariant value(const QString &key) const;
    Q_INVOKABLE int number(const QString &key) const;
    Q_INVOKABLE double decimal(const QString &key) const;
    Q_INVOKABLE bool flag(const QString &key) const;
    Q_INVOKABLE QString text(const QString &key) const;

    /// Every setting the program reads, with what it falls back to. The
    /// canonical list: `config check` reports anything else in the file as a
    /// key nobody looks at, and the shipped default is checked against this.
    static const QList<QPair<QString, QVariant>> &settings();

    // -------------------------------------------------------------- the keys

    /// Every action that can be bound, with the keys it comes with.
    static const QList<QPair<QString, QString>> &actions();

    /// The action a key press fires, or "" for one that is not bound.
    ///
    /// Matching is exact on the key and the four modifiers first. Failing
    /// that, a binding on punctuation matches with shift held as well: `+` is
    /// shift-and-equals on most layouts and a key of its own on some, and a
    /// user who wrote `zoom_in = "plus"` means the plus key either way.
    Q_INVOKABLE QString action(int key, int modifiers) const;

    /// The first binding of an action as Qt spells it -- "Ctrl+S" -- for a
    /// QML Action's `shortcut`.
    Q_INVOKABLE QString shortcut(const QString &action) const;

    /// The first binding written short, for the hint bar: "^S", "⇧C", "Esc".
    Q_INVOKABLE QString label(const QString &action) const;

    /// Every binding of an action, as written in the file.
    Q_INVOKABLE QStringList bindings(const QString &action) const;

    QVariantMap settingsMap() const;
    QVariantMap shortcuts() const;
    QVariantMap labels() const;

    // ------------------------------------------------------- what went wrong

    /// Everything the file got wrong, in the order it got it wrong: a bad
    /// value, a key nobody reads, a binding that names no key, two actions on
    /// one key. What `omapixel config check` prints.
    QStringList problems() const { return m_problems; }

    /// A key press, parsed. -1 for a name that is not a key.
    static int parseKey(const QString &binding, int *modifiers);

    /// The reverse: Qt's own spelling, for a menu.
    static QString spell(int key, int modifiers);

signals:
    void changed();

private:
    void watch();
    void bind(const QString &action, const QVariant &value, int line);

    struct Combination
    {
        int key = 0;
        int modifiers = 0;
        bool operator==(const Combination &other) const
        {
            return key == other.key && modifiers == other.modifiers;
        }
    };

    QHash<QString, QVariant> m_values;
    QHash<QString, QList<Combination>> m_bound;   ///< action -> keys
    QHash<QString, QStringList> m_written;        ///< action -> keys as written
    QStringList m_problems;
    QFileSystemWatcher m_watcher;
};

} // namespace omapixel
