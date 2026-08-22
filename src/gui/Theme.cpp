#include "Theme.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTextStream>
#include <QTimer>

namespace omapixel {

Theme::~Theme()
{
    if (!m_hyprctl || m_hyprctl->state() == QProcess::NotRunning)
        return;
    m_hyprctl->terminate();
    if (!m_hyprctl->waitForFinished(100)) {
        m_hyprctl->kill();
        m_hyprctl->waitForFinished(100);
    }
}
namespace {

/// Where omarchy keeps the pointer to the active theme.
QString currentThemePath()
{
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    QString state = env.value(QStringLiteral("XDG_STATE_HOME"));
    if (state.isEmpty())
        state = QDir::homePath() + QStringLiteral("/.local/state");
    return state + QStringLiteral("/omarchy/current/theme");
}

/// Enough TOML for `colors.toml`, which is flat `key = "value"` lines and
/// nothing else. Pulling in a TOML library to read seventeen strings would be
/// a dependency for the sake of the shape of the file rather than its content;
/// if the file ever grows tables, this comment is the place that gets deleted.
QHash<QString, QString> readFlatToml(const QString &path)
{
    QHash<QString, QString> values;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return values;

    QTextStream stream(&file);
    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))
            || line.startsWith(QLatin1Char('[')))
            continue;
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals < 0)
            continue;
        const QString key = line.left(equals).trimmed();
        QString value = line.mid(equals + 1).trimmed();
        if (value.startsWith(QLatin1Char('"')) && value.endsWith(QLatin1Char('"')))
            value = value.mid(1, value.size() - 2);
        values.insert(key, value);
    }
    return values;
}

QColor colourOr(const QHash<QString, QString> &values, const QString &key,
                const QColor &fallback)
{
    const QColor parsed(values.value(key));
    return parsed.isValid() ? parsed : fallback;
}

/// `amount` of `toward` mixed into `from`.
///
/// The derived surfaces use this rather than QColor::lighter/darker, which
/// scale the HSV value: on a background of #0c0b0c, lighter(135) is still
/// #101010, so a surface built that way is invisible on exactly the dark themes
/// omarchy ships most of. Mixing moves a fixed distance regardless of where the
/// background starts.
QColor mix(const QColor &from, const QColor &toward, qreal amount)
{
    return QColor::fromRgbF(from.redF() + (toward.redF() - from.redF()) * amount,
                            from.greenF() + (toward.greenF() - from.greenF()) * amount,
                            from.blueF() + (toward.blueF() - from.blueF()) * amount);
}

} // namespace

Theme::Theme(QObject *parent) : QObject(parent), m_themePath(currentThemePath())
{
    reload();
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { reload(); });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] {
        // The symlink was repointed: what we were watching is now a file in the
        // old theme. Re-arm on the new target before reading.
        watch();
        reload();
    });
    watch();
    queryRounding();
}

int Theme::parseRounding(const QByteArray &json)
{
    const QJsonObject option = QJsonDocument::fromJson(json).object();
    const QJsonValue value = option.value(QStringLiteral("int"));
    if (!value.isDouble())
        return -1;
    const int rounding = value.toInt(-1);
    return rounding >= 0 ? rounding : -1;
}

void Theme::queryRounding()
{
    if (!m_hyprctl) {
        m_hyprctl = new QProcess(this);
        connect(m_hyprctl, &QProcess::finished, this, [this] {
            const int next = parseRounding(m_hyprctl->readAllStandardOutput());
            if (next < 0 || next == m_rounding)
                return;
            m_rounding = next;
            emit changed();
        });
        // Not running Hyprland, or no hyprctl on PATH: the corners stay square,
        // which is what every omarchy theme but one asks for anyway.
        connect(m_hyprctl, &QProcess::errorOccurred, this, [] {});
    }
    if (m_hyprctl->state() != QProcess::NotRunning)
        return;
    m_hyprctl->start(QStringLiteral("hyprctl"),
                     {QStringLiteral("-j"), QStringLiteral("getoption"),
                      QStringLiteral("decoration:rounding")});
}

