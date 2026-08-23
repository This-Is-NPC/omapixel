#include "Config.h"

#include "Document.h"
#include "Toml.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QKeySequence>
#include <QProcessEnvironment>
#include <QStandardPaths>

#include <cmath>
#include <limits>
#include <QTextStream>

namespace omapixel {
namespace {

/// The keys, by the names a person writes in a config file.
///
/// Lowercase and spelled out, the way herdr and Hyprland spell them, rather
/// than Qt's `Key_BracketLeft`. Punctuation is accepted both by name and as
/// itself, because half of everybody writes `[` and the other half writes
/// `bracketleft` and neither is wrong.
struct Named
{
    const char *name;
    int key;
};

const Named NamedKeys[] = {
    {"esc", Qt::Key_Escape},        {"escape", Qt::Key_Escape},
    {"enter", Qt::Key_Return},      {"return", Qt::Key_Return},
    {"tab", Qt::Key_Tab},           {"space", Qt::Key_Space},
    {"backspace", Qt::Key_Backspace}, {"bksp", Qt::Key_Backspace},
    {"delete", Qt::Key_Delete},     {"del", Qt::Key_Delete},
    {"insert", Qt::Key_Insert},     {"home", Qt::Key_Home},
    {"end", Qt::Key_End},           {"pageup", Qt::Key_PageUp},
    {"pagedown", Qt::Key_PageDown}, {"left", Qt::Key_Left},
    {"right", Qt::Key_Right},       {"up", Qt::Key_Up},
    {"down", Qt::Key_Down},

    {"plus", Qt::Key_Plus},         {"+", Qt::Key_Plus},
    {"minus", Qt::Key_Minus},       {"-", Qt::Key_Minus},
    {"equal", Qt::Key_Equal},       {"=", Qt::Key_Equal},
    {"comma", Qt::Key_Comma},       {",", Qt::Key_Comma},
    {"period", Qt::Key_Period},     {".", Qt::Key_Period},
    {"semicolon", Qt::Key_Semicolon}, {";", Qt::Key_Semicolon},
    {"colon", Qt::Key_Colon},       {":", Qt::Key_Colon},
    {"slash", Qt::Key_Slash},       {"/", Qt::Key_Slash},
    {"backslash", Qt::Key_Backslash}, {"\\", Qt::Key_Backslash},
    {"apostrophe", Qt::Key_Apostrophe}, {"'", Qt::Key_Apostrophe},
    {"backtick", Qt::Key_QuoteLeft}, {"`", Qt::Key_QuoteLeft},
    {"bracketleft", Qt::Key_BracketLeft}, {"[", Qt::Key_BracketLeft},
    {"bracketright", Qt::Key_BracketRight}, {"]", Qt::Key_BracketRight},
    {"less", Qt::Key_Less},         {"<", Qt::Key_Less},
    {"greater", Qt::Key_Greater},   {">", Qt::Key_Greater},
    {"question", Qt::Key_Question}, {"?", Qt::Key_Question},
    {"asterisk", Qt::Key_Asterisk}, {"*", Qt::Key_Asterisk},
    {"underscore", Qt::Key_Underscore}, {"_", Qt::Key_Underscore},
};

/// How a key is written back out for the hint bar. Short, because the bar has
/// a line and the studio has forty commands.
QString shortName(int key)
{
    switch (key) {
    case Qt::Key_Escape:    return QStringLiteral("Esc");
    case Qt::Key_Return:
    case Qt::Key_Enter:     return QStringLiteral("Enter");
    case Qt::Key_Backspace: return QStringLiteral("Bksp");
    case Qt::Key_Delete:    return QStringLiteral("Del");
    case Qt::Key_Space:     return QStringLiteral("Space");
    case Qt::Key_Tab:       return QStringLiteral("Tab");
    case Qt::Key_Left:      return QStringLiteral("←");
    case Qt::Key_Right:     return QStringLiteral("→");
    case Qt::Key_Up:        return QStringLiteral("↑");
    case Qt::Key_Down:      return QStringLiteral("↓");
    default: break;
    }
    // Punctuation is listed twice -- spelled out, then as itself. The
    // character is the one that fits on a bar with forty commands on it.
    for (const Named &named : NamedKeys) {
        if (named.key == key && QString::fromLatin1(named.name).size() == 1)
            return QString::fromLatin1(named.name);
    }
    for (const Named &named : NamedKeys) {
        if (named.key == key)
            return QString::fromLatin1(named.name);
    }
    if (key >= Qt::Key_F1 && key <= Qt::Key_F35)
        return QStringLiteral("F") + QString::number(key - Qt::Key_F1 + 1);
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return QString(QChar(key));
    if (key >= Qt::Key_0 && key <= Qt::Key_9)
        return QString(QChar(key));
    return QString();
}

/// Punctuation, for the shift-tolerant second pass. Letters and digits are
/// excluded on purpose: `c` and `shift+c` are two different commands here and
/// always will be.
bool isPunctuation(int key)
{
    if (key >= Qt::Key_A && key <= Qt::Key_Z)
        return false;
    if (key >= Qt::Key_0 && key <= Qt::Key_9)
        return false;
    return key > Qt::Key_Space && key < Qt::Key_A;
}

constexpr int ModifierMask = Qt::ShiftModifier | Qt::ControlModifier
                             | Qt::AltModifier | Qt::MetaModifier;

} // namespace

// ------------------------------------------------------------- the defaults

const QList<QPair<QString, QVariant>> &Config::settings()
{
    // The canonical list. A key that is not here is a key nothing reads, and
    // `omapixel config check` says so rather than letting a typo sit in a file
    // doing nothing for a year.
    static const QList<QPair<QString, QVariant>> only = {
        {QStringLiteral("language"), QString()},

        {QStringLiteral("window.width"), 1280},
        {QStringLiteral("window.height"), 820},
        {QStringLiteral("window.hints"), true},

        {QStringLiteral("canvas.zoom"), QStringLiteral("fit")},
        {QStringLiteral("canvas.grid"), true},
        {QStringLiteral("canvas.onion"), false},
        {QStringLiteral("canvas.big_step"), 8},
        {QStringLiteral("canvas.caret_margin_x"), 0},
        {QStringLiteral("canvas.caret_margin_y"), 0},

        {QStringLiteral("playback.loop"), true},

        {QStringLiteral("studio.scratch"), true},

        {QStringLiteral("document.width"), 32},
        {QStringLiteral("document.height"), 24},
        {QStringLiteral("document.fps"), 8},

        {QStringLiteral("history.depth"), 80},

        {QStringLiteral("warnings.file_mib"), 16},
        {QStringLiteral("warnings.clips"), 256},
        {QStringLiteral("warnings.frames_per_clip"), 1024},
        {QStringLiteral("warnings.frames_total"), 4096},
        {QStringLiteral("warnings.palette_slots"), 256},
        {QStringLiteral("warnings.render_megapixels"), 64},
    };
    return only;
}

const QList<QPair<QString, QString>> &Config::actions()
{
    // Every action, with the key it comes with. The order is the order the
    // shipped config lists them in, which is the order they are grouped in the
    // menus -- a config file is also documentation, and one sorted
    // alphabetically explains nothing.
    static const QList<QPair<QString, QString>> only = {
        {QStringLiteral("new"), QStringLiteral("ctrl+n")},
        {QStringLiteral("open"), QStringLiteral("ctrl+o")},
        {QStringLiteral("save"), QStringLiteral("ctrl+s")},
        {QStringLiteral("save_as"), QStringLiteral("ctrl+shift+s")},
        {QStringLiteral("export_png"), QStringLiteral("ctrl+e")},
        {QStringLiteral("export_sheet"), QStringLiteral("ctrl+shift+e")},
        {QStringLiteral("quit"), QStringLiteral("ctrl+q")},

        {QStringLiteral("undo"), QStringLiteral("ctrl+z")},
        {QStringLiteral("redo"), QStringLiteral("ctrl+shift+z")},
        {QStringLiteral("copy_pixels"), QStringLiteral("ctrl+c")},
        {QStringLiteral("paste_pixels"), QStringLiteral("ctrl+v")},
        {QStringLiteral("clear_frame"), QStringLiteral("ctrl+delete")},
        {QStringLiteral("trim"), QStringLiteral("ctrl+shift+t")},

        {QStringLiteral("tool_pencil"), QStringLiteral("b")},
        {QStringLiteral("tool_eraser"), QStringLiteral("e")},
        {QStringLiteral("tool_bucket"), QStringLiteral("f")},
        {QStringLiteral("tool_picker"), QStringLiteral("i")},
        {QStringLiteral("tool_hand"), QStringLiteral("h")},

        {QStringLiteral("caret_left"), QStringLiteral("left")},
        {QStringLiteral("caret_right"), QStringLiteral("right")},
        {QStringLiteral("caret_up"), QStringLiteral("up")},
        {QStringLiteral("caret_down"), QStringLiteral("down")},
        {QStringLiteral("select_left"), QStringLiteral("shift+left")},
        {QStringLiteral("select_right"), QStringLiteral("shift+right")},
        {QStringLiteral("select_up"), QStringLiteral("shift+up")},
        {QStringLiteral("select_down"), QStringLiteral("shift+down")},
        {QStringLiteral("select_left_far"), QStringLiteral("ctrl+shift+left")},
        {QStringLiteral("select_right_far"), QStringLiteral("ctrl+shift+right")},
        {QStringLiteral("select_up_far"), QStringLiteral("ctrl+shift+up")},
        {QStringLiteral("select_down_far"), QStringLiteral("ctrl+shift+down")},
        {QStringLiteral("caret_left_far"), QStringLiteral("ctrl+left")},
        {QStringLiteral("caret_right_far"), QStringLiteral("ctrl+right")},
        {QStringLiteral("caret_up_far"), QStringLiteral("ctrl+up")},
        {QStringLiteral("caret_down_far"), QStringLiteral("ctrl+down")},
        {QStringLiteral("go_to_pixel"), QStringLiteral("g")},

        {QStringLiteral("paint"), QStringLiteral("[\"enter\", \"x\"]")},
        {QStringLiteral("erase"), QStringLiteral("[\"backspace\", \"delete\"]")},
        {QStringLiteral("cancel"), QStringLiteral("esc")},

        {QStringLiteral("slot_leader"), QStringLiteral("semicolon")},
        {QStringLiteral("choose_colour"), QStringLiteral("c")},
        {QStringLiteral("replace_colour"), QStringLiteral("shift+c")},
        {QStringLiteral("roulette"), QStringLiteral("r")},
        {QStringLiteral("draw_mode"), QStringLiteral("d")},
        {QStringLiteral("pick_mode"), QStringLiteral("p")},
        {QStringLiteral("line_point"), QStringLiteral("l")},

        {QStringLiteral("play"), QStringLiteral("space")},
        {QStringLiteral("frame_previous"), QStringLiteral("comma")},
        {QStringLiteral("frame_next"), QStringLiteral("period")},
        {QStringLiteral("frame_add"), QStringLiteral("ctrl+shift+n")},
        {QStringLiteral("frame_duplicate"), QStringLiteral("ctrl+d")},
        {QStringLiteral("frame_move_back"), QStringLiteral("shift+comma")},
        {QStringLiteral("frame_move_on"), QStringLiteral("shift+period")},
        {QStringLiteral("clip_previous"), QStringLiteral("bracketleft")},
        {QStringLiteral("clip_next"), QStringLiteral("bracketright")},

        {QStringLiteral("zoom_in"), QStringLiteral("[\"ctrl+plus\", \"plus\", \"equal\"]")},
        {QStringLiteral("zoom_out"), QStringLiteral("[\"ctrl+minus\", \"minus\"]")},
        {QStringLiteral("zoom_fit"), QStringLiteral("ctrl+0")},
        {QStringLiteral("toggle_grid"), QStringLiteral("m")},
        {QStringLiteral("toggle_onion"), QStringLiteral("o")},
        {QStringLiteral("toggle_hints"), QString()},
        {QStringLiteral("toggle_loop"), QString()},
        {QStringLiteral("menus"), QStringLiteral("f10")},
    };
    return only;
}

// ------------------------------------------------------------- the file

QString Config::file()
{
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    const QString asked = env.value(QStringLiteral("OMAPIXEL_CONFIG_PATH"));
    if (!asked.isEmpty())
        return asked;
    const QString home =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    return home + QStringLiteral("/config.toml");
}

QStringList Config::defaultSearchPath()
{
    // The checkout first, so working on the file and printing it are the same
    // thing; then beside the binary, which is where `mise run install` and a
    // package both put it.
    return {QStringLiteral(OMAPIXEL_CONFIG_DIR) + QStringLiteral("/config.toml"),
            QCoreApplication::applicationDirPath()
                + QStringLiteral("/../share/omapixel/config.toml"),
            QStringLiteral("/usr/share/omapixel/config.toml")};
}

QString Config::defaultText()
{
    for (const QString &path : defaultSearchPath()) {
        QFile file(path);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text))
            return QString::fromUtf8(file.readAll());
    }
    return QString();
}

