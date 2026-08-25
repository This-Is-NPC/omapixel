#include "PluginManifest.h"

#include "Codec.h"
#include "Document.h"
#include "Output.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <cmath>

namespace omapixel {
namespace {

bool fail(QString *error, const QString &path, const QString &message)
{
    *error = QStringLiteral("%1: %2").arg(path, message);
    return false;
}

bool integerOne(const QJsonValue &value, const QByteArray &json, const QByteArray &field)
{
    if (!value.isDouble() || !std::isfinite(value.toDouble()) || value.toDouble() != 1.0)
        return false;

    // QJsonDocument normalizes 1.0 and 1 to the same QJsonValue. Keep the
    // integer-only manifest fields strict by checking the original token.
    int objectDepth = 0;
    int arrayDepth = 0;
    for (int index = 0; index < json.size(); ++index) {
        const char character = json.at(index);
        if (character == '"') {
            const int start = index + 1;
            bool escaped = false;
            ++index;
            for (; index < json.size(); ++index) {
                const char current = json.at(index);
                if (escaped) {
                    escaped = false;
                } else if (current == '\\') {
                    escaped = true;
                } else if (current == '"') {
                    break;
                }
            }
            if (objectDepth == 1 && arrayDepth == 0) {
                const QByteArray key = json.mid(start, index - start);
                int after = index + 1;
                while (after < json.size()
                       && QByteArray(" \t\n\r").contains(json.at(after)))
                    ++after;
                if (key == field && after < json.size() && json.at(after) == ':') {
                    ++after;
                    while (after < json.size()
                           && QByteArray(" \t\n\r").contains(json.at(after)))
                        ++after;
                    return after < json.size() && json.at(after) == '1'
                        && (after + 1 == json.size()
                            || QByteArray(",]} \t\n\r").contains(json.at(after + 1)));
                }
            }
            continue;
        }
        if (character == '{')
            ++objectDepth;
        else if (character == '}')
            --objectDepth;
        else if (character == '[')
            ++arrayDepth;
        else if (character == ']')
            --arrayDepth;
    }
    return false;
}

bool required(const QJsonObject &object, const QString &key, QJsonValue *value,
              QString *error)
{
    if (!object.contains(key))
        return fail(error, QStringLiteral("$.%1").arg(key), QStringLiteral("is required"));
    *value = object.value(key);
    return true;
}

bool onlyFields(const QJsonObject &object, const QStringList &allowed,
                const QString &path, QString *error)
{
    for (const QString &key : object.keys()) {
        if (!allowed.contains(key))
            return fail(error, path + QLatin1Char('.') + key, QStringLiteral("unknown field"));
    }
    return true;
}

bool stringLength(const QJsonValue &value, int maximum, const QString &path,
                  QString *error)
{
    if (!value.isString() || value.toString().isEmpty()
        || value.toString().size() > maximum) {
        return fail(error, path,
                    QStringLiteral("must be a non-empty string of at most %1 characters")
                        .arg(maximum));
    }
    return true;
}

bool identifier(const QJsonValue &value, const QString &path, QString *error)
{
    if (!value.isString())
        return fail(error, path, QStringLiteral("must match [a-z][a-z0-9-]{0,63}"));
    const QString text = value.toString();
    if (text.isEmpty() || text.size() > 64 || text.at(0) < QLatin1Char('a')
        || text.at(0) > QLatin1Char('z'))
        return fail(error, path, QStringLiteral("must match [a-z][a-z0-9-]{0,63}"));
    for (const QChar character : text) {
        if (!((character >= QLatin1Char('a') && character <= QLatin1Char('z'))
              || (character >= QLatin1Char('0') && character <= QLatin1Char('9'))
              || character == QLatin1Char('-'))) {
            return fail(error, path, QStringLiteral("must match [a-z][a-z0-9-]{0,63}"));
        }
    }
    return true;
}

bool safeRelativePath(const QJsonValue &value, const QString &path, QString *error,
                      QString *relative)
{
    if (!value.isString())
        return fail(error, path, QStringLiteral("must be a safe slash-separated relative path"));
    const QString text = value.toString();
    if (text.isEmpty() || text.size() > 255 || text.startsWith(QLatin1Char('/'))
        || text.contains(QLatin1Char('\\'))) {
        return fail(error, path, QStringLiteral("must be a safe slash-separated relative path"));
    }
    const QStringList parts = text.split(QLatin1Char('/'));
    for (const QString &part : parts) {
        if (part.isEmpty() || part == QLatin1String(".") || part == QLatin1String(".."))
            return fail(error, path, QStringLiteral("must be a safe slash-separated relative path"));
        for (const QChar character : part) {
            const ushort code = character.unicode();
            if (code < 0x20 || (code >= 0x7f && code <= 0x9f))
                return fail(error, path,
                            QStringLiteral("must be a safe slash-separated relative path"));
        }
    }
    *relative = text;
    return true;
}

bool executableIsSafe(const QString &rootPath, const QString &relative, QString *error)
{
    const QFileInfo root(rootPath);
    if (root.isSymLink())
        return fail(error, QStringLiteral("$.executable"),
                    QStringLiteral("must not traverse a symlink"));

    QString current = root.absoluteFilePath();
    for (const QString &part : relative.split(QLatin1Char('/'))) {
        current = QDir(current).filePath(part);
        if (QFileInfo(current).isSymLink())
            return fail(error, QStringLiteral("$.executable"),
                        QStringLiteral("must not traverse a symlink"));
    }

    const QFileInfo executable(current);
    if (!executable.exists() || !executable.isFile() || executable.isSymLink()
        || !executable.isExecutable()) {
        return fail(error, QStringLiteral("$.executable"),
                    QStringLiteral("must name a regular executable file, not a symlink"));
    }

    const QString resolvedRoot = root.canonicalFilePath();
    const QString resolvedExecutable = executable.canonicalFilePath();
    if (resolvedRoot.isEmpty() || resolvedExecutable.isEmpty())
        return fail(error, QStringLiteral("$.executable"),
                    QStringLiteral("could not resolve the executable"));
    const QString relativePath = QDir(resolvedRoot).relativeFilePath(resolvedExecutable);
    if (relativePath == QLatin1String("..")
        || relativePath.startsWith(QStringLiteral("..%1").arg(QDir::separator()))) {
        return fail(error, QStringLiteral("$.executable"),
                    QStringLiteral("must remain inside the plugin root"));
    }
    return true;
}

bool parseActions(const QJsonValue &value, QList<PluginManifest::Action> *actions,
                  QString *error)
{
    if (!value.isArray() || value.toArray().isEmpty())
        return fail(error, QStringLiteral("$.actions"),
                    QStringLiteral("must contain at least one action"));

    QStringList names;
    const QJsonArray array = value.toArray();
    for (int index = 0; index < array.size(); ++index) {
        const QString path = QStringLiteral("$.actions[%1]").arg(index);
        const QJsonValue item = array.at(index);
        if (!item.isObject())
            return fail(error, path, QStringLiteral("must be an object"));
        const QJsonObject object = item.toObject();
        if (!onlyFields(object, {QStringLiteral("name"), QStringLiteral("kind")}, path, error))
            return false;

        QJsonValue name;
        QJsonValue kind;
        if (!required(object, QStringLiteral("name"), &name, error)
            || !required(object, QStringLiteral("kind"), &kind, error))
            return false;
        if (!stringLength(name, 128, path + QStringLiteral(".name"), error))
            return false;
        if (names.contains(name.toString()))
            return fail(error, path + QStringLiteral(".name"),
                        QStringLiteral("duplicates action name `%1`").arg(name.toString()));
        if (!kind.isString() || kind.toString() != QLatin1String("export"))
            return fail(error, path + QStringLiteral(".kind"),
                        QStringLiteral("must be exactly `export`"));
        names.append(name.toString());
        actions->append({name.toString(), kind.toString()});
    }
    return true;
}

} // namespace

PluginManifest::Result PluginManifest::read(const QString &rootPath)
{
    Result result;
    const QFileInfo rootInfo(rootPath);
    const QString root = rootInfo.absoluteFilePath();
    if (!rootInfo.exists() || !rootInfo.isDir()) {
        result.error = QStringLiteral("plugin root is not a directory");
        return result;
    }
    if (rootInfo.isSymLink()) {
        result.error = QStringLiteral("plugin root must not be a symlink");
        return result;
    }

    const QString manifestPath = QDir(root).filePath(QStringLiteral("omapixel-plugin.json"));
    QByteArray bytes;
    if (!input::readRegularFile(manifestPath, Document::maxDocumentBytes, &bytes,
                                &result.error))
        return result;
    if (bytes.size() > Document::maxDocumentBytes) {
        result.error = QStringLiteral("manifest exceeds hard limit of %1 MiB")
                           .arg(Document::maxDocumentBytes / (1024 * 1024));
        return result;
    }

    QString scanError;
    if (Codec::rejectDuplicateJsonKeys(bytes, &scanError)) {
        result.error = scanError;
        return result;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.error = QStringLiteral("manifest must be a JSON object (%1)")
                           .arg(parseError.errorString());
        return result;
    }

    const QJsonObject object = document.object();
    if (!onlyFields(object, {QStringLiteral("schemaVersion"), QStringLiteral("id"),
                             QStringLiteral("name"), QStringLiteral("version"),
                             QStringLiteral("pluginApi"), QStringLiteral("executable"),
                             QStringLiteral("actions")}, QStringLiteral("$"),
                     &result.error))
        return result;

    QJsonValue schemaVersion;
    QJsonValue id;
    QJsonValue name;
    QJsonValue version;
    QJsonValue pluginApi;
    QJsonValue executable;
    QJsonValue actions;
    if (!required(object, QStringLiteral("schemaVersion"), &schemaVersion, &result.error)
        || !required(object, QStringLiteral("id"), &id, &result.error)
        || !required(object, QStringLiteral("name"), &name, &result.error)
        || !required(object, QStringLiteral("version"), &version, &result.error)
        || !required(object, QStringLiteral("pluginApi"), &pluginApi, &result.error)
        || !required(object, QStringLiteral("executable"), &executable, &result.error)
        || !required(object, QStringLiteral("actions"), &actions, &result.error))
        return result;

    if (!integerOne(schemaVersion, bytes, QByteArrayLiteral("schemaVersion"))) {
        fail(&result.error, QStringLiteral("$.schemaVersion"),
             QStringLiteral("must be exactly integer 1"));
        return result;
    }
    if (!identifier(id, QStringLiteral("$.id"), &result.error)
        || !stringLength(name, 128, QStringLiteral("$.name"), &result.error)
        || !stringLength(version, 128, QStringLiteral("$.version"), &result.error))
        return result;
    if (!integerOne(pluginApi, bytes, QByteArrayLiteral("pluginApi"))) {
        fail(&result.error, QStringLiteral("$.pluginApi"),
             QStringLiteral("must be exactly integer 1"));
        return result;
    }

    QString executableText;
    if (!safeRelativePath(executable, QStringLiteral("$.executable"), &result.error,
                           &executableText)
        || !executableIsSafe(root, executableText, &result.error)
        || !parseActions(actions, &result.manifest.actions, &result.error))
        return result;

    result.manifest.rootPath = root;
    result.manifest.id = id.toString();
    result.manifest.name = name.toString();
    result.manifest.version = version.toString();
    result.manifest.executable = executableText;
    result.ok = true;
    return result;
}

} // namespace omapixel
