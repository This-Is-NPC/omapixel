#include "InputLog.h"

#include <QEvent>
#include <QNativeGestureEvent>
#include <QWheelEvent>

#include <cstdio>

namespace omapixel {

InputLog::InputLog(bool enabled, QObject *parent) : QObject(parent), m_enabled(enabled)
{
}

void InputLog::say(const QString &line) const
{
    if (!m_enabled)
        return;
    std::fprintf(stderr, "qml    %s\n", qPrintable(line));
    std::fflush(stderr);
}

bool InputLog::eventFilter(QObject *watched, QEvent *event)
{
    if (!m_enabled)
        return QObject::eventFilter(watched, event);

    switch (event->type()) {
    case QEvent::Wheel: {
        auto *wheel = static_cast<QWheelEvent *>(event);
        // The position matters as much as the delta: a wheel over a rail is
        // meant to scroll the rail, and looks identical in a log that records
        // only deltas.
        std::fprintf(stderr,
                     "wheel  at %4.0f,%-4.0f angle %d,%d  pixel %d,%d  "
                     "modifiers 0x%x  phase %d\n",
                     wheel->position().x(), wheel->position().y(),
                     wheel->angleDelta().x(), wheel->angleDelta().y(),
                     wheel->pixelDelta().x(), wheel->pixelDelta().y(),
                     int(wheel->modifiers()), int(wheel->phase()));
        break;
    }
    case QEvent::NativeGesture: {
        auto *gesture = static_cast<QNativeGestureEvent *>(event);
        std::fprintf(stderr, "gesture   type %d  value %.3f\n",
                     int(gesture->gestureType()), gesture->value());
        break;
    }
    default:
        break;
    }
    std::fflush(stderr);
    return QObject::eventFilter(watched, event);
}

} // namespace omapixel
