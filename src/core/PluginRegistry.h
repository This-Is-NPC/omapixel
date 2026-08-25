#pragma once

#include "PluginManifest.h"

#include <QList>

namespace omapixel {

class PluginRegistry
{
public:
    struct Diagnostic {
        QString path;
        QString message;
    };

    static PluginRegistry discover();

    const QList<PluginManifest> &plugins() const { return m_plugins; }
    const QList<Diagnostic> &diagnostics() const { return m_diagnostics; }
    const PluginManifest *find(const QString &id) const;

private:
    void scanContainer(const QString &path, bool reportEmpty);
    void addCandidate(const QString &path);
    void addDiagnostic(const QString &path, const QString &message);

    QList<PluginManifest> m_plugins;
    QList<Diagnostic> m_diagnostics;
};

} // namespace omapixel
