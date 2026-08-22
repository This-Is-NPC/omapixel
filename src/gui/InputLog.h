#pragma once

#include <QObject>

namespace omapixel {

/// Reports pointer input, from both sides of the QML boundary.
///
/// Written after several rounds of chasing "scrolling does not work". The
/// lesson that earned this file: an event that never reaches the window, an
/// event that reaches it and is ignored, and an event that is handled while the
/// *logging* is broken all look identical from the outside. So this logs at two
/// points -- as the event enters the window, below QML, and from the handler
/// that acts on it -- and it writes straight to stderr rather than through
/// `console.log`, whose output depends on logging categories that can be off.
///
/// Enabled by OMAPIXEL_DEBUG_INPUT. Off, it costs one branch per event.
class InputLog : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled CONSTANT)

public:
    explicit InputLog(bool enabled, QObject *parent = nullptr);

    bool enabled() const { return m_enabled; }

    /// Called from QML, so a handler can say that it ran and what it decided.
    Q_INVOKABLE void say(const QString &line) const;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    bool m_enabled = false;
};

} // namespace omapixel
