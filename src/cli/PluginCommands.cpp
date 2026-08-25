#include "PluginCommands.h"

#include "Codec.h"
#include "Output.h"
#include "PluginRegistry.h"
#include "TextSafety.h"

#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>

#include <cmath>

namespace omapixel {
namespace cli {
namespace {

constexpr qint64 pluginTimeoutMs = 60 * 1000;
constexpr qint64 pluginStreamBudget = 1024 * 1024;
constexpr qint64 pluginArtifactBudget = 64 * 1024 * 1024;

struct Parameter {
    QString key;
    QString value;
};

struct ProtocolResult {
    bool ok = false;
    QString artifact;
    QString error;
};

bool integerInRange(const QJsonValue &value, int minimum, int maximum)
{
    if (!value.isDouble())
        return false;
    const double number = value.toDouble();
    return std::isfinite(number) && std::floor(number) == number
        && number >= minimum && number <= maximum;
}

bool parseParameters(const QCommandLineParser &parser, QList<Parameter> *parameters,
                     QString *error)
{
    QStringList keys;
    for (const QString &raw : parser.values(QStringLiteral("param"))) {
        const int separator = raw.indexOf(QLatin1Char('='));
        if (separator <= 0 || separator == raw.size() - 1) {
            *error = QStringLiteral(
                "plugin run: --param must be KEY=VALUE with non-empty strings");
            return false;
        }
        const QString key = raw.left(separator);
        const QString value = raw.mid(separator + 1);
        if (key.size() > 128 || value.size() > 128) {
            *error = QStringLiteral(
                "plugin run: --param keys and values are limited to 128 characters");
            return false;
        }
        if (keys.contains(key)) {
            *error = QStringLiteral("plugin run: duplicate parameter key `%1`").arg(key);
            return false;
        }
        keys.append(key);
        parameters->append({key, value});
    }
    return true;
}

QProcessEnvironment pluginEnvironment()
{
    // PATH is needed by ordinary shebang executables. The locale and temporary
    // directory keys preserve normal local-process behavior without leaking the
    // caller's application, credential, or plugin-specific environment.
    static const QStringList retained{
        QStringLiteral("PATH"), QStringLiteral("HOME"), QStringLiteral("LANG"),
        QStringLiteral("LC_ALL"), QStringLiteral("LC_CTYPE"), QStringLiteral("TMPDIR")};
    const QProcessEnvironment parent = QProcessEnvironment::systemEnvironment();
    QProcessEnvironment result = parent;
    result.clear();
    for (const QString &key : retained) {
        if (parent.contains(key))
            result.insert(key, parent.value(key));
    }
    return result;
}

bool safeRelativePath(const QString &path)
{
    if (path.isEmpty() || path.size() > 255 || path.startsWith(QLatin1Char('/'))
        || path.contains(QLatin1Char('\\')))
        return false;
    for (const QString &part : path.split(QLatin1Char('/'))) {
        if (part.isEmpty() || part == QLatin1String(".") || part == QLatin1String(".."))
            return false;
        for (const QChar character : part) {
            const ushort code = character.unicode();
            if (code < 0x20 || (code >= 0x7f && code <= 0x9f))
                return false;
        }
    }
    return true;
}

bool onlyFields(const QJsonObject &object, const QStringList &allowed,
                QString *error)
{
    for (const QString &key : object.keys()) {
        if (!allowed.contains(key)) {
            *error = QStringLiteral("protocol record has unknown field `%1`").arg(key);
            return false;
        }
    }
    return true;
}

bool readArtifact(const QString &outputRoot, const QString &relative, QByteArray *bytes,
                  QString *error)
{
    if (!safeRelativePath(relative)) {
        *error = QStringLiteral("result artifact is not a safe relative path");
        return false;
    }

    QString current = QDir::cleanPath(outputRoot);
    for (const QString &part : relative.split(QLatin1Char('/'))) {
        current = QDir(current).filePath(part);
        if (QFileInfo(current).isSymLink()) {
            *error = QStringLiteral("result artifact must not traverse a symlink");
            return false;
        }
    }

    const QFileInfo outputInfo(outputRoot);
    const QFileInfo artifactInfo(current);
    if (!artifactInfo.exists() || !artifactInfo.isFile() || artifactInfo.isSymLink()) {
        *error = QStringLiteral("result artifact must be one regular non-symlink file");
        return false;
    }
    const QString outputCanonical = outputInfo.canonicalFilePath();
    const QString artifactCanonical = artifactInfo.canonicalFilePath();
    const QString prefix = outputCanonical + QDir::separator();
    if (outputCanonical.isEmpty() || artifactCanonical.isEmpty()
        || (artifactCanonical != outputCanonical && !artifactCanonical.startsWith(prefix))) {
        *error = QStringLiteral("result artifact must remain inside workspace output");
        return false;
    }

    if (!input::readRegularFile(current, pluginArtifactBudget, bytes, error))
        return false;
    if (bytes->size() > pluginArtifactBudget) {
        *error = QStringLiteral("result artifact exceeds the 64 MiB limit");
        return false;
    }
    return true;
}

bool parseProtocol(const QByteArray &stdoutBytes, const QString &requestId,
                   const QString &outputRoot, ProtocolResult *result, QString *error)
{
    if (stdoutBytes.size() > pluginStreamBudget) {
        *error = QStringLiteral("plugin stdout exceeds the 1 MiB protocol budget");
        return false;
    }

    bool terminalSeen = false;
    for (const QByteArray &line : stdoutBytes.split('\n')) {
        const QByteArray recordBytes = line.trimmed();
        if (recordBytes.isEmpty())
            continue;
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(recordBytes, &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            *error = QStringLiteral("plugin stdout contains malformed JSONL");
            return false;
        }
        const QJsonObject record = document.object();
        if (terminalSeen) {
            *error = QStringLiteral("plugin emitted output after the terminal result");
            return false;
        }
        if (record.value(QStringLiteral("requestId")).toString() != requestId) {
            *error = QStringLiteral("plugin response request ID does not match the request");
            return false;
        }

        const QString type = record.value(QStringLiteral("type")).toString();
        if (type == QLatin1String("progress")) {
            if (!onlyFields(record, {QStringLiteral("type"), QStringLiteral("requestId"),
                                     QStringLiteral("message"), QStringLiteral("percent")}, error))
                return false;
            const QString message = record.value(QStringLiteral("message")).toString();
            if (!record.value(QStringLiteral("message")).isString()
                || message.isEmpty() || message.size() > 256) {
                *error = QStringLiteral("plugin progress message is invalid");
                return false;
            }
            if (record.contains(QStringLiteral("percent"))
                && !integerInRange(record.value(QStringLiteral("percent")), 0, 100)) {
                *error = QStringLiteral("plugin progress percent is invalid");
                return false;
            }
            continue;
        }
        if (type != QLatin1String("result")) {
            *error = QStringLiteral("plugin stdout contains an unexpected record");
            return false;
        }
        if (!record.value(QStringLiteral("ok")).isBool()) {
            *error = QStringLiteral("plugin result ok must be boolean");
            return false;
        }
        if (record.value(QStringLiteral("ok")).toBool()) {
            if (!onlyFields(record, {QStringLiteral("type"), QStringLiteral("requestId"),
                                     QStringLiteral("ok"), QStringLiteral("artifact")}, error))
                return false;
            const QString artifact = record.value(QStringLiteral("artifact")).toString();
            QByteArray ignored;
            if (!record.value(QStringLiteral("artifact")).isString() || artifact.isEmpty()) {
                *error = QStringLiteral("plugin result artifact is invalid");
                return false;
            }
            if (!readArtifact(outputRoot, artifact, &ignored, error))
                return false;
            result->ok = true;
            result->artifact = artifact;
        } else {
            if (!onlyFields(record, {QStringLiteral("type"), QStringLiteral("requestId"),
                                     QStringLiteral("ok"), QStringLiteral("error")}, error))
                return false;
            const QString message = record.value(QStringLiteral("error")).toString();
            if (!record.value(QStringLiteral("error")).isString()
                || message.isEmpty() || message.size() > 1024) {
                *error = QStringLiteral("plugin failure message is invalid");
                return false;
            }
            result->error = message;
        }
        terminalSeen = true;
    }
    if (!terminalSeen) {
        *error = QStringLiteral("plugin did not emit exactly one terminal result");
        return false;
    }
    return true;
}

void drainProcess(QProcess *process, QByteArray *standardOutput, QByteArray *standardError,
                  bool *overflow)
{
    process->setReadChannel(QProcess::StandardOutput);
    if (standardOutput->size() <= pluginStreamBudget) {
        const QByteArray chunk = process->read(pluginStreamBudget + 1 - standardOutput->size());
        standardOutput->append(chunk);
        if (standardOutput->size() > pluginStreamBudget)
            *overflow = true;
    }
    process->setReadChannel(QProcess::StandardError);
    if (standardError->size() <= pluginStreamBudget) {
        const QByteArray chunk = process->read(pluginStreamBudget + 1 - standardError->size());
        standardError->append(chunk);
        if (standardError->size() > pluginStreamBudget)
            *overflow = true;
    }
}

QString processFailure(const QProcess &process, const QByteArray &standardError)
{
    QString message;
    if (process.exitStatus() != QProcess::NormalExit)
        message = QStringLiteral("plugin crashed");
    else
        message = QStringLiteral("plugin exited with status %1").arg(process.exitCode());
    if (!standardError.isEmpty())
        message += QStringLiteral(": %1").arg(QString::fromUtf8(standardError).trimmed());
    return message;
}

Outcome runFailure(const QString &pluginId, const QString &action, bool json,
                   const QString &message)
{
    Outcome result = Outcome::refused(message + QLatin1Char('\n'));
    if (json) {
        const QJsonObject report{{QStringLiteral("ok"), false},
                                 {QStringLiteral("plugin"), pluginId},
                                 {QStringLiteral("action"), action},
                                 {QStringLiteral("error"), message}};
        result.output = QJsonDocument(report).toJson(QJsonDocument::Compact) + '\n';
    }
    return result;
}

Outcome runUsageFailure(const QString &pluginId, const QString &action, bool json,
                        const QString &message)
{
    Outcome result = Outcome::wrong(message + QLatin1Char('\n'));
    if (json) {
        const QJsonObject report{{QStringLiteral("ok"), false},
                                 {QStringLiteral("plugin"), pluginId},
                                 {QStringLiteral("action"), action},
                                 {QStringLiteral("error"), message}};
        result.output = QJsonDocument(report).toJson(QJsonDocument::Compact) + '\n';
    }
    return result;
}

QJsonObject pluginObject(const PluginManifest &plugin)
{
    QJsonArray actions;
    for (const PluginManifest::Action &action : plugin.actions)
        actions.append(QJsonObject{{QStringLiteral("name"), action.name},
                                   {QStringLiteral("kind"), action.kind}});
    return QJsonObject{{QStringLiteral("id"), plugin.id},
                       {QStringLiteral("name"), plugin.name},
                       {QStringLiteral("version"), plugin.version},
                       {QStringLiteral("path"), plugin.rootPath},
                       {QStringLiteral("executable"), plugin.executable},
                       {QStringLiteral("actions"), actions}};
}

QJsonArray diagnosticsArray(const QList<PluginRegistry::Diagnostic> &diagnostics)
{
    QJsonArray result;
    for (const PluginRegistry::Diagnostic &diagnostic : diagnostics) {
        result.append(QJsonObject{{QStringLiteral("path"), diagnostic.path},
                                  {QStringLiteral("message"), diagnostic.message}});
    }
    return result;
}

QString humanPath(const QString &path)
{
    return text::escapeForTerminal(path);
}

QString humanMessage(const PluginRegistry::Diagnostic &diagnostic)
{
    return QStringLiteral("%1: %2").arg(humanPath(diagnostic.path),
                                        text::escapeForTerminal(diagnostic.message));
}

Outcome listPlugins(bool json)
{
    const PluginRegistry registry = PluginRegistry::discover();
    if (json) {
        QJsonArray plugins;
        for (const PluginManifest &plugin : registry.plugins())
            plugins.append(pluginObject(plugin));
        const QJsonObject report{{QStringLiteral("plugins"), plugins},
                                 {QStringLiteral("diagnostics"),
                                  diagnosticsArray(registry.diagnostics())}};
        Outcome result = Outcome::ok(QJsonDocument(report).toJson(QJsonDocument::Compact)
                                     + QByteArray("\n"));
        result.code = registry.diagnostics().isEmpty() ? 0 : 1;
        return result;
    }

    QString output;
    for (const PluginManifest &plugin : registry.plugins()) {
        output += QStringLiteral("%1  %2  %3  %4\n")
                      .arg(humanPath(plugin.id), -24)
                      .arg(text::escapeForTerminal(plugin.name), -24)
                      .arg(humanPath(plugin.version), -12)
                      .arg(humanPath(plugin.rootPath));
    }
    if (registry.plugins().isEmpty() && registry.diagnostics().isEmpty())
        output = QStringLiteral("no plugins found\n");
    Outcome result = Outcome::ok(output);
    for (const PluginRegistry::Diagnostic &diagnostic : registry.diagnostics())
        result.error += QStringLiteral("%1\n").arg(humanMessage(diagnostic));
    result.code = registry.diagnostics().isEmpty() ? 0 : 1;
    return result;
}

Outcome checkPlugin(const QString &target, bool json)
{
    const QFileInfo targetInfo(target);
    PluginRegistry registry = PluginRegistry::discover();
    PluginManifest selected;
    bool hasPlugin = false;
    QList<PluginRegistry::Diagnostic> diagnostics;

    if (targetInfo.exists() || target.contains(QLatin1Char('/'))
        || target.contains(QDir::separator())) {
        const PluginManifest::Result parsed = PluginManifest::read(target);
        if (parsed) {
            selected = parsed.manifest;
            hasPlugin = true;
        }
        else
            diagnostics.append({QDir::cleanPath(targetInfo.absoluteFilePath()), parsed.error});
    } else {
        const PluginManifest *found = registry.find(target);
        if (found) {
            selected = *found;
            hasPlugin = true;
        }
        if (hasPlugin) {
            const QString duplicate = QStringLiteral("duplicate plugin id `%1`").arg(target);
            for (const PluginRegistry::Diagnostic &diagnostic : registry.diagnostics()) {
                if (diagnostic.message.contains(duplicate))
                    diagnostics.append(diagnostic);
            }
        } else {
            diagnostics = registry.diagnostics();
            diagnostics.append({target, QStringLiteral("plugin was not found")});
        }
    }

    if (json) {
        QJsonObject report;
        if (hasPlugin)
            report.insert(QStringLiteral("plugin"), pluginObject(selected));
        else
            report.insert(QStringLiteral("plugin"), QJsonValue::Null);
        report.insert(QStringLiteral("diagnostics"), diagnosticsArray(diagnostics));
        Outcome result = Outcome::ok(QJsonDocument(report).toJson(QJsonDocument::Compact)
                                     + QByteArray("\n"));
        result.code = diagnostics.isEmpty() ? 0 : 1;
        return result;
    }

    Outcome result;
    if (hasPlugin) {
        result.output = QStringLiteral("%1: good\n  %2 %3\n  executable %4\n")
                            .arg(humanPath(selected.id), text::escapeForTerminal(selected.name),
                                 humanPath(selected.version), humanPath(selected.executable));
        result.code = diagnostics.isEmpty() ? 0 : 1;
    } else {
        result.code = 1;
    }
    for (const PluginRegistry::Diagnostic &diagnostic : diagnostics)
        result.error += QStringLiteral("%1\n").arg(humanMessage(diagnostic));
    return result;
}

Outcome runPlugin(const QStringList &words, const QCommandLineParser &parser)
{
    const bool json = parser.isSet(QStringLiteral("json"));
    const QString pluginId = words.value(1);
    const QString actionId = words.value(2);
    const QString documentPath = words.value(3);
    const QString outputPath = parser.value(QStringLiteral("out"));

    QList<Parameter> parameters;
    QString error;
    if (!parseParameters(parser, &parameters, &error))
        return runUsageFailure(pluginId, actionId, json, error);
    if (outputPath.isEmpty())
        return Outcome::wrong(QStringLiteral("plugin run: say --out where to write"));
    if (!output::validate(outputPath, {documentPath}, &error))
        return runFailure(pluginId, actionId, json, error);

    const PluginRegistry registry = PluginRegistry::discover();
    const PluginManifest *plugin = registry.find(pluginId);
    if (!plugin)
        return runFailure(pluginId, actionId, json,
                          QStringLiteral("plugin `%1` was not found").arg(pluginId));
    bool actionFound = false;
    for (const PluginManifest::Action &action : plugin->actions) {
        if (action.name == actionId) {
            actionFound = true;
            break;
        }
    }
    if (!actionFound)
        return runFailure(pluginId, actionId, json,
                          QStringLiteral("plugin action `%1` was not found").arg(actionId));

    const Codec::Result loaded = Codec::readFile(documentPath);
    if (!loaded)
        return runFailure(pluginId, actionId, json, loaded.error);

    QTemporaryDir workspace(
        QDir(QDir::tempPath()).filePath(QStringLiteral("omapixel-plugin-XXXXXX")));
    if (!workspace.isValid())
        return runFailure(pluginId, actionId, json,
                          QStringLiteral("could not create private plugin workspace"));
    const QString inputRoot = QDir(workspace.path()).filePath(QStringLiteral("input"));
    const QString outputRoot = QDir(workspace.path()).filePath(QStringLiteral("output"));
    if (!QDir().mkpath(inputRoot) || !QDir().mkpath(outputRoot))
        return runFailure(pluginId, actionId, json,
                          QStringLiteral("could not create private plugin workspace directories"));
    const QString snapshotPath = QDir(inputRoot).filePath(QStringLiteral("document.json"));
    if (!Codec::writeFile(snapshotPath, loaded.document, &error))
        return runFailure(pluginId, actionId, json, error);

    QJsonArray requestParams;
    for (const Parameter &parameter : parameters)
        requestParams.append(QJsonObject{{QStringLiteral("key"), parameter.key},
                                         {QStringLiteral("value"), parameter.value}});
    const QJsonObject request{{QStringLiteral("type"), QStringLiteral("request")},
                              {QStringLiteral("requestId"), QStringLiteral("req-1")},
                              {QStringLiteral("action"), actionId},
                              {QStringLiteral("document"), QStringLiteral("input/document.json")},
                              {QStringLiteral("outputDir"), QStringLiteral("output")},
                              {QStringLiteral("params"), requestParams}};
    const QByteArray requestBytes =
        QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';

    QProcess process;
    process.setWorkingDirectory(workspace.path());
    process.setProcessEnvironment(pluginEnvironment());
    process.setProgram(QDir(plugin->rootPath).filePath(plugin->executable));
    process.start();
    if (!process.waitForStarted(pluginTimeoutMs))
        return runFailure(pluginId, actionId, json,
                          QStringLiteral("plugin failed to start: %1").arg(process.errorString()));
    if (process.write(requestBytes) != requestBytes.size()
        || !process.waitForBytesWritten(pluginTimeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        return runFailure(pluginId, actionId, json,
                          QStringLiteral("could not send plugin request: %1").arg(process.errorString()));
    }
    process.closeWriteChannel();

    QByteArray standardOutput;
    QByteArray standardError;
    bool overflow = false;
    QElapsedTimer timer;
    timer.start();
    bool timedOut = false;
    while (process.state() != QProcess::NotRunning) {
        process.waitForFinished(100);
        drainProcess(&process, &standardOutput, &standardError, &overflow);
        if (overflow) {
            process.kill();
            process.waitForFinished(1000);
            break;
        }
        if (timer.elapsed() >= pluginTimeoutMs) {
            timedOut = true;
            process.kill();
            process.waitForFinished(1000);
            break;
        }
    }
    drainProcess(&process, &standardOutput, &standardError, &overflow);
    if (overflow)
        return runFailure(pluginId, actionId, json,
                          QStringLiteral("plugin stdout or stderr exceeds the 1 MiB budget"));
    if (timedOut)
        return runFailure(pluginId, actionId, json,
                          QStringLiteral("plugin timed out after 60 seconds"));
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return runFailure(pluginId, actionId, json, processFailure(process, standardError));

    ProtocolResult protocol;
    if (!parseProtocol(standardOutput, QStringLiteral("req-1"), outputRoot,
                       &protocol, &error))
        return runFailure(pluginId, actionId, json, error);
    if (!protocol.ok)
        return runFailure(pluginId, actionId, json,
                          QStringLiteral("plugin reported failure: %1").arg(protocol.error));

    QByteArray artifactBytes;
    if (!readArtifact(outputRoot, protocol.artifact, &artifactBytes, &error))
        return runFailure(pluginId, actionId, json, error);
    if (!output::writeAtomically(outputPath, artifactBytes, {documentPath}, &error))
        return runFailure(pluginId, actionId, json, error);

    Outcome result = Outcome::ok();
    if (json) {
        const QJsonObject report{{QStringLiteral("ok"), true},
                                 {QStringLiteral("plugin"), pluginId},
                                 {QStringLiteral("action"), actionId},
                                 {QStringLiteral("out"), outputPath}};
        result.output = QJsonDocument(report).toJson(QJsonDocument::Compact) + '\n';
    } else {
        result.output = QStringLiteral("%1: plugin %2/%3\n")
                           .arg(text::escapeForTerminal(outputPath),
                                text::escapeForTerminal(pluginId),
                                text::escapeForTerminal(actionId));
    }
    if (!standardError.isEmpty())
        result.error = QString::fromUtf8(standardError) + QLatin1Char('\n');
    return result;
}

} // namespace

Outcome runPluginCommand(const QStringList &words, const QCommandLineParser &parser)
{
    const bool json = parser.isSet(QStringLiteral("json"));
    if (words.isEmpty())
        return Outcome::wrong(QStringLiteral("plugin: list, check or run"));

    const QString action = words.first();
    if (action == QLatin1String("list")) {
        if (words.size() != 1)
            return Outcome::wrong(QStringLiteral("plugin list: no positional arguments allowed"));
        return listPlugins(json);
    }
    if (action == QLatin1String("check")) {
        if (words.size() != 2 || words.at(1).isEmpty())
            return Outcome::wrong(QStringLiteral("plugin check: say a plugin directory or ID"));
        return checkPlugin(words.at(1), json);
    }
    if (action == QLatin1String("run")) {
        if (words.size() != 4 || words.at(1).isEmpty() || words.at(2).isEmpty()
            || words.at(3).isEmpty())
            return Outcome::wrong(QStringLiteral(
                "plugin run: say PLUGIN ACTION DOCUMENT --out PATH"));
        return runPlugin(words, parser);
    }
    return Outcome::wrong(QStringLiteral("plugin: list, check or run — not %1").arg(action));
}

} // namespace cli
} // namespace omapixel