// ------------------------------------------------------------- the parsing

int Config::parseKey(const QString &binding, int *modifiers)
{
    *modifiers = 0;
    QString rest = binding.trimmed().toLower();
    if (rest.isEmpty())
        return -1;

    // Eaten from the left rather than split on `+`, so `ctrl++` -- control and
    // the plus key -- parses instead of turning into three empty pieces.
    for (;;) {
        static const QPair<QLatin1String, Qt::KeyboardModifier> prefixes[] = {
            {QLatin1String("ctrl+"), Qt::ControlModifier},
            {QLatin1String("control+"), Qt::ControlModifier},
            {QLatin1String("shift+"), Qt::ShiftModifier},
            {QLatin1String("alt+"), Qt::AltModifier},
            {QLatin1String("super+"), Qt::MetaModifier},
            {QLatin1String("meta+"), Qt::MetaModifier},
            {QLatin1String("cmd+"), Qt::MetaModifier},
        };
        bool ate = false;
        for (const auto &prefix : prefixes) {
            if (rest.startsWith(prefix.first) && rest.size() > prefix.first.size()) {
                *modifiers |= prefix.second;
                rest = rest.mid(prefix.first.size());
                ate = true;
                break;
            }
        }
        if (!ate)
            break;
    }

    for (const Named &named : NamedKeys) {
        if (rest == QLatin1String(named.name))
            return named.key;
    }
    if (rest.size() == 1) {
        const QChar ch = rest.at(0);
        if (ch >= QLatin1Char('a') && ch <= QLatin1Char('z'))
            return Qt::Key_A + (ch.unicode() - 'a');
        if (ch >= QLatin1Char('0') && ch <= QLatin1Char('9'))
            return Qt::Key_0 + (ch.unicode() - '0');
    }
    if (rest.size() >= 2 && rest.at(0) == QLatin1Char('f')) {
        bool whole = false;
        const int which = rest.mid(1).toInt(&whole);
        if (whole && which >= 1 && which <= 35)
            return Qt::Key_F1 + which - 1;
    }
    return -1;
}

