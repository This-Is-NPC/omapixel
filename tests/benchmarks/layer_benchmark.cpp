#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSet>
#include <QSysInfo>
#include <QThread>
#include <QVector>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace {

constexpr int Width = 329;
constexpr int Height = 480;
constexpr int Frames = 64;
constexpr int LayerCount = 5;
constexpr int PixelsPerCel = Width * Height;
constexpr int PaletteEntries = 6;
constexpr int PlaybackFps = 8;
constexpr int PlaybackSeconds = 60;
constexpr int PlaybackFrames = PlaybackFps * PlaybackSeconds;
constexpr int CompositionWarmup = 100;
constexpr int CompositionSamples = 200;
constexpr int HistorySteps = 80;

struct Pixel {
    quint8 red;
    quint8 green;
    quint8 blue;
    quint8 alpha;
};

enum class Mode { Normal, Multiply, Screen };

struct Layer {
    QString id;
    int opacity = 255;
    Mode mode = Mode::Normal;
    QVector<QSharedPointer<QByteArray>> cels;
    std::array<quint8, PaletteEntries + 1> sourceAlpha{};
    std::array<quint8, (PaletteEntries + 1) * 256> sourceRed{};
    std::array<quint8, (PaletteEntries + 1) * 256> sourceGreen{};
    std::array<quint8, (PaletteEntries + 1) * 256> sourceBlue{};
};

struct Document {
    QVector<Layer> layers;
};

struct Rgba {
    // Composition keeps channels premultiplied. Straight channels are only
    // reconstructed at the checksum boundary, avoiding three divisions for
    // every layer and pixel while preserving the contracted integer rules.
    quint8 red = 0;
    quint8 green = 0;
    quint8 blue = 0;
    quint8 alpha = 0;
};

const Pixel Palette[PaletteEntries] = {
    {24, 26, 34, 255},
    {230, 80, 91, 255},
    {57, 190, 180, 220},
    {244, 190, 70, 255},
    {110, 116, 235, 200},
    {245, 245, 245, 255},
};

int round255(int value)
{
    return (value + 127) / 255;
}

quint8 clampByte(int value);

struct CompositionTables {
    quint8 round[256][256] = {};
    quint8 multiply[256][256] = {};
    quint8 screen[256][256] = {};
    quint8 straight[256][256] = {};

    CompositionTables()
    {
        for (int first = 0; first < 256; ++first) {
            for (int second = 0; second < 256; ++second) {
                round[first][second] = static_cast<quint8>(round255(first * second));
                multiply[first][second] = round[first][second];
                screen[first][second] = static_cast<quint8>(
                    255 - round255((255 - first) * (255 - second)));
                straight[first][second] = second == 0
                    ? 0
                    : clampByte((first * 255 + second / 2) / second);
            }
        }
    }
};

const CompositionTables &compositionTables()
{
    static const CompositionTables tables;
    return tables;
}

quint8 clampByte(int value)
{
    return static_cast<quint8>(qBound(0, value, 255));
}

void prepareLayer(Layer &layer)
{
    const CompositionTables &tables = compositionTables();
    for (int paletteIndex = 1; paletteIndex <= PaletteEntries; ++paletteIndex) {
        const Pixel source = Palette[paletteIndex - 1];
        const int sourceAlpha = tables.round[source.alpha][layer.opacity];
        layer.sourceAlpha[paletteIndex] = static_cast<quint8>(sourceAlpha);
        for (int destination = 0; destination < 256; ++destination) {
            Pixel blended = source;
            if (layer.mode == Mode::Multiply) {
                blended.red = tables.multiply[source.red][destination];
                blended.green = tables.multiply[source.green][destination];
                blended.blue = tables.multiply[source.blue][destination];
            } else if (layer.mode == Mode::Screen) {
                blended.red = tables.screen[source.red][destination];
                blended.green = tables.screen[source.green][destination];
                blended.blue = tables.screen[source.blue][destination];
            }
            const int offset = paletteIndex * 256 + destination;
            layer.sourceRed[offset] = tables.round[blended.red][sourceAlpha];
            layer.sourceGreen[offset] = tables.round[blended.green][sourceAlpha];
            layer.sourceBlue[offset] = tables.round[blended.blue][sourceAlpha];
        }
    }
}

