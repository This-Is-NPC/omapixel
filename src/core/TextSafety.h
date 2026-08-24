#pragma once

#include <QString>

namespace omapixel {
namespace text {

/// Unicode controls, format characters, and line separators are not safe in
/// names, protocol fields, or terminal diagnostics. Printable international
/// text remains valid, including supplementary code points.
bool isSafe(const QString &value, bool emptyAllowed = true);
bool isUnsafe(uint codepoint);
QString escapeForTerminal(const QString &value);

} // namespace text
} // namespace omapixel
