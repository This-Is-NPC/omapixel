#pragma once

#include "Document.h"

#include <QObject>
#include <QStringList>
#include <QVariantList>

namespace omapixel {

/// The core's Document, made visible to QML.
///
/// It owns no rules. Every method here is a line or two of adapting types and
/// then a call into `Document` or `ops::`, and that is the point: the studio and
/// the CLI reach the same functions, so a behaviour cannot be right in one and
/// wrong in the other. The moment a rule starts living in this file, the C++
/// rewrite has stopped paying for itself.
///
/// The change signals are deliberately coarse. A pixel-level notification would
/// let QML repaint less, but the drawing surface repaints from one image
/// anyway, and a fine-grained signal graph is where staleness bugs breed.
class DocumentModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int columns READ columns NOTIFY changed)
    Q_PROPERTY(int rows READ rows NOTIFY changed)
    Q_PROPERTY(QStringList clipNames READ clipNames NOTIFY changed)
    Q_PROPERTY(QVariantList palette READ palette NOTIFY changed)

    Q_PROPERTY(QString clip READ clip WRITE setClip NOTIFY viewChanged)
    Q_PROPERTY(int frame READ frame WRITE setFrame NOTIFY viewChanged)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY changed)
    Q_PROPERTY(int fps READ fps NOTIFY changed)

    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY fileChanged)
    Q_PROPERTY(bool dirty READ dirty NOTIFY fileChanged)
    Q_PROPERTY(QString note READ note NOTIFY noteChanged)

    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)

