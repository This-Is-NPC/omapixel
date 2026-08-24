#include "InputLog.h"

#include "TextSafety.h"

#include <QEvent>
#include <QNativeGestureEvent>
#include <QKeyEvent>
#include <QQuickItem>
#include <QQuickWindow>
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
    std::fprintf(stderr, "qml    %s\n", qPrintable(text::escapeForTerminal(line)));
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
    case QEvent::KeyPress: {
        auto *key = static_cast<QKeyEvent *>(event);
        // Which item holds the keyboard matters as much as which key was
        // pressed: keys that reach the window and keys that reach the drawing
        // are different things, and "the arrows do nothing" is usually the
        // second one failing while the first works.
        QString holder = QStringLiteral("nobody");
        if (auto *quick = qobject_cast<QQuickWindow *>(watched)) {
            if (QQuickItem *focused = quick->activeFocusItem()) {
                holder = QString::fromUtf8(focused->metaObject()->className());
                if (!focused->objectName().isEmpty())
                    holder += QStringLiteral(" (") + focused->objectName()
                              + QStringLiteral(")");
            }
        }
        std::fprintf(stderr, "key    0x%x  modifiers 0x%x  focus %s\n", key->key(),
                     int(key->modifiers()),
                     qPrintable(text::escapeForTerminal(holder)));
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
