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

    Q_INVOKABLE bool open(const QString &path);
    Q_INVOKABLE bool save(const QString &path = QString());

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
    void say(const QString &note);
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
