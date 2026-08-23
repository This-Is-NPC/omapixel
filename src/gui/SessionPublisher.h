#pragma once

#include "DocumentModel.h"

#include <QObject>
#include <QString>

namespace omapixel {

/// Publishes what this studio has open, as one file per process under
/// `sessions::directory()`, so an agent on the command line can see the
/// studio's side of the live loop before it writes: is this window on that
/// document, and does it hold unsaved work?
///
/// The selected rectangle includes its clip and frame so a caller never has to
/// guess which frame its coordinates describe. Caret and playback remain local.
///
/// The file contract itself lives in core (`sessions::`), because the CLI's
/// `where` reads the same directory: one description of a session, two front
/// ends, or a behaviour right in one and wrong in the other.
class SessionPublisher : public QObject
{
    Q_OBJECT

public:
    explicit SessionPublisher(QObject *parent = nullptr);

    /// Follows a model and keeps this process's session file current. The
    /// model announces file and selection changes through separate signals.
    void follow(const DocumentModel *model);

    /// Removes the session file. Called on clean exit; a studio that died
    /// without retiring leaves a file whose recorded start time stops
    /// matching its PID, which is exactly how `where` prunes it.
    void retire();

private:
    void write();

    const DocumentModel *m_model = nullptr;
};

} // namespace omapixel
