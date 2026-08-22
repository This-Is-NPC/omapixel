#pragma once

#include "Grid.h"
#include "Palette.h"

#include <QList>
#include <QString>

namespace omapixel {

/// One animation: a name, a speed, and its frames.
struct Clip {
    QString name;
    int fps = 8;
    QList<Grid> frames;
};

/// A palette and a handful of animations, all at one size.
///
/// The size belongs to the DOCUMENT and not to each clip: a document is a set
/// of drawings of the same thing, and a clip of another size in the middle of
/// it is another document. Whoever wants a 16px icon and a 128px backdrop wants
/// two files and will be happier with two.
///
/// Every mutation lives here, and there is exactly one implementation of each.
/// That is the whole reason this is C++ and not two programs: before, `resize`
/// existed once in QML and once in Python, with a comment in each pointing at
/// the other. The CLI and the studio now call the same function or they do not
/// call one at all.
class Document
{
public:
    Document() = default;
    static Document blank(int columns, int rows);

    /// A document with no clips at all, for a reader that is about to add
    /// them. Every other way in keeps at least one clip; this one exists
    /// because a reader has to be able to drop the placeholder before it knows
    /// what the file contains, and `removeClip` will not take the last one.
    /// Whoever calls it owns the gap: add a clip, or fall back to `blank`.
    static Document empty(int columns, int rows);

    int columns() const { return m_columns; }
    int rows() const { return m_rows; }

    const Palette &palette() const { return m_palette; }
    Palette &palette() { return m_palette; }

    const QList<Clip> &clips() const { return m_clips; }
    QStringList clipNames() const;

    /// -1 when there is no clip by that name. Callers check; nothing here
    /// throws, because a CLI has to turn a bad name into a message and not a
    /// crash.
    int indexOfClip(const QString &name) const;
    const Clip *clip(const QString &name) const;
    Clip *clip(const QString &name);

    /// The frame, or a null Grid when the clip or the index is unknown.
    Grid frame(const QString &clip, int index) const;
    bool setFrame(const QString &clip, int index, const Grid &grid);

    // ------------------------------------------------------------- the clips

    bool addClip(const QString &name, int fps = 8);
    bool removeClip(const QString &name);
    /// Keeps the clip where it was in the list. Deleting and reinserting sends
    /// it to the end, and the list is the sidebar -- a clip that jumps position
    /// when renamed looks like another clip.
    bool renameClip(const QString &from, const QString &to);
    bool setFps(const QString &name, int fps);

    // ------------------------------------------------------------ the frames

    bool addFrame(const QString &clip, int after, bool duplicate);
    bool removeFrame(const QString &clip, int index);
    bool moveFrame(const QString &clip, int index, int to);

    // -------------------------------------------------------------- the size

    /// How many drawn pixels shrinking to this size would lose. Zero means the
    /// resize is free of consequence; any other number is what a confirmation
    /// has to say before it happens.
    int wouldLose(int columns, int rows) const;

    /// Resizes every frame of every clip, keeping the drawing CENTRED. What is
    /// being resized is almost always a figure, and a figure has a centre. A
    /// clip left behind would produce a document with frames of two sizes,
    /// which is a file nothing can draw.
    void resize(int columns, int rows);

    /// Swaps one slot for another in every frame of every clip, and says how
    /// many pixels changed.
    int replaceSlot(QChar from, QChar to);

    /// The same, in one frame only.
    ///
    /// Both exist because both are wanted and neither is the obvious default.
    /// Recolouring one frame of an animation leaves it flickering between two
    /// colours; recolouring all twelve when you meant to fix one is a bigger
    /// mistake and a quieter one. The caller says which.
    int replaceSlotInFrame(const QString &clip, int frame, QChar from, QChar to);

    /// Whether any pixel anywhere still draws with this slot.
    bool usesSlot(QChar slot) const;

    // ------------------------------------------------------------- integrity

    /// What stops the document from being drawn, in plain sentences. Returns
    /// rather than throws: a document with a problem has to open so it can be
    /// fixed.
    QStringList problems() const;

    bool operator==(const Document &other) const;

private:
    int m_columns = 0;
    int m_rows = 0;
    Palette m_palette;
    QList<Clip> m_clips;
};

} // namespace omapixel
