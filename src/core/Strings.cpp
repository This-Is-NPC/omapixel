#include "Strings.h"

#include "Config.h"
#include "Codec.h"
#include "Output.h"

#include <QCoreApplication>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QLocale>
#include <QProcessEnvironment>
#include <QStandardPaths>

namespace omapixel {

Strings::Strings(QObject *parent) : QObject(parent)
{
}

Strings &Strings::shared()
{
    static Strings only;
    return only;
}

QString Strings::catalogueDir()
{
    return QStringLiteral(OMAPIXEL_I18N_DIR);
}

QStringList Strings::searchPath()
{
    QStringList places;
    // The user's own first, so a file dropped there wins without a rebuild --
    // which is the whole point of catalogues being files.
    const QString config =
        QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (!config.isEmpty())
        places << config + QStringLiteral("/i18n");
    // Then next to the sources, so a checkout works with no install step.
    places << catalogueDir();
    // Then whatever was installed beside the binary.
    places << QCoreApplication::applicationDirPath() + QStringLiteral("/../share/omapixel/i18n");
    return places;
}

QString Strings::preferredLanguage()
{
    // The environment first, so one run can be in another language without
    // editing anything; then the config file, which is where a lasting choice
    // belongs; then the system.
    const QString asked =
        QProcessEnvironment::systemEnvironment().value(QStringLiteral("OMAPIXEL_LANG"));
    if (!asked.isEmpty())
        return asked;
    const QString configured = Config::shared().text(QStringLiteral("language"));
    if (!configured.isEmpty())
        return configured;
    return QLocale::system().name();   // e.g. pt_BR
}

bool Strings::merge(const QString &language)
{
    if (language.isEmpty())
        return false;
    for (const QString &place : searchPath()) {
        const QString path = place + QLatin1Char('/') + language + QStringLiteral(".json");
        QByteArray bytes;
        if (!input::readRegularFile(path, Document::maxDocumentBytes, &bytes))
            continue;
        if (bytes.size() > Document::maxDocumentBytes)
            continue;
        QString scanError;
        if (Codec::rejectDuplicateJsonKeys(bytes, &scanError))
            continue;
        QJsonParseError parseError;
        const QJsonDocument parsed = QJsonDocument::fromJson(bytes, &parseError);
        if (parseError.error != QJsonParseError::NoError || !parsed.isObject())
            continue;
        const QJsonObject entries = parsed.object();
        for (auto it = entries.constBegin(); it != entries.constEnd(); ++it) {
            if (it.value().isString() && !it.value().toString().isEmpty())
                m_strings.insert(it.key(), it.value().toString());
        }
        return true;
    }
    return false;
}

void Strings::load(const QString &language)
{
    m_strings.clear();
    merge(QStringLiteral("en"));       // the floor: every key, in English
    m_language = QStringLiteral("en");

    if (language.isEmpty() || language.startsWith(QStringLiteral("en")))
        return;

    // `pt_BR`, then `pt`: a general catalogue serves a specific system until
    // somebody writes the specific one.
    if (merge(language)) {
        m_language = language;
        return;
    }
    const QString general = language.section(QLatin1Char('_'), 0, 0);
    if (general != language && merge(general))
        m_language = general;
}

QString Strings::t(const QString &key) const
{
    const auto found = m_strings.constFind(key);
    // The key itself, so a missing string is visible and obviously wrong
    // rather than an empty gap nobody notices.
    return found == m_strings.constEnd() ? key : found.value();
}

QStringList Strings::keys() const
{
    QStringList out = m_strings.keys();
    out.sort();
    return out;
}

} // namespace omapixel