void sourceOver(Rgba &destination, const Layer &layer, int paletteIndex)
{
    const CompositionTables &tables = compositionTables();
    const int sourceAlpha = layer.sourceAlpha[paletteIndex];
    if (sourceAlpha == 0)
        return;

    const int destinationAlpha = destination.alpha;
    const int inverseSourceAlpha = 255 - sourceAlpha;
    const int outputAlpha = sourceAlpha
        + tables.round[destinationAlpha][inverseSourceAlpha];
    const int destinationRed = tables.straight[destination.red][destinationAlpha];
    const int destinationGreen = tables.straight[destination.green][destinationAlpha];
    const int destinationBlue = tables.straight[destination.blue][destinationAlpha];
    int offset = paletteIndex * 256 + destinationRed;
    const int outputRedPremultiplied = layer.sourceRed[offset]
        + tables.round[destination.red][inverseSourceAlpha];
    offset = paletteIndex * 256 + destinationGreen;
    const int outputGreenPremultiplied = layer.sourceGreen[offset]
        + tables.round[destination.green][inverseSourceAlpha];
    offset = paletteIndex * 256 + destinationBlue;
    const int outputBluePremultiplied = layer.sourceBlue[offset]
        + tables.round[destination.blue][inverseSourceAlpha];

    destination.red = static_cast<quint8>(outputRedPremultiplied);
    destination.green = static_cast<quint8>(outputGreenPremultiplied);
    destination.blue = static_cast<quint8>(outputBluePremultiplied);
    destination.alpha = clampByte(outputAlpha);
}

QSharedPointer<QByteArray> makeCel(int layer, int frame)
{
    QSharedPointer<QByteArray> cel(new QByteArray(PixelsPerCel, '\0'));
    for (int y = 0; y < Height; ++y) {
        for (int x = 0; x < Width; ++x) {
            const int pattern = (x * 3 + y * 5 + frame * 11 + layer * 17) % 29;
            if (pattern == 0 || (x + y + frame + layer) % 47 == 0)
                continue;
            (*cel)[y * Width + x] = static_cast<char>(1 + ((x + y * 2 + frame * 3 + layer) % 6));
        }
    }
    return cel;
}

Document makeDocument()
{
    Document document;
    const int opacities[] = {255, 192, 160, 224, 128};
    const Mode modes[] = {
        Mode::Normal, Mode::Multiply, Mode::Screen, Mode::Normal, Mode::Multiply,
    };
    for (int layerIndex = 0; layerIndex < LayerCount; ++layerIndex) {
        Layer layer;
        layer.id = QStringLiteral("layer-%1").arg(layerIndex + 1);
        layer.opacity = opacities[layerIndex];
        layer.mode = modes[layerIndex];
        layer.cels.reserve(Frames);
        for (int frame = 0; frame < Frames; ++frame)
            layer.cels.append(makeCel(layerIndex, frame));
        prepareLayer(layer);
        document.layers.append(layer);
    }
    return document;
}

void compose(const Document &document, int frame, QVector<Rgba> &output)
{
    std::fill(output.begin(), output.end(), Rgba{});
    for (const Layer &layer : document.layers) {
        const QByteArray &cel = *layer.cels.at(frame);
        for (int pixel = 0; pixel < PixelsPerCel; ++pixel) {
            const int paletteIndex = static_cast<unsigned char>(cel.at(pixel));
            if (paletteIndex == 0)
                continue;
            sourceOver(output[pixel], layer, paletteIndex);
        }
    }
}

quint64 checksum(const QVector<Rgba> &output)
{
    const CompositionTables &tables = compositionTables();
    quint64 result = 1469598103934665603ULL;
    for (const Rgba &pixel : output) {
        result ^= tables.straight[pixel.red][pixel.alpha];
        result *= 1099511628211ULL;
        result ^= tables.straight[pixel.green][pixel.alpha];
        result *= 1099511628211ULL;
        result ^= tables.straight[pixel.blue][pixel.alpha];
        result *= 1099511628211ULL;
        result ^= pixel.alpha;
        result *= 1099511628211ULL;
    }
    return result;
}

