#pragma once

#include "Document.h"
#include "PaletteModel.h"

#include <QFileSystemWatcher>
#include <QRect>
#include <QObject>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace omapixel {

class ChangeLog;

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
    // The palette has its own signal because it has its own lifetime: it
    // changes when a colour is added or edited, and not when a pixel is
    // painted. Bound to `changed` it was rebuilt on every brush stroke, and
    // everything watching it -- a swatch per slot -- was destroyed and built
    // again. With a few hundred slots that is thousands of items per keypress.
    Q_PROPERTY(QVariantList palette READ palette NOTIFY paletteChanged)
    /// Bumped with every palette change, for bindings that call colourOf() and
    /// so cannot be tracked automatically.
    Q_PROPERTY(int paletteRevision READ paletteRevision NOTIFY paletteChanged)
    /// The same palette as a list model, for the view that draws one item per
    /// slot. Constant, because it is the same object throughout: what changes
    /// is its contents, row by row.
    Q_PROPERTY(QAbstractListModel *paletteModel READ paletteModel CONSTANT)
    /// What changed this session, studio hand and outside writes alike. A
    /// record, not a mechanism: restoring is undo's job alone. Constant for
    /// the same reason the palette model is; its rows change underneath.
    Q_PROPERTY(QAbstractListModel *changes READ changes CONSTANT)

    Q_PROPERTY(QString clip READ clip WRITE setClip NOTIFY viewChanged)
    Q_PROPERTY(int frame READ frame WRITE setFrame NOTIFY viewChanged)
    Q_PROPERTY(int frameCount READ frameCount NOTIFY changed)
    Q_PROPERTY(int fps READ fps NOTIFY changed)

    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)
    Q_PROPERTY(int selectionX READ selectionX NOTIFY selectionChanged)
    Q_PROPERTY(int selectionY READ selectionY NOTIFY selectionChanged)
    Q_PROPERTY(int selectionWidth READ selectionWidth NOTIFY selectionChanged)
    Q_PROPERTY(int selectionHeight READ selectionHeight NOTIFY selectionChanged)
    Q_PROPERTY(int selectionCount READ selectionCount NOTIFY selectionChanged)

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
    int paletteRevision() const { return m_paletteRevision; }
    QAbstractListModel *paletteModel() { return &m_paletteRows; }
    QAbstractListModel *changes() const;
    /// Whether the change that just landed came from outside -- a CLI write
    /// adopted from disk rather than the user's own hand. The change log
    /// reads this to label its entries; it is true only between an external
    /// adoption and the next `changed()` after it.
    bool lastChangeWasExternal() const { return m_lastChangeExternal; }

    QString clip() const { return m_clip; }
    void setClip(const QString &clip);
    int frame() const { return m_frame; }
    void setFrame(int frame);
    int frameCount() const;
    int fps() const;

    bool hasSelection() const { return m_selection.isValid(); }
    int selectionX() const { return m_selection.x(); }
    int selectionY() const { return m_selection.y(); }
    int selectionWidth() const { return hasSelection() ? m_selection.width() : 0; }
    int selectionHeight() const { return hasSelection() ? m_selection.height() : 0; }
    int selectionCount() const
    {
        return selectionWidth() * selectionHeight();
    }
    Q_INVOKABLE void setSelection(int x0, int y0, int x1, int y1);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE bool copySelection();
    Q_INVOKABLE bool pastePixels(int x, int y);

    QString path() const { return m_path; }
    void setPath(const QString &path);
    bool dirty() const { return m_dirty; }
    QString note() const { return m_note; }

    /// What the watcher follows and the session advertises: the open file,
    /// or -- for an untitled window -- its scratch backing file under the
    /// runtime directory. Empty when neither exists (scratch disabled, no
    /// sane runtime directory).
    ///
    /// A scratch backing is an ADDRESS, not a save: the window keeps saying
    /// unsaved, Ctrl+S keeps asking where, closing keeps asking. It exists
    /// so `omapixel where` can find the window and an agent can draw into
    /// it live.
    QString followedPath() const;
    /// True while the followed path is a scratch backing rather than a file
    /// the user named. Adoptions from disk never clear dirty in that state:
    /// tmpfs dying at logout is exactly the loss "unsaved" warns about.
    bool isScratchBacked() const;

    /// Removes the scratch backing file, if one exists. Called on clean exit.
    void retireScratch();

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

    /// How many pixels draw with this slot: in every frame of every clip, or
    /// in the open frame alone.
    Q_INVOKABLE int countSlot(const QString &slot, bool everywhere) const;

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
    /// `everywhere` decides the scope. Both are wanted and neither is the
    /// obvious default: recolouring one frame of an animation leaves it
    /// flickering, and recolouring all twelve when you meant one is a bigger
    /// mistake and a quieter one.
    Q_INVOKABLE void replaceColour(const QString &fromSlot, const QString &hex,
                                   bool everywhere);
    /// Paints the selected rectangle, or (x, y) when no range is selected.
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
    Q_INVOKABLE QVariantMap trimPreview() const;
    Q_INVOKABLE bool trim(bool anyway = false);
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

    /// Re-reads the file this model has open and adopts what is on disk.
    ///
    /// This is the half of the live loop the agent drives: a CLI write lands
    /// in the window with no user action. The decision is synchronous and
    /// public so tests can drive it directly; only the watcher wiring around
    /// it needs an async test.
    ///
    /// False means nothing was adopted -- the file failed to read (what is on
    /// screen stays, and `say` reports why), the content equals what is
    /// already here (our own save, a touch, the second half of a double
    /// fire), or a reload arrived mid-stroke and is queued for `endStroke`.
    Q_INVOKABLE bool reloadFromDisk();

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
    /// Raster content changed. An empty clip or negative frame invalidates all
    /// renderers; otherwise only items showing that frame need repainting.
    void renderChanged(const QString &clip, int frame);
    /// Only which clip or frame is being looked at changed.
    void viewChanged();
    void fileChanged();
    void noteChanged();
    void historyChanged();
    void paletteChanged();
    void selectionChanged();
    /// A different document was created or opened. Unlike fileChanged, this
    /// does not fire for edits, saves, or dirty-state changes.
    void documentReplaced();

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

    /// Points the watcher at the open file AND its directory. The directory
    /// half is not paranoia: a CLI write renames over the target, the inode
    /// changes, and without re-arming only the first external write would
    /// ever be seen.
    void watch();

    /// Creates and seeds the scratch backing for an untitled window, when
    /// the config allows it and there is somewhere sane to put it. Failure
    /// falls back quietly to the old invisibility rather than blocking
    /// startup over a convenience.
    void openScratch();

    /// Rewrites the scratch file with the current document. Used at seed
    /// time and after File > New; afterwards the file belongs to whoever
    /// writes it -- usually an agent -- and the watcher brings their writes
    /// back here.
    void writeScratchSeed();

    /// What an external write changed, as one status-bar line. The full walk
    /// comes from core's `documentDifferences`; the first few sentences are
    /// kept and the rest counted.
    QString describeDifferences(const Document &before) const;

    /// How far back undo goes. `history.depth` in the config file, because a
    /// snapshot is a whole document: deep enough to cover a session's worth of
    /// mistakes on a 32x24 sprite is not the same number as on a 512x512 one,
    /// and only the person drawing knows which they are doing.
    static int historyDepth();

    /// Announces a palette change, and keeps the revision counter honest.
    void paletteMoved();

    int m_paletteRevision = 0;
    PaletteModel m_paletteRows;
    ChangeLog *m_changes = nullptr;
    Document m_document;
    QList<Document> m_undo;
    QList<Document> m_redo;
    bool m_stroke = false;
    bool m_strokeRemembered = false;
    QString m_clip;
    int m_frame = 0;
    QRect m_selection;
    QString m_path;
    /// The untitled window's backing file under the runtime directory, when
    /// scratch is enabled and possible. Cleared the moment the document
    /// gains a real name.
    QString m_scratch;
    bool m_dirty = false;
    QString m_note;
    /// A reload that arrived while a stroke was live, waiting for
    /// `endStroke` to apply it. Swapping the document under a drag corrupts;
    /// deferring only surprises.
    bool m_reloadPending = false;
    QFileSystemWatcher m_watcher;
    /// True from the moment an external write is adopted until the change
    /// log has seen it. Every user-driven path clears it first.
    bool m_lastChangeExternal = false;
};

} // namespace omapixel