QString Config::spell(int key, int modifiers)
{
    if (key < 0)
        return QString();
    return QKeySequence(QKeyCombination(static_cast<Qt::KeyboardModifiers>(modifiers),
                                        static_cast<Qt::Key>(key)))
        .toString(QKeySequence::PortableText);
}

// ------------------------------------------------------------- the loading

Config::Config(QObject *parent) : QObject(parent)
{
    load();
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, this, [this] {
        load();
        emit changed();
    });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] {
        // A file saved by an editor that writes a new one and renames it over
        // the top is a file the watcher has lost. Re-arming on the directory
        // is what keeps a rebind you made in vim from being the last one that
        // ever took.
        load();
        emit changed();
    });
}

Config &Config::shared()
{
    static Config only;
    return only;
}

void Config::bind(const QString &action, const QVariant &value, int line)
{
    QStringList written;
    if (value.typeId() == QMetaType::QVariantList) {
        const QVariantList list = value.toList();
        for (const QVariant &one : list)
            written.append(one.toString());
    } else {
        written.append(value.toString());
    }

    QList<Combination> combinations;
    QStringList kept;
    for (const QString &one : written) {
        // An empty string is how an action is left unbound, which is a thing
        // people want: a key you never use is a key another program can have.
        if (one.trimmed().isEmpty())
            continue;
        int modifiers = 0;
        const int key = parseKey(one, &modifiers);
        if (key < 0) {
            m_problems.append(
                QStringLiteral("line %1: keys.%2 — \"%3\" is not a key")
                    .arg(line).arg(action, one));
            continue;
        }
        combinations.append({key, modifiers & ModifierMask});
        kept.append(one.trimmed().toLower());
    }
    m_bound.insert(action, combinations);
    m_written.insert(action, kept);
}

