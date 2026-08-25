#include "PluginRegistry.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>

namespace omapixel {

namespace {

QString absolutePath(const QString &path)
{
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QFileInfoList childDirectories(const QString &path)
{
    QFileInfoList children = QDir(path).entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    std::sort(children.begin(), children.end(), [](const QFileInfo &left, const QFileInfo &right) {
        return left.fileName() < right.fileName();
    });
    return children;
}

} // namespace

PluginRegistry PluginRegistry::discover()
{
    PluginRegistry registry;

    const QString configured = QString::fromLocal8Bit(qgetenv("OMAPIXEL_PLUGIN_PATH"));
    if (!configured.isEmpty()) {
        const QStringList paths = configured.split(QDir::listSeparator(), Qt::KeepEmptyParts);
        for (const QString &path : paths) {
            if (path.isEmpty()) {
                registry.addDiagnostic(QStringLiteral("OMAPIXEL_PLUGIN_PATH"),
                                       QStringLiteral("contains an empty entry"));
                continue;
            }
            registry.scanContainer(absolutePath(path), true);
        }
    }

    QString dataHome = QString::fromLocal8Bit(qgetenv("XDG_DATA_HOME"));
    if (dataHome.isEmpty()) {
        const QString home = QString::fromLocal8Bit(qgetenv("HOME"));
        if (!home.isEmpty())
            dataHome = QDir(home).filePath(QStringLiteral(".local/share"));
    }
    if (!dataHome.isEmpty())
        registry.scanContainer(QDir(dataHome).filePath(QStringLiteral("omapixel/plugins")), false);

    std::sort(registry.m_plugins.begin(), registry.m_plugins.end(),
              [](const PluginManifest &left, const PluginManifest &right) {
                  return left.id < right.id;
              });
    return registry;
}

void PluginRegistry::scanContainer(const QString &path, bool reportEmpty)
{
    const QFileInfo container(path);
    if (!container.exists() || !container.isDir()) {
        if (reportEmpty)
            addDiagnostic(path, QStringLiteral("plugin discovery root is not a directory"));
        return;
    }

    const QString manifest = QDir(path).filePath(QStringLiteral("omapixel-plugin.json"));
    if (QFileInfo::exists(manifest)) {
        addCandidate(path);
        return;
    }

    const QFileInfoList children = childDirectories(path);
    if (children.isEmpty()) {
        if (reportEmpty)
            addDiagnostic(path, QStringLiteral("plugin manifest is missing"));
        return;
    }
    for (const QFileInfo &child : children)
        addCandidate(child.absoluteFilePath());
}

void PluginRegistry::addCandidate(const QString &path)
{
    const QString root = absolutePath(path);
    const PluginManifest::Result result = PluginManifest::read(root);
    if (!result) {
        addDiagnostic(root, result.error);
        return;
    }

    if (find(result.manifest.id)) {
        addDiagnostic(root, QStringLiteral("duplicate plugin id `%1`; earlier valid occurrence wins")
                              .arg(result.manifest.id));
        return;
    }
    m_plugins.append(result.manifest);
}

void PluginRegistry::addDiagnostic(const QString &path, const QString &message)
{
    m_diagnostics.append({path, message});
}

const PluginManifest *PluginRegistry::find(const QString &id) const
{
    for (const PluginManifest &plugin : m_plugins) {
        if (plugin.id == id)
            return &plugin;
    }
    return nullptr;
}

} // namespace omapixel
