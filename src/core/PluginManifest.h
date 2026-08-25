#pragma once

#include <QString>
#include <QStringList>
#include <QList>

namespace omapixel {

class PluginManifest
{
public:
    struct Action {
        QString name;
        QString kind;
    };

    struct Result;

    static Result read(const QString &rootPath);

    QString rootPath;
    QString id;
    QString name;
    QString version;
    QString executable;
    QList<Action> actions;
};

struct PluginManifest::Result {
    PluginManifest manifest;
    bool ok = false;
    QString error;

    explicit operator bool() const { return ok; }
};

} // namespace omapixel
