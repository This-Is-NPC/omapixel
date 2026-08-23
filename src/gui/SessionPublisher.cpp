#include "SessionPublisher.h"

#include "Sessions.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

namespace omapixel {

SessionPublisher::SessionPublisher(QObject *parent) : QObject(parent) {}

void SessionPublisher::follow(const DocumentModel *model)
{
    m_model = model;
    connect(m_model, &DocumentModel::fileChanged, this, &SessionPublisher::write);
    connect(m_model, &DocumentModel::selectionChanged, this,
            &SessionPublisher::write);
    write();
}

void SessionPublisher::retire()
{
    const QString dir = sessions::directory();
    if (!dir.isEmpty())
        QFile::remove(dir + QStringLiteral("/%1.json")
                              .arg(QCoreApplication::applicationPid()));
}

void SessionPublisher::write()
{
    const QString dir = sessions::directory();
    if (dir.isEmpty() || !m_model)
        return;
    QDir().mkpath(dir);

    // Absolute, so `omapixel where heart.json` matches from any working
    // directory. An untitled window advertises its scratch backing -- the
    // whole point of the file -- and stays an empty string when there is
    // nothing to advertise. Renaming over the target keeps a reader -- the
    // very agents this file serves -- off a half-written document.
    const QString advertised = m_model->followedPath();
    QJsonObject session;
    session.insert(QStringLiteral("pid"), QCoreApplication::applicationPid());
    session.insert(QStringLiteral("started"),
                   sessions::startTimeOf(QCoreApplication::applicationPid()));
    session.insert(QStringLiteral("path"),
                   advertised.isEmpty()
                       ? QString()
                       : QFileInfo(advertised).absoluteFilePath());
    session.insert(QStringLiteral("dirty"), m_model->dirty());
    if (m_model->hasSelection()) {
        QJsonObject selection;
        selection.insert(QStringLiteral("clip"), m_model->clip());
        selection.insert(QStringLiteral("frame"), m_model->frame());
        selection.insert(QStringLiteral("x"), m_model->selectionX());
        selection.insert(QStringLiteral("y"), m_model->selectionY());
        selection.insert(QStringLiteral("width"), m_model->selectionWidth());
        selection.insert(QStringLiteral("height"), m_model->selectionHeight());
        selection.insert(QStringLiteral("count"), m_model->selectionCount());
        session.insert(QStringLiteral("selection"), selection);
    } else {
        session.insert(QStringLiteral("selection"), QJsonValue::Null);
    }

    QSaveFile file(dir + QStringLiteral("/%1.json")
                           .arg(QCoreApplication::applicationPid()));
    if (!file.open(QIODevice::WriteOnly))
        return;
    file.write(QJsonDocument(session).toJson(QJsonDocument::Compact));
    file.write("\n");
    file.commit();
}

} // namespace omapixel
