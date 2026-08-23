#include "Differences.h"

#include "Strings.h"

namespace omapixel {

// The `clip[i] (a/b)` label inside several lines is composed here rather than
// translated: it is an identifier saying which entry of which list, not a
// sentence. Everything around it speaks through the catalogue.
QStringList documentDifferences(const Document &left, const Document &right,
                                const QString &leftLabel, const QString &rightLabel)
{
    QStringList differences;
    if (left.columns() != right.columns() || left.rows() != right.rows()) {
        differences << Strings::shared().t(QStringLiteral("diff.size"))
                           .arg(leftLabel)
                           .arg(left.columns())
                           .arg(left.rows())
                           .arg(rightLabel)
                           .arg(right.columns())
                           .arg(right.rows());
    }

    const QList<Palette::Slot> &leftPalette = left.palette().entries();
    const QList<Palette::Slot> &rightPalette = right.palette().entries();
    if (leftPalette.size() != rightPalette.size()) {
        differences << Strings::shared().t(QStringLiteral("diff.palette.count"))
                           .arg(leftLabel)
                           .arg(leftPalette.size())
                           .arg(rightLabel)
                           .arg(rightPalette.size());
    }
    const int paletteCount = qMin(leftPalette.size(), rightPalette.size());
    for (int i = 0; i < paletteCount; ++i) {
        const Palette::Slot &mine = leftPalette.at(i);
        const Palette::Slot &theirs = rightPalette.at(i);
        if (mine.letter != theirs.letter) {
            differences << Strings::shared().t(QStringLiteral("diff.palette.order"))
                               .arg(i)
                               .arg(QString(mine.letter))
                               .arg(leftLabel)
                               .arg(QString(theirs.letter))
                               .arg(rightLabel);
        } else if (mine.colour != theirs.colour) {
            differences << Strings::shared().t(QStringLiteral("diff.palette.colour"))
                               .arg(i)
                               .arg(QString(mine.letter))
                               .arg(mine.colour.name(QColor::HexRgb).toUpper())
                               .arg(leftLabel)
                               .arg(theirs.colour.name(QColor::HexRgb).toUpper())
                               .arg(rightLabel);
        }
    }
    for (int i = paletteCount; i < leftPalette.size(); ++i) {
        differences << Strings::shared().t(QStringLiteral("diff.palette.onlyIn"))
                           .arg(i)
                           .arg(leftLabel)
                           .arg(QString(leftPalette.at(i).letter));
    }
    for (int i = paletteCount; i < rightPalette.size(); ++i) {
        differences << Strings::shared().t(QStringLiteral("diff.palette.onlyIn"))
                           .arg(i)
                           .arg(rightLabel)
                           .arg(QString(rightPalette.at(i).letter));
    }

    const QList<Clip> &leftClips = left.clips();
    const QList<Clip> &rightClips = right.clips();
    if (leftClips.size() != rightClips.size()) {
        differences << Strings::shared().t(QStringLiteral("diff.clips.count"))
                           .arg(leftLabel)
                           .arg(leftClips.size())
                           .arg(rightLabel)
                           .arg(rightClips.size());
    }
    const int clipCount = qMin(leftClips.size(), rightClips.size());
    for (int i = 0; i < clipCount; ++i) {
        const Clip &mine = leftClips.at(i);
        const Clip &theirs = rightClips.at(i);
        if (mine.name != theirs.name) {
            differences << Strings::shared().t(QStringLiteral("diff.clips.order"))
                               .arg(i)
                               .arg(mine.name)
                               .arg(leftLabel)
                               .arg(theirs.name)
                               .arg(rightLabel);
        }
        const QString clipLabel = QStringLiteral("clip[%1] (%2/%3)")
                                      .arg(i)
                                      .arg(mine.name)
                                      .arg(theirs.name);
        if (mine.fps != theirs.fps) {
            differences << Strings::shared().t(QStringLiteral("diff.clip.fps"))
                               .arg(clipLabel)
                               .arg(mine.fps)
                               .arg(leftLabel)
                               .arg(theirs.fps)
                               .arg(rightLabel);
        }
        if (mine.frames.size() != theirs.frames.size()) {
            differences << Strings::shared().t(QStringLiteral("diff.clip.frameCount"))
                               .arg(clipLabel)
                               .arg(mine.frames.size())
                               .arg(leftLabel)
                               .arg(theirs.frames.size())
                               .arg(rightLabel);
        }

        const int frameCount = qMin(mine.frames.size(), theirs.frames.size());
        for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
            const Grid &mineFrame = mine.frames.at(frameIndex);
            const Grid &theirFrame = theirs.frames.at(frameIndex);
            if (mineFrame.columns() != theirFrame.columns()
                || mineFrame.rows() != theirFrame.rows()) {
                differences
                    << Strings::shared().t(QStringLiteral("diff.clip.frameDimensions"))
                           .arg(clipLabel)
                           .arg(frameIndex)
                           .arg(mineFrame.columns())
                           .arg(mineFrame.rows())
                           .arg(leftLabel)
                           .arg(theirFrame.columns())
                           .arg(theirFrame.rows())
                           .arg(rightLabel);
            }

            int pixels = 0;
            const int columns = qMax(mineFrame.columns(), theirFrame.columns());
            const int rows = qMax(mineFrame.rows(), theirFrame.rows());
            for (int y = 0; y < rows; ++y) {
                for (int x = 0; x < columns; ++x) {
                    if (mineFrame.at(x, y) != theirFrame.at(x, y))
                        ++pixels;
                }
            }
            if (pixels > 0) {
                differences << Strings::shared().t(QStringLiteral("diff.clip.pixelsDiffer"))
                                   .arg(clipLabel)
                                   .arg(frameIndex)
                                   .arg(pixels);
            }
        }
    }
    for (int i = clipCount; i < leftClips.size(); ++i) {
        differences << Strings::shared().t(QStringLiteral("diff.clips.onlyIn"))
                           .arg(i)
                           .arg(leftLabel)
                           .arg(leftClips.at(i).name);
    }
    for (int i = clipCount; i < rightClips.size(); ++i) {
        differences << Strings::shared().t(QStringLiteral("diff.clips.onlyIn"))
                           .arg(i)
                           .arg(rightLabel)
                           .arg(rightClips.at(i).name);
    }
    return differences;
}

QStringList summarizeDifferences(const QStringList &lines, int max)
{
    if (lines.size() <= max)
        return lines;
    QStringList kept = lines.mid(0, max);
    // The tail is counted rather than hidden: the sentence says there is
    // more behind it, and says how much.
    kept << Strings::shared().t(QStringLiteral("note.moreDifferences"))
                .arg(lines.size() - max);
    return kept;
}

} // namespace omapixel
