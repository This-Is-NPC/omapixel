#include "TextSafety.h"

#include <QChar>

namespace omapixel {
namespace text {

bool isUnsafe(uint codepoint)
{
    const bool nonCharacter = codepoint <= 0x10ffff
        && ((codepoint & 0xffff) >= 0xfffe
            || (codepoint >= 0xfdd0 && codepoint <= 0xfdef));
    const QChar::Category category = QChar::category(codepoint);
    return nonCharacter || category == QChar::Other_Control
        || category == QChar::Other_Format
        || category == QChar::Separator_Line
        || category == QChar::Separator_Paragraph
        || category == QChar::Other_Surrogate
        || category == QChar::Other_NotAssigned;
}

bool isSafe(const QString &value, bool emptyAllowed)
{
    if (!emptyAllowed && value.isEmpty())
        return false;
    for (int index = 0; index < value.size(); ++index) {
        const QChar character = value.at(index);
        if (character.isHighSurrogate() && index + 1 < value.size()
            && value.at(index + 1).isLowSurrogate()) {
            if (isUnsafe(QChar::surrogateToUcs4(character, value.at(index + 1))))
                return false;
            ++index;
            continue;
        }
        if (isUnsafe(character.unicode()))
            return false;
    }
    return true;
}

QString escapeForTerminal(const QString &value)
{
    QString escaped;
    escaped.reserve(value.size());
    for (int index = 0; index < value.size(); ++index) {
        const QChar character = value.at(index);
        uint codepoint = character.unicode();
        if (character.isHighSurrogate() && index + 1 < value.size()
            && value.at(index + 1).isLowSurrogate()) {
            codepoint = QChar::surrogateToUcs4(character, value.at(++index));
        }
        if (isUnsafe(codepoint)) {
            if (codepoint <= 0xffff) {
                escaped += QStringLiteral("\\u%1")
                    .arg(codepoint, 4, 16, QLatin1Char('0'));
            } else {
                escaped += QStringLiteral("\\U%1")
                    .arg(codepoint, 8, 16, QLatin1Char('0'));
            }
        } else if (codepoint <= 0xffff) {
            escaped += QChar(ushort(codepoint));
        } else {
            escaped += QChar::highSurrogate(codepoint);
            escaped += QChar::lowSurrogate(codepoint);
        }
    }
    return escaped;
}

} // namespace text
} // namespace omapixel