void Theme::watch()
{
    if (!m_watcher.files().isEmpty())
        m_watcher.removePaths(m_watcher.files());
    if (!m_watcher.directories().isEmpty())
        m_watcher.removePaths(m_watcher.directories());

    const QString colours = m_themePath + QStringLiteral("/colors.toml");
    if (QFile::exists(colours))
        m_watcher.addPath(colours);
    // The parent of the symlink, so a theme swap is seen even though the
    // symlink itself never "changes" as a file.
    const QFileInfo pointer(m_themePath);
    if (pointer.dir().exists())
        m_watcher.addPath(pointer.absolutePath());
}

void Theme::reload()
{
    const QHash<QString, QString> values =
        readFlatToml(m_themePath + QStringLiteral("/colors.toml"));
    if (values.isEmpty())
        return;

    m_name = QFileInfo(QFile::symLinkTarget(m_themePath).isEmpty()
                           ? m_themePath
                           : QFile::symLinkTarget(m_themePath))
                 .fileName();
    m_dark = values.value(QStringLiteral("mode"), QStringLiteral("dark"))
             != QLatin1String("light");

    m_background = colourOr(values, QStringLiteral("background"), m_background);
    m_foreground = colourOr(values, QStringLiteral("foreground"), m_foreground);
    m_accent = colourOr(values, QStringLiteral("accent"), m_accent);
    // omarchy has no `urgent` key in colors.toml; the shell resolves it from
    // the theme's red, which is what a theme author tunes for alarm.
    m_urgent = colourOr(values, QStringLiteral("red"), m_urgent);
    m_muted = colourOr(values, QStringLiteral("muted"),
                       colourOr(values, QStringLiteral("dark_foreground"), m_muted));

    emit changed();

    // A theme can change the rounding too, but through Hyprland rather than
    // through this file. Asked twice: once now, and once after Hyprland has had
    // a moment to apply the theme's `hyprland.lua`, because the colours land
    // first and asking only now reads the outgoing theme's corners.
    queryRounding();
    QTimer::singleShot(700, this, [this] { queryRounding(); });
}

// The derived surfaces. They are computed from the five roles rather than read
// from more keys, so a theme that only defines the basics still produces a
// coherent window -- and so a light theme lightens where a dark one darkens.

// Mixing the foreground into the background separates the rails from the page
// in either mode: a dark theme has a pale foreground, so the rails lift; a light
// theme has a dark one, so they settle. One expression, no branch on mode.
QColor Theme::panel() const
{
    return mix(m_background, m_foreground, 0.05);
}

QColor Theme::sunken() const
{
    return m_dark ? m_background.darker(130) : m_background.darker(112);
}

QColor Theme::line() const
{
    QColor edge = m_foreground;
    edge.setAlphaF(0.18);
    return edge;
}

QColor Theme::dim() const
{
    return m_muted.isValid() ? m_muted : m_foreground.darker(160);
}

QColor Theme::onAccent() const
{
    // Text sitting on the accent. Picked by the accent's own lightness rather
    // than by the theme's mode: a dark theme may still carry a pale accent, and
    // white on pale is the one combination that cannot be read.
    return m_accent.lightnessF() > 0.55 ? m_background : m_foreground;
}

// The chequerboard behind transparency. Both squares step away from the page by
// mixing in the foreground, and `checkerLight` is the lighter of the two in
// either mode -- which is why the amounts swap: under a dark theme the further
// square is the lighter one, under a light theme it is the darker one.
//
// A light theme used to get its lighter square darker than its darker one. The
// pair still alternated, so it read as a chequerboard, but the empty area sat
// heavier on the page than the drawing did, which is the one thing an empty
// pixel must never do.
QColor Theme::checkerDark() const
{
    return mix(m_background, m_foreground, m_dark ? 0.06 : 0.14);
}

QColor Theme::checkerLight() const
{
    return mix(m_background, m_foreground, m_dark ? 0.14 : 0.06);
}

QColor Theme::fill(const QColor &role, qreal alpha) const
{
    QColor wash = role;
    wash.setAlphaF(qBound(0.0, alpha, 1.0));
    return wash;
}

} // namespace omapixel