QString modeName(Mode mode)
{
    switch (mode) {
    case Mode::Normal: return QStringLiteral("normal");
    case Mode::Multiply: return QStringLiteral("multiply");
    case Mode::Screen: return QStringLiteral("screen");
    }
    return QStringLiteral("normal");
}

QString slotName(int paletteIndex)
{
    return QString(QChar('A' + paletteIndex));
}

QJsonDocument toV2Json(const Document &document)
{
    QJsonObject root;
    root.insert(QStringLiteral("version"), 2);
    root.insert(QStringLiteral("canvas"), QJsonObject{
        {QStringLiteral("width"), Width},
        {QStringLiteral("height"), Height},
    });

    QJsonArray palette;
    for (int index = 0; index < static_cast<int>(std::size(Palette)); ++index) {
        const Pixel colour = Palette[index];
        const QString rgba = QStringLiteral("#%1%2%3%4")
            .arg(colour.red, 2, 16, QLatin1Char('0'))
            .arg(colour.green, 2, 16, QLatin1Char('0'))
            .arg(colour.blue, 2, 16, QLatin1Char('0'))
            .arg(colour.alpha, 2, 16, QLatin1Char('0'))
            .toUpper();
        palette.append(QJsonObject{
            {QStringLiteral("slot"), slotName(index)},
            {QStringLiteral("colour"), rgba},
        });
    }
    root.insert(QStringLiteral("palette"), palette);
    root.insert(QStringLiteral("clips"), QJsonArray{
        QJsonObject{
            {QStringLiteral("id"), QStringLiteral("benchmark")},
            {QStringLiteral("name"), QStringLiteral("Benchmark")},
            {QStringLiteral("fps"), PlaybackFps},
            {QStringLiteral("frameCount"), Frames},
        },
    });

    QJsonArray layers;
    for (const Layer &layer : document.layers) {
        QJsonArray cels;
        for (int frame = 0; frame < Frames; ++frame) {
            const QByteArray &bytes = *layer.cels.at(frame);
            QJsonArray rows;
            for (int y = 0; y < Height; ++y) {
                QString row;
                row.reserve(Width);
                for (int x = 0; x < Width; ++x) {
                    const int paletteIndex = static_cast<unsigned char>(bytes.at(y * Width + x));
                    if (paletteIndex == 0)
                        row.append(QLatin1Char('.'));
                    else
                        row.append(slotName(paletteIndex - 1));
                }
                rows.append(row);
            }
            cels.append(QJsonObject{
                {QStringLiteral("clip"), QStringLiteral("benchmark")},
                {QStringLiteral("frame"), frame},
                {QStringLiteral("rows"), rows},
            });
        }
        layers.append(QJsonObject{
            {QStringLiteral("id"), layer.id},
            {QStringLiteral("name"), layer.id},
            {QStringLiteral("visible"), true},
            {QStringLiteral("locked"), false},
            {QStringLiteral("opacity"), layer.opacity},
            {QStringLiteral("mode"), modeName(layer.mode)},
            {QStringLiteral("storage"), QStringLiteral("animated")},
            {QStringLiteral("cels"), cels},
        });
    }
    root.insert(QStringLiteral("layers"), layers);
    return QJsonDocument(root);
}

