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

    const QList<Layer> &leftLayers = left.layers();
    const QList<Layer> &rightLayers = right.layers();
    if (leftLayers.size() != rightLayers.size()) {
        differences << Strings::shared().t(QStringLiteral("diff.layers.count"))
                           .arg(leftLabel)
                           .arg(leftLayers.size())
                           .arg(rightLabel)
                           .arg(rightLayers.size());
    }
    const int layerCount = qMin(leftLayers.size(), rightLayers.size());
    for (int i = 0; i < layerCount; ++i) {
        const Layer &mine = leftLayers.at(i);
        const Layer &theirs = rightLayers.at(i);
        const QString layerLabel = QStringLiteral("layer[%1] (%2/%3, %4/%5)")
                                        .arg(i)
                                        .arg(mine.id, theirs.id)
                                        .arg(mine.name, theirs.name);
        if (mine.id != theirs.id || mine.name != theirs.name) {
            differences << Strings::shared().t(QStringLiteral("diff.layers.order"))
                               .arg(i)
                               .arg(mine.id + QStringLiteral("/") + mine.name)
                               .arg(leftLabel)
                               .arg(theirs.id + QStringLiteral("/") + theirs.name)
                               .arg(rightLabel);
        }
        if (mine.visible != theirs.visible || mine.locked != theirs.locked
            || mine.opacity != theirs.opacity || mine.mode != theirs.mode
            || mine.storage != theirs.storage) {
            differences << Strings::shared().t(QStringLiteral("diff.layer.metadata"))
                               .arg(layerLabel)
                               .arg(leftLabel)
                               .arg(rightLabel);
        }

        const int layerClipCount = qMin(left.clips().size(), right.clips().size());
        for (int clipIndex = 0; clipIndex < layerClipCount; ++clipIndex) {
            const Clip &leftClip = left.clips().at(clipIndex);
            const Clip &rightClip = right.clips().at(clipIndex);
            const int frameCount = qMin(leftClip.frameCount, rightClip.frameCount);
            for (int frameIndex = 0; frameIndex < frameCount; ++frameIndex) {
                const Grid mineGrid = left.cel(mine.id, leftClip.id, frameIndex);
                const Grid theirGrid = right.cel(theirs.id, rightClip.id, frameIndex);
                if (mineGrid.columns() != theirGrid.columns()
                    || mineGrid.rows() != theirGrid.rows()) {
                    differences
                        << Strings::shared().t(QStringLiteral("diff.layer.celDimensions"))
                               .arg(layerLabel)
                               .arg(leftClip.name)
                               .arg(frameIndex)
                               .arg(mineGrid.columns())
                               .arg(mineGrid.rows())
                               .arg(leftLabel)
                               .arg(theirGrid.columns())
                               .arg(theirGrid.rows())
                               .arg(rightLabel);
                }
                const int columns = qMax(mineGrid.columns(), theirGrid.columns());
                const int rows = qMax(mineGrid.rows(), theirGrid.rows());
                int pixels = 0;
                for (int y = 0; y < rows; ++y) {
                    for (int x = 0; x < columns; ++x) {
                        if (mineGrid.at(x, y) != theirGrid.at(x, y))
                            ++pixels;
                    }
                }
                if (pixels > 0) {
                    differences << Strings::shared().t(QStringLiteral("diff.layer.celPixels"))
                                       .arg(layerLabel)
                                       .arg(leftClip.name)
                                       .arg(frameIndex)
                                       .arg(pixels);
                }
            }
        }
    }
    for (int i = layerCount; i < leftLayers.size(); ++i) {
        differences << Strings::shared().t(QStringLiteral("diff.layers.onlyIn"))
                           .arg(i)
                           .arg(leftLabel)
                           .arg(leftLayers.at(i).id)
                           .arg(leftLayers.at(i).name);
    }
    for (int i = layerCount; i < rightLayers.size(); ++i) {
        differences << Strings::shared().t(QStringLiteral("diff.layers.onlyIn"))
                           .arg(i)
                           .arg(rightLabel)
                           .arg(rightLayers.at(i).id)
                           .arg(rightLayers.at(i).name);
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
        if (mine.frameCount != theirs.frameCount) {
            differences << Strings::shared().t(QStringLiteral("diff.clip.frameCount"))
                               .arg(clipLabel)
                            .arg(mine.frameCount)
                               .arg(leftLabel)
                            .arg(theirs.frameCount)
                               .arg(rightLabel);
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