public:
    explicit DocumentModel(QObject *parent = nullptr);

    /// The document itself, for the painter. Handed out const so nothing can
    /// edit around the signals.
    const Document &document() const { return m_document; }

    int columns() const { return m_document.columns(); }
    int rows() const { return m_document.rows(); }
    QStringList clipNames() const { return m_document.clipNames(); }
    QVariantList palette() const;

    QString clip() const { return m_clip; }
    void setClip(const QString &clip);
    int frame() const { return m_frame; }
    void setFrame(int frame);
    int frameCount() const;
    int fps() const;

    QString path() const { return m_path; }
    void setPath(const QString &path);
    bool dirty() const { return m_dirty; }
    QString note() const { return m_note; }

    // ------------------------------------------------------------- history
    //
    // Whole-document snapshots, not a log of inverse operations. A document is
    // small -- twelve frames of 160x90 is a third of a megabyte -- and an undo
    // assembled from inverse operations has to be written correctly once per
    // operation, including every operation added later. This one cannot be
    // wrong about an operation it has never heard of, and its memory is
    // bounded. If documents ever grow past what that costs, the thing to
    // change is the snapshot, not the guarantee.

    bool canUndo() const { return !m_undo.isEmpty(); }
    bool canRedo() const { return !m_redo.isEmpty(); }
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    /// A drag is one undo step, not one per pixel it crossed. The surface says
    /// where a stroke begins and ends; the snapshot is taken at the first
    /// change inside it, so a stroke that draws nothing costs nothing.
    Q_INVOKABLE void beginStroke();
    Q_INVOKABLE void endStroke();

    // ---------------------------------------------------------------- drawing

    Q_INVOKABLE QString slotAt(int x, int y) const;

    /// A colour that will be seen against the pixel at (x, y).
    ///
    /// Roughly the inverse of what is there, which is what the eye expects of a
    /// cursor, with one correction: the inverse of a mid grey is another mid
    /// grey, and a cursor drawn in it disappears exactly where the drawing is
    /// busiest. When inverting does not move far enough in brightness, this
    /// goes to black or white instead.
    ///
    /// Returns an invalid colour for an empty pixel: what shows through there
    /// is the chequerboard, which belongs to the window and not the document.
    Q_INVOKABLE QColor contrastAt(int x, int y) const;

    /// The colour a slot draws in, for showing a swatch beside its letter.
    /// Invalid for a slot the palette does not define, which is not an error:
    /// a hand-written file may use one, and it reads as empty until it is.
    Q_INVOKABLE QColor colourOf(const QString &slot) const;

    /// Colours matching `query`, as {name, colour} pairs.
    ///
    /// Searching by name is the point. Somebody who wants "a teal" does not
    /// want to compose one out of three numbers, and a spectrum is only useful
    /// once you already know roughly where you are going. The names are Qt's,
    /// which are the SVG ones -- a hundred and forty-eight colours everybody
    /// has already agreed on.
    ///
    /// A query that parses as a colour comes back first, under its own name, so
    /// typing a hex works without a separate field for it.
    Q_INVOKABLE QVariantList findColours(const QString &query) const;

    /// A colour drawn out of the whole of RGB, not out of the palette.
    ///
    /// Genuinely random, which is the point: picking from what is already in
    /// the document would be a shuffle, and a shuffle is not a gamble.
    Q_INVOKABLE QString randomColour() const;

    /// A slot letter nothing in the palette is using, or "" if there is none.
    ///
    /// A slot is one character -- that is the format, not a limit somebody
    /// chose -- but one character is far more than the letters and digits.
    /// Every printable character works, and so does most of Latin-1, which puts
    /// the ceiling in the high two hundreds rather than at sixty-two.
    Q_INVOKABLE QString freeSlot() const;

    /// How many pixels in the whole document draw with this slot.
    Q_INVOKABLE int countSlot(const QString &slot) const;

    /// Repaints every pixel of `fromSlot` in `hex`, everywhere.
    ///
    /// By moving those pixels onto a slot that holds the new colour, not by
    /// recolouring the slot they were on. The two look identical here and are
    /// not: recolouring changes every pixel that refers to that slot, and
    /// "every pixel of this colour" is what was asked for -- if some other
    /// part of the drawing shares the slot on purpose, it keeps its colour.
    ///
    /// The old slot is dropped from the palette if nothing is left using it, so
    /// replacing a colour repeatedly does not silt the palette up with entries
    /// no pixel refers to.
    Q_INVOKABLE void replaceColour(const QString &fromSlot, const QString &hex);
    Q_INVOKABLE void paint(int x, int y, const QString &slot);
    Q_INVOKABLE void line(int x0, int y0, int x1, int y1, const QString &slot);
    Q_INVOKABLE void rect(int x0, int y0, int x1, int y1, const QString &slot,
                          bool filled);
    Q_INVOKABLE void fill(int x, int y, const QString &slot);
    Q_INVOKABLE void clearFrame();
    Q_INVOKABLE void shift(int dx, int dy);
    Q_INVOKABLE void flip(const QString &axis);

    // ------------------------------------------------------------ structure

    Q_INVOKABLE void addClip(const QString &name);
    Q_INVOKABLE void removeClip(const QString &name);
    Q_INVOKABLE void renameClip(const QString &from, const QString &to);
    Q_INVOKABLE void setFps(int fps);

    Q_INVOKABLE void addFrame(bool duplicate);
    Q_INVOKABLE void removeFrame();
    Q_INVOKABLE void moveFrame(int step);

    Q_INVOKABLE int wouldLose(int columns, int rows) const;
    Q_INVOKABLE void resize(int columns, int rows);
    Q_INVOKABLE void reset(int columns, int rows);

    Q_INVOKABLE void setPaletteColour(const QString &slot, const QString &colour);
    Q_INVOKABLE QVariantList sizePresets() const;

    // ---------------------------------------------------------------- files

    /// Puts a line on the status bar. Public because the window has things to
    /// report that the model cannot know about -- a key that named no slot,
    /// for one -- and they belong on the same line as everything else.
    Q_INVOKABLE void say(const QString &note);

    Q_INVOKABLE bool open(const QString &path);
    Q_INVOKABLE bool save(const QString &path = QString());

    /// Writes a PNG of the open clip. `sheet` lays every frame side by side
    /// rather than writing the one on screen.
    ///
    /// Exporting belongs here rather than in the window because it is the same
    /// operation the command line performs, through the same `render::` call --
    /// a picture that comes out of the studio and a picture that comes out of
    /// `omapixel render` have to be the same picture.
    Q_INVOKABLE bool exportImage(const QString &path, int scale, bool sheet,
                                 bool checker);

    /// A path to offer when exporting: the document's own, with the suffix
    /// swapped. Guessing this saves the one piece of typing nobody wants to do.
    Q_INVOKABLE QString suggestedExportPath(bool sheet) const;

signals:
    /// The document's contents changed: repaint, relist, everything.
    void changed();
    /// Only which clip or frame is being looked at changed.
    void viewChanged();
    void fileChanged();
    void noteChanged();
    void historyChanged();

private:
    /// Fetches the open frame, hands it to `edit`, stores it back. Every
    /// drawing command is the same three steps, and writing them out at each
    /// call site is how one of them eventually forgets the store.
    void editFrame(const std::function<void(Grid &)> &edit);
    QChar slotOf(const QString &text) const;

    /// Files `before` as the state undo returns to, and drops the redo branch.
    /// Editing after undoing abandons what was undone -- which is what every
    /// editor does, and the only rule that keeps the two stacks meaningful.
    void remember(const Document &before);
    /// Keeps the open clip and frame inside a document that just changed shape
    /// underneath them, which undo can do in one step.
    void reseat();

    /// Deep enough to cover a session's worth of mistakes, shallow enough that
    /// the snapshots cannot quietly eat a workstation.
    static constexpr int HistoryDepth = 80;

    Document m_document;
    QList<Document> m_undo;
    QList<Document> m_redo;
    bool m_stroke = false;
    bool m_strokeRemembered = false;
    QString m_clip;
    int m_frame = 0;
    QString m_path;
    bool m_dirty = false;
    QString m_note;
};

} // namespace omapixel