bool fromV2Json(const QJsonDocument &document, Document *result, QString *error)
{
    if (!document.isObject()) {
        *error = QStringLiteral("root is not an object");
        return false;
    }
    const QJsonObject root = document.object();
    const QJsonObject canvas = root.value(QStringLiteral("canvas")).toObject();
    if (root.value(QStringLiteral("version")).toInt() != 2
        || canvas.value(QStringLiteral("width")).toInt() != Width
        || canvas.value(QStringLiteral("height")).toInt() != Height) {
        *error = QStringLiteral("canonical canvas or version mismatch");
        return false;
    }
    const QJsonArray layers = root.value(QStringLiteral("layers")).toArray();
    if (layers.size() != LayerCount) {
        *error = QStringLiteral("canonical layer count mismatch");
        return false;
    }

    Document loaded;
    for (const QJsonValue &layerValue : layers) {
        const QJsonObject layerObject = layerValue.toObject();
        Layer layer;
        layer.id = layerObject.value(QStringLiteral("id")).toString();
        layer.opacity = layerObject.value(QStringLiteral("opacity")).toInt();
        const QString mode = layerObject.value(QStringLiteral("mode")).toString();
        layer.mode = mode == QLatin1String("multiply")
            ? Mode::Multiply
            : mode == QLatin1String("screen") ? Mode::Screen : Mode::Normal;
        const QJsonArray cels = layerObject.value(QStringLiteral("cels")).toArray();
        if (cels.size() != Frames) {
            *error = QStringLiteral("canonical cel count mismatch");
            return false;
        }
        layer.cels.reserve(Frames);
        for (const QJsonValue &celValue : cels) {
            const QJsonArray rows = celValue.toObject().value(QStringLiteral("rows")).toArray();
            if (rows.size() != Height) {
                *error = QStringLiteral("canonical row count mismatch");
                return false;
            }
            QSharedPointer<QByteArray> bytes(new QByteArray(PixelsPerCel, '\0'));
            for (int y = 0; y < Height; ++y) {
                const QString row = rows.at(y).toString();
                if (row.size() != Width) {
                    *error = QStringLiteral("canonical row width mismatch");
                    return false;
                }
                for (int x = 0; x < Width; ++x) {
                    const QChar value = row.at(x);
                    (*bytes)[y * Width + x] = value == QLatin1Char('.')
                        ? '\0'
                        : static_cast<char>(value.unicode() - 'A' + 1);
                }
            }
            layer.cels.append(bytes);
        }
        prepareLayer(layer);
        loaded.layers.append(layer);
    }
    *result = loaded;
    return true;
}

qint64 highWaterMarkKiB()
{
    QFile status(QStringLiteral("/proc/self/status"));
    if (!status.open(QIODevice::ReadOnly))
        return -1;
    const QList<QByteArray> lines = status.readAll().split('\n');
    for (const QByteArray &line : lines) {
        if (line.startsWith("VmHWM:"))
            return line.simplified().split(' ').at(1).toLongLong();
    }
    return -1;
}

Document editedDocument(const Document &document, int step)
{
    Document edited = document;
    const int layer = step % LayerCount;
    const int frame = step % Frames;
    const int pixel = (step * 7919) % PixelsPerCel;
    QSharedPointer<QByteArray> cel(new QByteArray(*edited.layers[layer].cels[frame]));
    (*cel)[pixel] = static_cast<char>(1 + (step % 6));
    edited.layers[layer].cels[frame] = cel;
    return edited;
}

int distinctCelCount(const Document &current, const QVector<Document> &undo,
                     const QVector<Document> &redo)
{
    QSet<const void *> pointers;
    const auto collect = [&pointers](const Document &document) {
        for (const Layer &layer : document.layers) {
            for (const QSharedPointer<QByteArray> &cel : layer.cels)
                pointers.insert(cel->constData());
        }
    };
    collect(current);
    for (const Document &document : undo)
        collect(document);
    for (const Document &document : redo)
        collect(document);
    return pointers.size();
}

QVector<double> timedCompositions(const Document &document, quint64 *sink)
{
    QVector<Rgba> output(PixelsPerCel);
    for (int i = 0; i < CompositionWarmup; ++i) {
        compose(document, i % Frames, output);
        *sink ^= checksum(output);
    }

    QVector<double> samples;
    samples.reserve(CompositionSamples);
    for (int i = 0; i < CompositionSamples; ++i) {
        QElapsedTimer timer;
        timer.start();
        compose(document, i % Frames, output);
        const double elapsed = timer.nsecsElapsed() / 1000000.0;
        *sink ^= checksum(output);
        samples.append(elapsed);
    }
    return samples;
}

double percentile(QVector<double> values, double fraction)
{
    std::sort(values.begin(), values.end());
    const int index = qBound(0, static_cast<int>(std::ceil(values.size() * fraction)) - 1,
                             values.size() - 1);
    return values.at(index);
}