void Config::load()
{
    m_values.clear();
    m_bound.clear();
    m_written.clear();
    m_problems.clear();

    for (const auto &setting : settings())
        m_values.insert(setting.first, setting.second);
    for (const auto &action : actions()) {
        const QString fallback = action.second;
        if (fallback.startsWith(QLatin1Char('['))) {
            // The defaults are written the way the file writes them, so the
            // shipped config and this table cannot say different things.
            const toml::Table parsed =
                toml::read(QStringLiteral("k = ") + fallback);
            bind(action.first, parsed.value(QStringLiteral("k")), 0);
        } else {
            bind(action.first, fallback, 0);
        }
    }

    const QString path = file();
    QFile handle(path);
    watch();
    if (!handle.open(QIODevice::ReadOnly | QIODevice::Text))
        return;   // no file is the normal case: the defaults are the program

    const toml::Table parsed = toml::read(QString::fromUtf8(handle.readAll()));
    for (const toml::Problem &problem : parsed.problems)
        m_problems.append(QStringLiteral("line %1: %2").arg(problem.line).arg(problem.message));

    // Two actions on one key is the mistake a keybinding file makes, and the
    // symptom -- one of them silently never firing -- is impossible to guess
    // at. Collected as the file is read so the message can name both.
    QHash<QString, QString> claimed;

    for (const QString &key : parsed.order) {
        const int line = parsed.lines.value(key);
        if (key.startsWith(QLatin1String("keys."))) {
            const QString action = key.mid(5);
            bool known = false;
            for (const auto &pair : actions())
                known = known || pair.first == action;
            if (!known) {
                m_problems.append(QStringLiteral("line %1: keys.%2 — no such action")
                                      .arg(line).arg(action));
                continue;
            }
            bind(action, parsed.value(key), line);
            continue;
        }

        if (!m_values.contains(key)) {
            m_problems.append(
                QStringLiteral("line %1: %2 — nothing reads this").arg(line).arg(key));
            continue;
        }
        const QVariant was = m_values.value(key);
        const QVariant now = parsed.value(key);
        // A setting that changes type is a setting that will be read wrong:
        // `hints = "true"` is a string, and a string is not false.
        const bool wasNumber = was.typeId() == QMetaType::Int
                               || was.typeId() == QMetaType::LongLong
                               || was.typeId() == QMetaType::Double;
        const bool isNumber = now.typeId() == QMetaType::LongLong
                               || now.typeId() == QMetaType::Double;
        const bool isCaretMargin = key == QLatin1String("canvas.caret_margin_x")
                                   || key == QLatin1String("canvas.caret_margin_y");
        if (was.typeId() == QMetaType::Bool && now.typeId() != QMetaType::Bool) {
            m_problems.append(QStringLiteral("line %1: %2 — wants true or false")
                                  .arg(line).arg(key));
            continue;
        }
        if (wasNumber && !isNumber && !isCaretMargin) {
            m_problems.append(
                QStringLiteral("line %1: %2 — wants a number").arg(line).arg(key));
            continue;
        }
        if (was.typeId() == QMetaType::QString
            && now.typeId() != QMetaType::QString
            && key != QLatin1String("canvas.zoom")) {
            m_problems.append(
                QStringLiteral("line %1: %2 — wants text").arg(line).arg(key));
            continue;
        }

        const auto integerIn = [&](qint64 minimum, qint64 maximum,
                                   const QString &description) {
            bool ok = false;
            const double number = now.toDouble(&ok);
            if (!ok || !std::isfinite(number) || std::floor(number) != number
                || number < minimum || number > maximum) {
                m_problems.append(QStringLiteral("line %1: %2 — %3")
                                      .arg(line).arg(key, description));
                return false;
            }
            return true;
        };
        if ((key == QLatin1String("window.width")
             || key == QLatin1String("window.height")
             || key == QLatin1String("canvas.big_step")
             || key == QLatin1String("history.depth"))
            && !integerIn(1, std::numeric_limits<int>::max(),
                          QStringLiteral("wants a positive integer")))
            continue;
        if ((key == QLatin1String("document.width")
             || key == QLatin1String("document.height"))
            && !integerIn(1, Document::maxDimension,
                          QStringLiteral("wants an integer from 1 to %1")
                              .arg(Document::maxDimension)))
            continue;
        if (key == QLatin1String("document.fps")
            && !integerIn(1, 60, QStringLiteral("wants an integer from 1 to 60")))
            continue;
        if (key.startsWith(QLatin1String("warnings."))
            && !integerIn(0, std::numeric_limits<int>::max(),
                          QStringLiteral("wants zero or a positive integer")))
            continue;
        if (key == QLatin1String("canvas.zoom")) {
            if (now.typeId() == QMetaType::QString
                && now.toString() == QLatin1String("fit")) {
                m_values.insert(key, now);
                continue;
            }
            if (!integerIn(1, 40,
                           QStringLiteral("wants `fit` or an integer from 1 to 40")))
                continue;
        }
        if (isCaretMargin) {
            const QString wanted = QStringLiteral(
                "wants `center` or a non-negative integer");
            if (now.typeId() == QMetaType::QString) {
                if (now.toString() == QLatin1String("center")) {
                    m_values.insert(key, now);
                    continue;
                }
                m_problems.append(QStringLiteral("line %1: %2 — %3")
                                      .arg(line).arg(key, wanted));
                continue;
            }
            if (!isNumber
                || !integerIn(0, std::numeric_limits<int>::max(), wanted))
                continue;
        }
        m_values.insert(key, now);
    }

    for (auto it = m_bound.constBegin(); it != m_bound.constEnd(); ++it) {
        for (const Combination &combination : it.value()) {
            const QString press = spell(combination.key, combination.modifiers);
            const QString already = claimed.value(press);
            if (!already.isEmpty() && already != it.key()) {
                m_problems.append(QStringLiteral("%1 is on both %2 and %3 — one of them wins")
                                      .arg(press, already, it.key()));
                continue;
            }
            claimed.insert(press, it.key());
        }
    }
    m_problems.sort();
}

