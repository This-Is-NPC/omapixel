#pragma once

#include <QColor>
#include <QFileSystemWatcher>
#include <QObject>

class QProcess;

namespace omapixel {

/// The omarchy theme, followed live.
///
/// omarchy keeps the active theme as a symlink at
/// `$XDG_STATE_HOME/omarchy/current/theme`, and the colours in a `colors.toml`
/// inside it. The shell reads that same file, so a studio that reads it too
/// changes colour at the same instant everything else on the desktop does --
/// including when `omarchy theme set` swaps it while the window is open.
///
/// The roles are omarchy's own vocabulary -- background, foreground, accent,
/// urgent, muted -- rather than a set invented here. Naming them differently
/// would mean deciding, in this file, what a theme author meant, which is
/// exactly the decision that belongs to the theme.
///
/// Nothing here touches the DOCUMENT's palette. The art must not change colour
/// because somebody switched desktop themes, and the window must not change
/// because somebody recoloured a character.
class Theme : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(bool dark READ dark NOTIFY changed)

    /// Hyprland's `decoration:rounding`. Some omarchy themes round their
    /// corners and most leave them square; the number lives in the theme's
    /// `hyprland.lua`, not in `colors.toml`, and the user's own Hyprland config
    /// overrides it. Used verbatim as the radius of every control, which is
    /// what the omarchy shell does -- a studio with rounded buttons on a square
    /// desktop is the one window that looks like it came from somewhere else.
    Q_PROPERTY(int rounding READ rounding NOTIFY changed)

    // The five roles, straight from the theme.
    Q_PROPERTY(QColor background READ background NOTIFY changed)
    Q_PROPERTY(QColor foreground READ foreground NOTIFY changed)
    Q_PROPERTY(QColor accent READ accent NOTIFY changed)
    Q_PROPERTY(QColor urgent READ urgent NOTIFY changed)
    Q_PROPERTY(QColor muted READ muted NOTIFY changed)

    // Derived surfaces, so the QML never does colour arithmetic of its own.
    Q_PROPERTY(QColor panel READ panel NOTIFY changed)
    Q_PROPERTY(QColor sunken READ sunken NOTIFY changed)
    Q_PROPERTY(QColor line READ line NOTIFY changed)
    Q_PROPERTY(QColor dim READ dim NOTIFY changed)
    Q_PROPERTY(QColor onAccent READ onAccent NOTIFY changed)

    // The chequerboard behind transparency, tuned to the theme rather than
    // fixed: a light theme with a dark chequer reads as a hole in the drawing.
    Q_PROPERTY(QColor checkerDark READ checkerDark NOTIFY changed)
    Q_PROPERTY(QColor checkerLight READ checkerLight NOTIFY changed)

    Q_PROPERTY(QString fontFamily READ fontFamily CONSTANT)

public:
    explicit Theme(QObject *parent = nullptr);
    ~Theme() override;

    QString name() const { return m_name; }
    bool dark() const { return m_dark; }
    int rounding() const { return m_rounding; }

    /// The `int` out of `hyprctl -j getoption decoration:rounding`, or -1 if
    /// the output is not something we recognise. Separated from the process so
    /// it can be tested without a compositor.
    static int parseRounding(const QByteArray &json);

    QColor background() const { return m_background; }
    QColor foreground() const { return m_foreground; }
    QColor accent() const { return m_accent; }
    QColor urgent() const { return m_urgent; }
    QColor muted() const { return m_muted; }

    QColor panel() const;
    QColor sunken() const;
    QColor line() const;
    QColor dim() const;
    QColor onAccent() const;
    QColor checkerDark() const;
    QColor checkerLight() const;

    QString fontFamily() const { return QStringLiteral("monospace"); }

    /// omarchy's own state fills: a translucent wash of a role over the
    /// surface, rather than a second opaque colour. Exposed as functions so QML
    /// asks for the state it is in instead of hard-coding an alpha.
    Q_INVOKABLE QColor fill(const QColor &role, qreal alpha) const;

signals:
    void changed();

private:
    void reload();
    void watch();
    void queryRounding();

    QFileSystemWatcher m_watcher;
    QProcess *m_hyprctl = nullptr;
    QString m_themePath;
    QString m_name;
    int m_rounding = 0;
    bool m_dark = true;
    QColor m_background{"#16161e"};
    QColor m_foreground{"#c0caf5"};
    QColor m_accent{"#7aa2f7"};
    QColor m_urgent{"#f7768e"};
    QColor m_muted{"#565f89"};
};

} // namespace omapixel