QByteArray saveProjection(const Document &document, const QString &path, double *milliseconds)
{
    QElapsedTimer timer;
    timer.start();
    const QJsonDocument projection = toV2Json(document);
    const QByteArray bytes = projection.toJson(QJsonDocument::Compact);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        return QByteArray();
    file.close();
    *milliseconds = timer.nsecsElapsed() / 1000000.0;
    return bytes;
}

bool loadProjection(const QString &path, Document *document, double *milliseconds,
                    QString *error)
{
    QElapsedTimer timer;
    timer.start();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        *error = file.errorString();
        return false;
    }
    const QByteArray bytes = file.readAll();
    QJsonParseError parseError;
    const QJsonDocument parsed = QJsonDocument::fromJson(bytes, &parseError);
    if (parseError.error != QJsonParseError::NoError
        || !fromV2Json(parsed, document, error)) {
        if (parseError.error != QJsonParseError::NoError)
            *error = parseError.errorString();
        return false;
    }
    *milliseconds = timer.nsecsElapsed() / 1000000.0;
    return true;
}

bool runHistory(const Document &document, int *uniqueCels)
{
    Document current = document;
    QVector<Document> undo;
    QVector<Document> redo;
    undo.reserve(HistorySteps);
    redo.reserve(HistorySteps);
    for (int step = 0; step < HistorySteps; ++step) {
        undo.append(current);
        current = editedDocument(current, step);
    }
    for (int step = 0; step < HistorySteps; ++step) {
        redo.append(current);
        current = undo.takeLast();
    }
    for (int step = 0; step < HistorySteps; ++step) {
        undo.append(current);
        current = redo.takeLast();
    }
    *uniqueCels = distinctCelCount(current, undo, redo);
    return undo.size() == HistorySteps && redo.isEmpty();
}

bool runPlayback(const Document &document, quint64 *sink, int *missed, qint64 *elapsedMs)
{
    QVector<Rgba> output(PixelsPerCel);
    compose(document, 0, output);
    *sink ^= checksum(output);

    QElapsedTimer timer;
    timer.start();
    *missed = 0;
    constexpr qint64 frameNsecs = 1000000000LL / PlaybackFps;
    for (int frame = 0; frame < PlaybackFrames; ++frame) {
        compose(document, frame % Frames, output);
        *sink ^= checksum(output);
        const qint64 deadline = frameNsecs * (frame + 1);
        if (timer.nsecsElapsed() > deadline)
            ++*missed;
        while (timer.nsecsElapsed() < deadline) {
            const qint64 remaining = deadline - timer.nsecsElapsed();
            QThread::usleep(static_cast<unsigned long>(qMin<qint64>(remaining / 1000, 5000)));
        }
    }
    *elapsedMs = timer.elapsed();
    return *missed == 0;
}