void Config::watch()
{
    if (!m_watcher.files().isEmpty())
        m_watcher.removePaths(m_watcher.files());
    if (!m_watcher.directories().isEmpty())
        m_watcher.removePaths(m_watcher.directories());
    const QFileInfo info(file());
    if (info.exists())
        m_watcher.addPath(info.absoluteFilePath());
    if (info.dir().exists())
        m_watcher.addPath(info.absolutePath());
}

// ------------------------------------------------------------- the questions

QVariant Config::value(const QString &key) const
{
    return m_values.value(key);
}

int Config::number(const QString &key) const
{
    return m_values.value(key).toInt();
}

double Config::decimal(const QString &key) const
{
    return m_values.value(key).toDouble();
}

bool Config::flag(const QString &key) const
{
    return m_values.value(key).toBool();
}

QString Config::text(const QString &key) const
{
    return m_values.value(key).toString();
}

QString Config::action(int key, int modifiers) const
{
    // Return and the keypad's Enter are one key as far as anybody typing is
    // concerned, and only one of them can be written in a config file.
    if (key == Qt::Key_Enter)
        key = Qt::Key_Return;
    const int held = modifiers & ModifierMask;

    for (auto it = m_bound.constBegin(); it != m_bound.constEnd(); ++it) {
        for (const Combination &combination : it.value()) {
            if (combination.key == key && combination.modifiers == held)
                return it.key();
        }
    }

    // Second pass: punctuation, ignoring shift. `+` is shift-and-equals on a
    // US layout and its own key on a numeric pad, and `zoom_in = "plus"` means
    // the plus key on both. Only reached when nothing matched exactly, so a
    // deliberate `shift+comma` still beats a plain `comma`.
    if (isPunctuation(key) && (held & Qt::ShiftModifier)) {
        for (auto it = m_bound.constBegin(); it != m_bound.constEnd(); ++it) {
            for (const Combination &combination : it.value()) {
                if (combination.key == key
                    && combination.modifiers == (held & ~Qt::ShiftModifier))
                    return it.key();
            }
        }
    }
    return QString();
}