void printEnvironment()
{
    std::printf("environment=qt-%s;arch=%s;threads=%d\n",
                qVersion(), qPrintable(QSysInfo::currentCpuArchitecture()),
                QThread::idealThreadCount());
    QFile cpuInfo(QStringLiteral("/proc/cpuinfo"));
    if (cpuInfo.open(QIODevice::ReadOnly)) {
        const QList<QByteArray> lines = cpuInfo.readAll().split('\n');
        for (const QByteArray &line : lines) {
            if (line.startsWith("model name")) {
                std::printf("cpu=%s\n", line.mid(line.indexOf(':') + 1).trimmed().constData());
                break;
            }
        }
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    std::printf("benchmark=canonical-v2-multilayer\n");
    std::printf("workload=329x480;frames=64;layers=5;fps=%d;transparent-cells=present;non-100-opacity=present\n",
                PlaybackFps);
    printEnvironment();
    std::printf("composition_protocol=warmup=%d;samples=%d;frame-cycling=0..63;output=reused\n",
                CompositionWarmup, CompositionSamples);
    std::printf("history_protocol=copy-on-write-cels;edits=%d;undo=%d;redo=%d\n",
                HistorySteps, HistorySteps, HistorySteps);
    std::printf("playback_protocol=frames=%d;duration=%ds;deadline=%dms\n",
                PlaybackFrames, PlaybackSeconds, 1000 / PlaybackFps);

    const Document document = makeDocument();
    QVector<Rgba> output(PixelsPerCel);
    compose(document, 0, output);
    quint64 sink = checksum(output);

    const QVector<double> composition = timedCompositions(document, &sink);
    const double compositionP95 = percentile(composition, 0.95);
    std::printf("metric=current-frame-composite-p95;value_ms=%.3f;threshold_ms=16.000;pass=%s\n",
                compositionP95, compositionP95 <= 16.0 ? "yes" : "no");

    int uniqueCels = 0;
    const bool historyOk = runHistory(document, &uniqueCels);
    const qint64 hwmKiB = highWaterMarkKiB();
    const double hwmMiB = hwmKiB < 0 ? -1.0 : hwmKiB / 1024.0;
    const qint64 rasterBytes = qint64(uniqueCels) * PixelsPerCel;
    std::printf("metric=edit-undo-redo-peak-rss;value_mib=%.3f;threshold_mib=384.000;pass=%s;unique_cels=%d;raster_bytes=%lld\n",
                hwmMiB, hwmMiB >= 0 && hwmMiB <= 384.0 && historyOk ? "yes" : "no",
                uniqueCels, static_cast<long long>(rasterBytes));

    const QString path = QDir::tempPath() + QStringLiteral("/omapixel-v2-layer-benchmark.json");
    QVector<double> saves;
    QVector<double> loads;
    QString error;
    double milliseconds = 0.0;
    const QByteArray warmBytes = saveProjection(document, path, &milliseconds);
    if (warmBytes.isEmpty()) {
        std::fprintf(stderr, "save failed\n");
        return EXIT_FAILURE;
    }
    Document loaded;
    if (!loadProjection(path, &loaded, &milliseconds, &error)) {
        std::fprintf(stderr, "warm load failed: %s\n", qPrintable(error));
        return EXIT_FAILURE;
    }
    for (int sample = 0; sample < 3; ++sample) {
        const QByteArray bytes = saveProjection(document, path, &milliseconds);
        if (bytes.isEmpty()) {
            std::fprintf(stderr, "save failed\n");
            return EXIT_FAILURE;
        }
        saves.append(milliseconds);
        if (!loadProjection(path, &loaded, &milliseconds, &error)) {
            std::fprintf(stderr, "load failed: %s\n", qPrintable(error));
            return EXIT_FAILURE;
        }
        loads.append(milliseconds);
    }
    QFile::remove(path);
    const double saveP95 = percentile(saves, 0.95);
    const double loadP95 = percentile(loads, 0.95);
    std::printf("metric=save-v2-json-projection-p95;value_ms=%.3f;threshold_ms=3000.000;pass=%s;bytes=%lld\n",
                saveP95, saveP95 <= 3000.0 ? "yes" : "no",
                static_cast<long long>(warmBytes.size()));
    std::printf("metric=load-v2-json-projection-p95;value_ms=%.3f;threshold_ms=3000.000;pass=%s\n",
                loadP95, loadP95 <= 3000.0 ? "yes" : "no");

    int missed = 0;
    qint64 playbackMs = 0;
    const bool playbackOk = runPlayback(document, &sink, &missed, &playbackMs);
    std::printf("metric=playback-equivalent;value_seconds=%.3f;expected_seconds=%d;frames=%d;missed_deadlines=%d;pass=%s\n",
                playbackMs / 1000.0, PlaybackSeconds, PlaybackFrames, missed,
                playbackOk ? "yes" : "no");
    std::printf("checksum=%llu\n", static_cast<unsigned long long>(sink));

    const bool allPassed = compositionP95 <= 16.0
        && saveP95 <= 3000.0
        && loadP95 <= 3000.0
        && hwmMiB >= 0.0 && hwmMiB <= 384.0
        && playbackOk;
    std::printf("result=%s\n", allPassed ? "PASS" : "FAIL");
    return allPassed ? EXIT_SUCCESS : EXIT_FAILURE;
}