QString Config::shortcut(const QString &action) const
{
    const QList<Combination> combinations = m_bound.value(action);
    if (combinations.isEmpty())
        return QString();
    return spell(combinations.first().key, combinations.first().modifiers);
}

QString Config::label(const QString &action) const
{
    const QList<Combination> combinations = m_bound.value(action);
    if (combinations.isEmpty())
        return QString();
    const Combination combination = combinations.first();
    QString out;
    if (combination.modifiers & Qt::ControlModifier)
        out += QStringLiteral("^");
    if (combination.modifiers & Qt::ShiftModifier)
        out += QStringLiteral("⇧");
    if (combination.modifiers & Qt::AltModifier)
        out += QStringLiteral("⌥");
    if (combination.modifiers & Qt::MetaModifier)
        out += QStringLiteral("◆");
    QString name = shortName(combination.key);
    // A bare letter reads as itself, the way the key is printed on the board;
    // one with a modifier reads as a capital, the way a shortcut is written.
    if (out.isEmpty() && name.size() == 1)
        name = name.toLower();
    return out + name;
}

QStringList Config::bindings(const QString &action) const
{
    return m_written.value(action);
}

QVariantMap Config::settingsMap() const
{
    QVariantMap out;
    for (auto it = m_values.constBegin(); it != m_values.constEnd(); ++it)
        out.insert(it.key(), it.value());
    return out;
}

QVariantMap Config::shortcuts() const
{
    QVariantMap out;
    for (const auto &action : actions())
        out.insert(action.first, shortcut(action.first));
    return out;
}

QVariantMap Config::labels() const
{
    QVariantMap out;
    for (const auto &action : actions())
        out.insert(action.first, label(action.first));
    return out;
}

} // namespace omapixel
