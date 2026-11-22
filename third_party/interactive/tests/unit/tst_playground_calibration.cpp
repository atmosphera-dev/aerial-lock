// SPDX-License-Identifier: LGPL-3.0-or-later
#include "PlaygroundState.h"
#include "PaletteSampleHistory.h"
#include "AppearanceTraceController.h"
#include "AppearanceTrace_p.h"
#include <QDateTime>
#include <QElapsedTimer>
#include <QDir>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QCryptographicHash>
#include <sys/stat.h>
#include <memory>
#include <cstdlib>

using InterActive::Appearance;

namespace {
QVariantMap inputColors(const QVariantMap &colors)
{
    return {{QStringLiteral("tokens"), QVariantMap{{QStringLiteral("colors"), colors}}}};
}
QVariantMap colors(const PlaygroundState &state)
{
    return state.appearanceInput().value(QStringLiteral("tokens")).toMap()
        .value(QStringLiteral("colors")).toMap();
}
QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}
class FailingWriter : public PaletteSampleWriter
{
public:
    enum Failure { Partial, Zero, Flush, Close, Readback };
    FailingWriter(const QString &path, Failure failure) : PaletteSampleWriter(path), m_failure(failure) {}
protected:
    qint64 writeBatch(QFile &file, const QByteArray &batch) override
    {
        if (m_failure == Zero)
            return 0;
        return PaletteSampleWriter::writeBatch(file, m_failure == Partial ? batch.left(7) : batch);
    }
    bool flush(QFile &file) override
    {
        return m_failure != Flush && PaletteSampleWriter::flush(file);
    }
    bool close(QFile &file, QString *error) override
    {
        const bool result = PaletteSampleWriter::close(file, error);
        if (m_failure == Close)
            *error = QStringLiteral("injected close failure");
        return m_failure != Close && result;
    }
    bool readBack(QFile &file, qint64 offset, const QByteArray &batch, QString *error) override
    {
        if (m_failure == Readback) {
            *error = QStringLiteral("injected readback mismatch");
            return false;
        }
        return PaletteSampleWriter::readBack(file, offset, batch, error);
    }
private:
    Failure m_failure;
};

QJsonObject historyRecord(const QColor &accent = QColor("#5c9cf5"))
{
    const auto colorObject = [](const QColor &color) {
        const auto rgba = color.rgba64();
        return QJsonObject{{"hexArgb", color.name(QColor::HexArgb)},
            {"rgba16", QJsonArray{rgba.red(), rgba.green(), rgba.blue(), rgba.alpha()}}};
    };
    return {{"version", 1}, {"utc", "2026-10-04T20:00:00.000Z"}, {"scheme", "light"},
        {"contrast", "normal"}, {"accent", colorObject(accent)}, {"amber", colorObject(QColor("orange"))},
        {"danger", colorObject(QColor("red"))}, {"corrections", QJsonObject{{"amber", false}, {"danger", false}}}};
}

QByteArray historyLine(const QJsonObject &record)
{
    return QJsonDocument(record).toJson(QJsonDocument::Compact) + '\n';
}

class InterruptedHistory : public PaletteSampleHistory
{
public:
    using PaletteSampleHistory::PaletteSampleHistory;
protected:
    QByteArray readChunk(QFile &, qint64) const override { return {}; }
};

class AppendingHistory : public PaletteSampleHistory
{
public:
    explicit AppendingHistory(const QString &path) : PaletteSampleHistory(path), m_path(path) {}
protected:
    QByteArray readChunk(QFile &file, qint64 maximum) const override
    {
        if (!m_appended) {
            m_appended = true;
            QFile append(m_path);
            if (append.open(QIODevice::WriteOnly | QIODevice::Append))
                append.write(historyLine(historyRecord(QColor("#49335f"))));
        }
        return PaletteSampleHistory::readChunk(file, maximum);
    }
private:
    QString m_path;
    mutable bool m_appended = false;
};
}

class PlaygroundCalibrationTests : public QObject
{
    Q_OBJECT
private slots:
    void historyDisplayedIdentity()
    {
        const QColor blue("#5c9cf5");
        const auto rgb = blue.rgba64();
        const QColor near = QColor::fromRgba64(rgb.red() + 1, rgb.green() - 1, rgb.blue() + 1, 65535);
        QCOMPARE(PaletteSampleHistory::opaqueAccentKey(near), QStringLiteral("#5c9cf5"));
        QVERIFY(near != blue);
        QVERIFY(PaletteSampleHistory::opaqueAccentKey(QColor::fromRgba64(rgb.red(), rgb.green(), rgb.blue(), 65534)).isEmpty());
        QVERIFY(PaletteSampleHistory::opaqueAccentKey(QColor()).isEmpty());
        QCOMPARE(PaletteSampleHistory::opaqueAccentKey(QColor("#5d9cf5")), QStringLiteral("#5d9cf5"));
        QTemporaryDir dir;
        const QString path = dir.filePath("samples.txt");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
        for (const QColor &color : {near, blue, QColor::fromRgba64(rgb.red(), rgb.green(), rgb.blue(), 65534)})
            file.write(historyLine(historyRecord(color)));
        for (const QString &scheme : {QStringLiteral("light"), QStringLiteral("dark")})
            for (const QString &contrast : {QStringLiteral("normal"), QStringLiteral("high")}) {
                auto record = historyRecord(QColor("#49335f"));
                record["scheme"] = scheme;
                record["contrast"] = contrast;
                auto accent = record["accent"].toObject();
                accent["hexArgb"] = accent["hexArgb"].toString().toUpper();
                record["accent"] = accent;
                record["extra"] = QJsonObject{{"future", true}};
                file.write(historyLine(record));
            }
        file.close();
        const QByteArray before = readFile(path);
        const auto result = PaletteSampleHistory(path).read();
        QVERIFY(result.available);
        QCOMPARE(result.skipped, quint64(0));
        QCOMPARE(result.keys, QStringList({"#49335f", "#5c9cf5"}));
        QCOMPARE(readFile(path), before);
        // A fresh constructed owner reads disk without replaying any semantic input.
        Appearance appearance;
        PlaygroundState state(&appearance);
        state.setPaletteSamplePath(path);
        QCOMPARE(state.savedAccentKeys(), result.keys);
        QCOMPARE(state.appearanceInput(), QVariantMap{});
    }

    void historyValidation_data()
    {
        QTest::addColumn<QByteArray>("invalid");
        const auto add = [](const char *name, const QJsonObject &record) { QTest::newRow(name) << historyLine(record); };
        for (const auto &field : QStringList{"version", "utc", "scheme", "contrast", "accent", "amber", "danger", "corrections"}) {
            auto record = historyRecord();
            record.remove(field);
            add(qPrintable("missing-" + field), record);
        }
        for (const auto &entry : QList<QPair<QString, QJsonValue>>{
                 {"version", 2}, {"version", "1"}, {"utc", "not-a-date"}, {"utc", "2026-10-04T20:00:00+01:00"},
                 {"scheme", "auto"}, {"contrast", "low"}, {"corrections", QJsonObject{{"amber", 1}, {"danger", false}}}}) {
            auto record = historyRecord();
            record[entry.first] = entry.second;
            add(qPrintable(entry.first + "-" + QString::number(QJsonDocument(record).toJson().size())), record);
        }
        int index = 0;
        for (const QJsonValue &channels : QList<QJsonValue>{QJsonArray{0, 0, 0}, QJsonArray{-1, 40092, 62965, 65535},
                 QJsonArray{65536, 40092, 62965, 65535}, QJsonArray{23644.5, 40092, 62965, 65535},
                 QJsonArray{"23644", 40092, 62965, 65535}, QJsonArray{true, 40092, 62965, 65535}, "channels"}) {
            auto record = historyRecord();
            auto accent = record["accent"].toObject();
            accent["rgba16"] = channels;
            record["accent"] = accent;
            add(qPrintable("channels-" + QString::number(index++)), record);
        }
        for (const QString &hex : {QStringLiteral("#ff000000"), QStringLiteral("#5c9cf5"), QStringLiteral("#zz5c9cf5")}) {
            auto record = historyRecord();
            auto accent = record["accent"].toObject();
            accent["hexArgb"] = hex;
            record["accent"] = accent;
            add(qPrintable(hex), record);
        }
        QTest::newRow("fragment-object") << QByteArray("{\"accent\":\"#5c9cf5\"}\n");
        QTest::newRow("broken-json") << QByteArray("{bad}\n");
        QTest::newRow("oversized") << (QByteArray(12000, 'x') + '\n');
    }

    void historyValidation()
    {
        QFETCH(QByteArray, invalid);
        QTemporaryDir dir;
        const QString path = dir.filePath("samples.txt");
        const QByteArray bytes = historyLine(historyRecord(QColor("#49335f"))) + invalid
            + historyLine(historyRecord()) + historyLine(historyRecord(QColor("#d0d0d0"))).chopped(1);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
        QCOMPARE(file.write(bytes), bytes.size());
        file.close();
        const auto before = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
        const auto result = PaletteSampleHistory(path).read();
        QVERIFY(result.available);
        QCOMPARE(result.keys, QStringList({"#49335f", "#5c9cf5"}));
        QCOMPARE(result.skipped, quint64(2));
        QVERIFY(result.status.contains("skipped 2"));
        QCOMPARE(QFileInfo(path).size(), bytes.size());
        QCOMPARE(QCryptographicHash::hash(readFile(path), QCryptographicHash::Sha256), before);
    }

    void historySafeDestinationsAndReadFailure()
    {
        QTemporaryDir dir;
        const QString missing = dir.filePath("absent-parent/samples.txt");
        const auto empty = PaletteSampleHistory(missing).read();
        QVERIFY(empty.available && empty.keys.isEmpty());
        QVERIFY(!QFileInfo::exists(dir.filePath("absent-parent")));
        QVERIFY(!QFileInfo::exists(missing + ".lock"));
        const QString fifo = dir.filePath("fifo");
        QCOMPARE(::mkfifo(QFile::encodeName(fifo).constData(), 0600), 0);
        for (const QString &path : {QString(), QStringLiteral("relative.txt"), dir.path(), QStringLiteral("/dev/null"), fifo}) {
            QElapsedTimer timer;
            timer.start();
            const auto result = PaletteSampleHistory(path).read();
            QVERIFY(!result.available && result.keys.isEmpty());
            QVERIFY(result.status.contains("unavailable"));
            QVERIFY(timer.elapsed() < 1000);
        }
        const QString path = dir.filePath("samples.txt");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
        const auto bytes = historyLine(historyRecord());
        QCOMPARE(file.write(bytes), bytes.size());
        file.close();
        QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner));
        QVERIFY(PaletteSampleHistory(path).read().available);
        const auto interrupted = InterruptedHistory(path).read();
        QVERIFY(!interrupted.available && interrupted.keys.isEmpty());
        QCOMPARE(readFile(path), bytes);
        QVERIFY(QFile::setPermissions(path, {}));
        const auto denied = PaletteSampleHistory(path).read();
        QVERIFY(!denied.available && denied.keys.isEmpty());
        QVERIFY(QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner));
        QCOMPARE(readFile(path), bytes);
    }

    void historyBoundedAppendAndTail()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("samples.txt");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
        file.write(historyLine(historyRecord()));
        file.close();
        const auto result = AppendingHistory(path).read();
        QCOMPARE(result.keys, QStringList({"#5c9cf5"}));
        QCOMPARE(PaletteSampleHistory(path).read().keys, QStringList({"#49335f", "#5c9cf5"}));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Append));
        file.write(historyLine(historyRecord(QColor("#d0d0d0"))).chopped(1));
        file.close();
        QCOMPARE(PaletteSampleHistory(path).read().skipped, quint64(1));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Append));
        file.write("\n");
        file.close();
        QCOMPARE(PaletteSampleHistory(path).read().keys, QStringList({"#49335f", "#5c9cf5", "#d0d0d0"}));
    }

    void historyRefreshSignalsAndIndependentStatuses()
    {
        QTemporaryDir dir;
        Appearance appearance;
        PlaygroundState state(&appearance);
        state.setPaletteSamplePath(dir.filePath("first.txt"));
        state.setDraftName("history draft");
        QVERIFY(state.applyInitialPaletteSuggestions());
        QSignalSpy source(&appearance, &Appearance::inputChanged);
        QSignalSpy keys(&state, &PlaygroundState::savedAccentKeysChanged);
        QSignalSpy status(&state, &PlaygroundState::paletteHistoryStatusChanged);
        const auto input = state.appearanceInput();
        state.refreshSavedAccents();
        QCOMPARE(keys.size(), 0);
        QCOMPARE(status.size(), 0);
        bool coherent = false;
        connect(&state, &PlaygroundState::savedAccentKeysChanged, this, [&] {
            coherent = true;
            QVERIFY(state.paletteHistoryAvailable());
            QVERIFY(!state.paletteHistoryStatus().isEmpty());
        });
        QVERIFY(state.savePaletteSample());
        QVERIFY(coherent);
        QCOMPARE(keys.size(), 1);
        QCOMPARE(state.savedAccentKeys(), QStringList({"#5c9cf5"}));
        const QString saved = state.paletteSaveStatus();
        const QByteArray bytes = readFile(state.paletteSamplePath());
        state.refreshSavedAccents();
        QCOMPARE(keys.size(), 1);
        QCOMPARE(readFile(state.paletteSamplePath()), bytes);
        // Same-path external replacement is observed only at an explicit refresh.
        QFile file(state.paletteSamplePath());
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(historyLine(historyRecord(QColor("#49335f"))));
        file.close();
        QCOMPARE(state.savedAccentKeys(), QStringList({"#5c9cf5"}));
        state.refreshSavedAccents();
        QCOMPARE(state.savedAccentKeys(), QStringList({"#49335f"}));
        disconnect(&state, &PlaygroundState::savedAccentKeysChanged, this, nullptr);
        state.setPaletteSamplePath(dir.filePath("second.txt"));
        QVERIFY(state.savedAccentKeys().isEmpty());
        QVERIFY(state.paletteHistoryAvailable());
        state.setPaletteSamplePath(dir.path());
        QVERIFY(!state.paletteHistoryAvailable());
        QCOMPARE(state.paletteSaveStatus(), saved);
        QVERIFY(!state.paletteSaveFailed());
        QCOMPARE(state.appearanceError(), QString());
        QCOMPARE(state.appearanceInput(), input);
        QCOMPARE(source.size(), 0);
        QCOMPARE(state.draftName(), QStringLiteral("history draft"));
        QVERIFY(state.hasAmberCorrection() && state.hasDangerCorrection());
    }
    void launcherStartup_data()
    {
        QTest::addColumn<QString>("scheme");
        QTest::addColumn<QString>("contrast");
        QTest::addColumn<QString>("scene");
        for (const QString &scene : {QStringLiteral("default"), QStringLiteral("partial-profile")})
            for (const QString &scheme : {QStringLiteral("light"), QStringLiteral("dark")})
                for (const QString &contrast : {QStringLiteral("normal"), QStringLiteral("high")})
                    QTest::newRow(qPrintable(scene + scheme + contrast)) << scheme << contrast << scene;
    }
    void launcherStartup()
    {
        QFETCH(QString, scene);
        QFETCH(QString, scheme);
        QFETCH(QString, contrast);
        QTemporaryDir dir;
        const QString path = dir.filePath("samples.txt");
        QProcess process;
        const QString executable = qEnvironmentVariable("IA_CALIBRATION_PLAYGROUND_EXECUTABLE",
            QCoreApplication::applicationDirPath() + "/interactive-playground");
        process.start(executable,
            {"--automation", "--scene", scene, "--appearance", scheme, "--contrast", contrast,
             "--palette-samples-file", path});
        QVERIFY(process.waitForStarted(5000));
        QByteArray buffer;
        QJsonObject ready;
        QElapsedTimer deadline;
        deadline.start();
        while (ready.isEmpty() && deadline.elapsed() < 20000 && process.state() != QProcess::NotRunning) {
            process.waitForReadyRead(1000);
            buffer += process.readAllStandardOutput();
            for (const auto &line : buffer.split('\n')) {
                if (!line.startsWith("IA_EVENT "))
                    continue;
                const auto record = QJsonDocument::fromJson(line.mid(9)).object();
                if (record.value("event").toString() == "ready")
                    ready = record;
            }
        }
        // Request normal closure even if readiness failed, so the evidence has an exit.
        process.write("{\"command\":\"close\"}\n");
        QVERIFY(process.waitForFinished(10000));
        QVERIFY2(!ready.isEmpty(), process.readAllStandardError().constData());
        QCOMPARE(process.exitCode(), 0);
        QCOMPARE(ready.value("qtVersion").toString(), QString::fromLatin1(qVersion()));
        QCOMPARE(ready.value("renderer").toString(), QStringLiteral("opengl"));
        QCOMPARE(ready.value("resolvedScheme").toString(), scheme);
        const auto input = ready.value("input").toObject();
        const auto roots = input.value("tokens").toObject().value("colors").toObject();
        if (scene == "default") {
            QCOMPARE(QColor(roots.value("warning").toString()), QColor("#FF7A18"));
            QCOMPARE(QColor(roots.value("error").toString()), QColor("#FF3434"));
            QVERIFY(!roots.contains("accent"));
        } else {
            QCOMPARE(QColor(roots.value("accent").toString()), QColor("#8657b8"));
            QVERIFY(!roots.contains("warning") && !roots.contains("error"));
        }
        QVERIFY(!QFileInfo::exists(path));
    }
    void initialSuggestions_data()
    {
        QTest::addColumn<QString>("scheme");
        QTest::addColumn<QString>("contrast");
        for (const QString &scheme : {QStringLiteral("light"), QStringLiteral("dark")})
            for (const QString &contrast : {QStringLiteral("normal"), QStringLiteral("high")})
                QTest::newRow(qPrintable(scheme + contrast)) << scheme << contrast;
    }
    void initialSuggestions()
    {
        QFETCH(QString, scheme);
        QFETCH(QString, contrast);
        Appearance appearance;
        PlaygroundState state(&appearance);
        QCOMPARE(state.appearanceInput(), QVariantMap{});
        QVERIFY(!state.hasAmberCorrection() && !state.hasDangerCorrection());
        QVERIFY(state.setAppearance({{QStringLiteral("scheme"), scheme}, {QStringLiteral("contrast"), contrast}}));
        QSignalSpy changes(&appearance, &Appearance::inputChanged);
        QVERIFY(state.applyInitialPaletteSuggestions());
        QCOMPARE(changes.size(), 1);
        QCOMPARE(appearance.colors()->warning(), QColor("#FF7A18"));
        QCOMPARE(appearance.colors()->error(), QColor("#FF3434"));
        QVERIFY(state.hasAmberCorrection() && state.hasDangerCorrection());
        QVERIFY(!state.hasAccentOverride());
        QVERIFY(state.resetAccentForCalibration());
        QVERIFY(!state.hasAmberCorrection() && !state.hasDangerCorrection());
        QVERIFY(colors(state).isEmpty());
        QVERIFY(state.setAppearance({}));
        QCOMPARE(state.appearanceInput(), QVariantMap{});
        QVERIFY(state.setAppearanceToken("colors", "accent", QColor("#765432")));
        QVERIFY(state.applyInitialPaletteSuggestions());
        QVERIFY(!state.hasAmberCorrection());
        QVERIFY(state.setAppearance(inputColors({{"warning", "#abcdef"}})));
        QVERIFY(state.applyInitialPaletteSuggestions());
        QVERIFY(!state.hasAmberCorrection() && !state.hasDangerCorrection());
        QCOMPARE(colors(state).size(), 1);
    }
    void ownershipAndAtomicAccent()
    {
        Appearance appearance;
        PlaygroundState state(&appearance);
        const QVariant rawWarning = QStringLiteral("#FF7A18");
        QVERIFY(state.setAppearance(inputColors({{"warning", rawWarning}, {"primarySurface", "#123456"},
                                                 {"errorText", "#654321"}})));
        QVERIFY(state.setSemanticCorrection("warning", QColor("#dba200")));
        QVERIFY(state.setSemanticCorrection("warning", QColor("#aadd00")));
        QVERIFY(state.setSemanticCorrection("error", QColor("#cd1234")));
        QVERIFY(state.setAppearanceOption("density", "touch"));
        QVERIFY(state.setAppearanceOption("scheme", "dark"));
        QVERIFY(state.setAppearanceToken("metrics", "controlRadius", 11));
        QVERIFY(state.setAppearanceToken("colors", "accent", appearance.colors()->accent()));
        QVERIFY(state.hasAmberCorrection() && state.hasDangerCorrection());
        QSignalSpy changes(&appearance, &Appearance::inputChanged);
        bool observed = false;
        connect(&appearance, &Appearance::resolvedChanged, this, [&] {
            observed = true;
            QVERIFY(!state.hasAmberCorrection() && !state.hasDangerCorrection());
            QCOMPARE(colors(state).value("warning"), rawWarning);
            QVERIFY(!colors(state).contains("error"));
            QCOMPARE(appearance.colors()->accent(), QColor("#234567"));
        });
        QVERIFY(state.setAppearanceToken("colors", "accent", QColor("#234567")));
        QVERIFY(observed);
        QCOMPARE(changes.size(), 1);
        QCOMPARE(colors(state).value("primarySurface"), QVariant("#123456"));
        QCOMPARE(colors(state).value("errorText"), QVariant("#654321"));
        QCOMPARE(appearance.density(), QStringLiteral("touch"));
        QCOMPARE(appearance.metrics()->controlRadius(), 11);
    }
    void precisionAndResetPresence()
    {
        Appearance appearance;
        PlaygroundState state(&appearance);
        const QColor precise = QColor::fromRgba64(12345, 34567, 45678, 54321);
        QVERIFY(state.setAppearanceToken("colors", "accent", precise));
        const QColor generated = appearance.colors()->warning();
        QVERIFY(state.setAppearanceToken("colors", "warning", generated));
        QVERIFY(state.hasAmberOverride());
        QVERIFY(state.setSemanticCorrection("warning", generated));
        QVERIFY(state.hasAmberCorrection());
        QVERIFY(state.setAppearanceToken("colors", "accent", precise));
        QVERIFY(state.hasAmberCorrection());
        QVERIFY(state.resetAppearanceToken("colors", "accent"));
        QVERIFY(!state.hasAmberCorrection());
        QCOMPARE(colors(state).value("warning").value<QColor>(), generated);
        QVERIFY(state.resetSemanticCorrection("warning"));
        QVERIFY(!state.hasAmberOverride());
        QVERIFY(state.setAppearanceToken("colors", "accent", precise));
        QVERIFY(state.setSemanticCorrection("error", QColor("red")));
        QVERIFY(state.setAppearanceToken("colors", "accent", QColor::fromRgba64(12346, 34567, 45678, 54321)));
        QVERIFY(!state.hasDangerCorrection());
        QVERIFY(state.setSemanticCorrection("error", QColor("red")));
        QVERIFY(state.setAppearanceToken("colors", "accent", QColor::fromRgba64(12346, 34567, 45678, 54322)));
        QVERIFY(!state.hasDangerCorrection());
        QVERIFY(state.setAppearanceToken("colors", "accent", precise));
        QVERIFY(!state.hasDangerCorrection());
        QVERIFY(!state.setSemanticCorrection("success", QColor("green")));
        QVERIFY(!state.setSemanticCorrection("warning", QColor()));
    }
    void authoritativeReplacementAndTakeover()
    {
        Appearance appearance;
        PlaygroundState state(&appearance);
        QVERIFY(state.applyInitialPaletteSuggestions());
        const auto identical = state.appearanceInput();
        QSignalSpy ownership(&state, &PlaygroundState::correctionsChanged);
        QSignalSpy source(&appearance, &Appearance::resolvedChanged);
        QVERIFY(state.setAppearance(identical));
        QCOMPARE(source.size(), 0);
        QCOMPARE(ownership.size(), 1);
        QVERIFY(!state.hasAmberCorrection() && !state.hasDangerCorrection());
        QVERIFY(state.setAppearanceToken("colors", "accent", "#345678"));
        QCOMPARE(appearance.colors()->warning(), QColor("#FF7A18"));
        QVERIFY(state.setSemanticCorrection("warning", QColor("yellow")));
        QVERIFY(state.setSemanticCorrection("error", QColor("red")));
        QVERIFY(state.setAppearanceOption("tokens", state.appearanceInput().value("tokens")));
        QVERIFY(!state.hasAmberCorrection() && !state.hasDangerCorrection());
        QVERIFY(state.setSemanticCorrection("warning", QColor("orange")));
        QVERIFY(state.setSemanticCorrection("error", QColor("pink")));
        QVERIFY(state.setAppearanceToken("colors", "warning", "#aabbcc"));
        QVERIFY(!state.hasAmberCorrection() && state.hasDangerCorrection());
        QVERIFY(state.resetAppearanceToken("colors", "error"));
        QVERIFY(!state.hasDangerCorrection());
        QVERIFY(state.setAppearanceToken("colors", "accent", "#56789a"));
        QCOMPARE(colors(state).value("warning"), QVariant("#aabbcc"));
        QVERIFY(!colors(state).contains("error"));
    }
    void rejectedAndInvalidFields()
    {
        Appearance appearance;
        PlaygroundState state(&appearance);
        QVERIFY(state.applyInitialPaletteSuggestions());
        const auto before = state.appearanceInput();
        QVERIFY(!state.setAppearanceJson("{\"tokens\":"));
        QVERIFY(!state.setAppearance({{"schemaVersion", 9}}));
        QVERIFY(!state.setAppearanceOption("tokens", 6));
        QCOMPARE(state.appearanceInput(), before);
        QVERIFY(state.hasAmberCorrection() && state.hasDangerCorrection());
        QVERIFY(state.setAppearance(inputColors({{"warning", "bad-color"}, {"accent", "#987654"}})));
        QVERIFY(!state.hasAmberCorrection() && !state.hasDangerCorrection());
        QCOMPARE(appearance.colors()->accent(), QColor("#987654"));
        QVERIFY(!appearance.diagnostics().isEmpty());
        QVERIFY(state.setSemanticCorrection("warning", QColor("yellow")));
        QVERIFY(state.resetSemanticCorrection("warning"));
        QCOMPARE(colors(state).value("warning"), QVariant("bad-color"));
    }
    void rejectionObserverNestedResetUsesActualBaseline()
    {
        Appearance appearance;
        PlaygroundState state(&appearance);
        QVERIFY(state.setAppearance(inputColors({{"warning", "#112233"}})));
        QVERIFY(state.setSemanticCorrection("warning", QColor("orange")));
        bool once = false;
        connect(&appearance, &Appearance::diagnosticsChanged, this, [&] {
            if (once)
                return;
            once = true;
            QVERIFY(state.hasAmberCorrection());
            QCOMPARE(appearance.colors()->warning(), QColor("orange"));
            QVERIFY(state.resetSemanticCorrection("warning"));
            QCOMPARE(colors(state).value("warning"), QVariant("#112233"));
            QVERIFY(!state.hasAmberCorrection());
        });
        QVERIFY(!state.setAppearance({{"tokens", 42}}));
        QVERIFY(once);
        QCOMPARE(colors(state).value("warning"), QVariant("#112233"));
        QVERIFY(!state.hasAmberCorrection());
    }
    void nestedAcceptedReplacementNewestWins()
    {
        Appearance appearance;
        PlaygroundState *state = nullptr;
        bool armed = false;
        // Earlier source observers can inspect and invoke the playground writer.
        connect(&appearance, &Appearance::resolvedChanged, this, [&] {
            if (!armed)
                return;
            armed = false;
            QVERIFY(state->hasAmberCorrection());
            QVERIFY(state->setAppearance(state->appearanceInput())); // authoritative no-op
            QVERIFY(!state->hasAmberCorrection());
        });
        PlaygroundState owner(&appearance);
        state = &owner;
        armed = true;
        QVERIFY(owner.setSemanticCorrection("warning", QColor("orange")));
        QVERIFY(!owner.hasAmberCorrection());
        QVERIFY(owner.setAppearanceToken("colors", "accent", "#987654"));
        QCOMPARE(appearance.colors()->warning(), QColor("orange"));
        QVERIFY(owner.setSemanticCorrection("error", QColor("red")));
        bool replace = true;
        connect(&appearance, &Appearance::resolvedChanged, this, [&] {
            if (!replace)
                return;
            replace = false;
            QVERIFY(owner.setAppearance(inputColors({{"warning", "#aabbcc"}})));
        });
        QVERIFY(owner.setSemanticCorrection("warning", QColor("yellow")));
        QVERIFY(!owner.hasAmberCorrection() && !owner.hasDangerCorrection());
        QCOMPARE(colors(owner).value("warning"), QVariant("#aabbcc"));
    }
    void appliedSnapshotAndAppendPrecision()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Appearance appearance;
        PlaygroundState state(&appearance);
        const QString path = dir.filePath("new-parent/samples.txt");
        state.setPaletteSamplePath(path);
        QVERIFY(!QFileInfo::exists(path));
        state.setDraftName("unsaved form draft");
        state.setDialogName("unsaved dialog draft");
        const QColor precise = QColor::fromRgba64(12345, 34567, 45678, 54321);
        QVERIFY(state.setAppearance(inputColors({{"accent", precise}, {"primarySurface", "#abcdef"}})));
        QVERIFY(state.setSemanticCorrection("warning", QColor::fromRgba64(11111, 22222, 33333, 44444)));
        QVERIFY(state.setAppearanceOption("scheme", "dark"));
        QVERIFY(state.setAppearanceOption("contrast", "high"));
        QVERIFY(!QFileInfo::exists(path));
        const auto input = state.appearanceInput();
        QVERIFY(state.savePaletteSample());
        const QByteArray first = readFile(path);
        QVERIFY(first.endsWith('\n'));
        QVERIFY(state.savePaletteSample());
        const QByteArray second = readFile(path);
        QVERIFY(second.startsWith(first));
        const auto lines = second.split('\n');
        QCOMPARE(lines.size(), 3);
        for (int i = 0; i < 2; ++i) {
            const auto record = QJsonDocument::fromJson(lines[i]).object();
            QCOMPARE(record.value("version").toInt(), 1);
            QVERIFY(QDateTime::fromString(record.value("utc").toString(), Qt::ISODateWithMs).isValid());
            QCOMPARE(record.value("scheme").toString(), QStringLiteral("dark"));
            QCOMPARE(record.value("contrast").toString(), QStringLiteral("high"));
            const std::array<QColor, 3> applied{appearance.colors()->accent(), appearance.colors()->warning(), appearance.colors()->error()};
            const QStringList roles{"accent", "amber", "danger"};
            for (int j = 0; j < 3; ++j) {
                const auto value = record.value(roles[j]).toObject();
                const auto rgba = applied[j].rgba64();
                QCOMPARE(value.value("rgba16").toArray(), QJsonArray({rgba.red(), rgba.green(), rgba.blue(), rgba.alpha()}));
                QCOMPARE(value.value("hexArgb").toString(), applied[j].name(QColor::HexArgb));
            }
            QCOMPARE(record.value("corrections").toObject().value("amber").toBool(), true);
            QVERIFY(!record.contains("input") && !record.contains("success") && !record.contains("draftName"));
        }
        QCOMPARE(state.appearanceInput(), input);
        QCOMPARE(state.draftName(), QStringLiteral("unsaved form draft"));
        QCOMPARE(state.dialogName(), QStringLiteral("unsaved dialog draft"));
        QVERIFY(state.paletteSaveStatus().startsWith("Last saved "));
        const QString saved = state.paletteSaveStatus();
        QVERIFY(state.setAppearanceToken("colors", "accent", "#abcdef"));
        QCOMPARE(state.paletteSaveStatus(), saved);
        QCOMPARE(readFile(path), second);
        QVERIFY(state.setAppearance({}));
        QCOMPARE(readFile(path), second);
    }
    void nestedRejectionMessageSurvivesOuterAcceptance_data()
    {
        QTest::addColumn<bool>("malformedJson");
        QTest::newRow("envelope") << false;
        QTest::newRow("json") << true;
    }
    void nestedRejectionMessageSurvivesOuterAcceptance()
    {
        QFETCH(bool, malformedJson);
        Appearance appearance;
        PlaygroundState state(&appearance);
        bool once = false;
        connect(&appearance, &Appearance::resolvedChanged, this, [&] {
            if (once)
                return;
            once = true;
            if (malformedJson)
                QVERIFY(!state.setAppearanceJson("{\"tokens\":"));
            else
                QVERIFY(!state.setAppearance({{"tokens", 42}}));
            QVERIFY(!state.appearanceError().isEmpty());
            QVERIFY(state.hasAmberCorrection());
        });
        QVERIFY(state.setSemanticCorrection("warning", QColor("orange")));
        QVERIFY(once);
        QVERIFY(!state.appearanceError().isEmpty());
        QVERIFY(state.hasAmberCorrection());
        QCOMPARE(appearance.colors()->warning(), QColor("orange"));
        QVERIFY(state.setAppearanceOption("scheme", "dark"));
        QVERIFY(state.appearanceError().isEmpty());
        QVERIFY(state.hasAmberCorrection());
    }
    void orphanFragmentPreserved()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("samples.txt");
        const QByteArray fragment = "{malformed orphan";
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
        QCOMPARE(file.write(fragment), fragment.size());
        file.close();
        PaletteSampleWriter writer(path);
        QVERIFY(writer.append("{\"version\":1}\n").success);
        QCOMPARE(readFile(path), fragment + "\n{\"version\":1}\n");
    }
    void failures_data()
    {
        QTest::addColumn<int>("failure");
        for (int i = 0; i <= FailingWriter::Readback; ++i)
            QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void failures()
    {
        QFETCH(int, failure);
        QTemporaryDir dir;
        Appearance appearance;
        PlaygroundState state(&appearance);
        const QString path = dir.filePath("samples.txt");
        PaletteSampleWriter writer(path);
        QVERIFY(writer.append("{\"old\":true}\n").success);
        const QByteArray before = readFile(path);
        state.setPaletteSampleWriter(std::make_unique<FailingWriter>(path, FailingWriter::Failure(failure)));
        QVERIFY(state.applyInitialPaletteSuggestions());
        const auto input = state.appearanceInput();
        QVERIFY(!state.savePaletteSample());
        QVERIFY(state.paletteSaveFailed());
        QVERIFY(state.paletteSaveStatus().contains("retrying can duplicate"));
        QCOMPARE(state.appearanceError(), QString());
        QCOMPARE(state.appearanceInput(), input);
        QVERIFY(state.hasAmberCorrection() && state.hasDangerCorrection());
        const QByteArray after = readFile(path);
        QVERIFY(after.startsWith(before));
        QCOMPARE(after.size() - before.size(), failure == FailingWriter::Partial ? 7
            : failure == FailingWriter::Zero ? 0 : after.size() - before.size());
        if (failure >= FailingWriter::Flush)
            QVERIFY(QJsonDocument::fromJson(after.mid(before.size())).isObject());
        QCOMPARE(state.savedAccentKeys(), failure >= FailingWriter::Flush
            ? QStringList({"#5c9cf5"}) : QStringList{});
        QVERIFY(state.paletteHistoryAvailable());
        QVERIFY(state.paletteSaveFailed());
        QVERIFY(state.paletteSaveStatus().contains("retrying can duplicate"));
    }
    void badDestinationsAndLiveLock()
    {
        QTemporaryDir dir;
        for (const QString &path : {QString(), QStringLiteral("relative.txt"), dir.path(), QStringLiteral("/dev/null"),
                                   dir.filePath("missing/../blocked/samples.txt")}) {
            if (path.contains("blocked")) {
                QFile parent(dir.filePath("blocked"));
                QVERIFY(parent.open(QIODevice::WriteOnly | QIODevice::NewOnly));
            }
            PaletteSampleWriter writer(path);
            QVERIFY(!writer.append("{\"version\":1}\n").success);
        }
        const QString path = dir.filePath("samples.txt");
        QLockFile lock(path + ".lock");
        lock.setStaleLockTime(0);
        QVERIFY(lock.tryLock(0));
        PaletteSampleWriter writer(path);
        const auto result = writer.append("{\"version\":1}\n");
        QVERIFY(!result.success && !result.uncertain);
        QVERIFY(!QFileInfo::exists(path));
        QVERIFY(QFileInfo::exists(path + ".lock"));
        QVERIFY(!writer.append(QByteArray(4097, 'x')).success);
        QVERIFY(!writer.append("{}\n{}\n").success);
        const QString unwritable = dir.filePath("unwritable");
        QVERIFY(QDir().mkdir(unwritable));
        QVERIFY(QFile::setPermissions(unwritable, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
        PaletteSampleWriter denied(unwritable + "/samples.txt");
        QVERIFY(!denied.append("{}\n").success);
        QVERIFY(!QFileInfo::exists(unwritable + "/samples.txt"));
        QVERIFY(QFile::setPermissions(unwritable, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
    }
    void staleOwnerRecoveryAndCooperatingWriters()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("samples.txt");
        QProcess stale;
        stale.start(QCoreApplication::applicationFilePath(), {"--leave-lock", path});
        QVERIFY(stale.waitForFinished(10000));
        QCOMPARE(stale.exitCode(), 0);
        QVERIFY(QFileInfo::exists(path + ".lock"));
        PaletteSampleWriter writer(path);
        QVERIFY(writer.append("{\"staleRecovered\":true}\n").success);
        const QByteArray prefix = readFile(path);
        std::array<QProcess, 8> processes;
        for (int i = 0; i < 8; ++i)
            processes[i].start(QCoreApplication::applicationFilePath(), {"--append-worker", path, QString::number(i)});
        QSet<int> successes;
        for (int i = 0; i < 8; ++i) {
            QVERIFY(processes[i].waitForFinished(10000));
            QCOMPARE(processes[i].exitStatus(), QProcess::NormalExit);
            QVERIFY(processes[i].exitCode() == 0 || processes[i].exitCode() == 3);
            if (processes[i].exitCode() == 0)
                successes.insert(i);
        }
        QVERIFY(!successes.isEmpty());
        const QByteArray data = readFile(path);
        QVERIFY(data.startsWith(prefix));
        const auto lines = data.mid(prefix.size()).split('\n');
        QCOMPARE(lines.size(), successes.size() + 1);
        QSet<int> records;
        for (const QByteArray &line : lines) {
            if (line.isEmpty())
                continue;
            const auto document = QJsonDocument::fromJson(line);
            QVERIFY(document.isObject());
            records.insert(document.object().value("writer").toInt());
        }
        QCOMPARE(records, successes);
    }
    void singleSolveAndTraceRoles()
    {
        using namespace InterActive;
        QTemporaryDir dir;
        Appearance appearance;
        PlaygroundState state(&appearance);
        QVERIFY(state.applyInitialPaletteSuggestions());
        AppearanceTrace::Config config;
        config.capacityMiB = 8;
        QVERIFY(AppearanceTrace::start(TraceMode::Trace, config));
        for (int role = AppearanceTraceBridge::Accent; role <= AppearanceTraceBridge::Danger; ++role) {
            for (auto kind : {AppearanceTraceBridge::ColorHandler, AppearanceTraceBridge::Synchronization}) {
                const auto token = state.traceBridge()->pickerHandlerBegin(kind, AppearanceTraceBridge::ColorRole(role), 0);
                QVERIFY(token != 0);
                state.traceBridge()->pickerHandlerEnd(token, 0);
            }
        }
        const auto legacy = state.traceBridge()->handlerBegin("syncAccentPicker", 0);
        state.traceBridge()->handlerEnd(legacy, "syncAccentPicker", 0);
        QVERIFY(state.setAppearanceToken("colors", "accent", "#345678"));
        AppearanceTrace::stop();
        QVERIFY(AppearanceTrace::quiesce(1000));
        QString error;
        QVERIFY2(AppearanceTrace::exportTo(dir.filePath("trace.json"), dir.filePath("sidecar.json"), &error), qPrintable(error));
        const auto events = QJsonDocument::fromJson(readFile(dir.filePath("trace.json")))
            .object().value("traceEvents").toArray();
        int attempts = 0, paletteBuilds = 0;
        QSet<int> colorsSeen, syncsSeen;
        for (const auto &value : events) {
            const auto event = value.toObject();
            const QString name = event.value("name").toString();
            if (name == "update-attempt") ++attempts;
            if (name == "palette-build" && event.value("ph").toString() == "X") ++paletteBuilds;
            const int role = event.value("args").toObject().value("aux").toInt();
            if (name == "qml-color-handler") colorsSeen.insert(role);
            if (name == "qml-sync-picker") syncsSeen.insert(role);
        }
        QCOMPARE(attempts, 1);
        QCOMPARE(paletteBuilds, 1);
        QCOMPARE(colorsSeen, QSet<int>({0, 1, 2}));
        QCOMPARE(syncsSeen, QSet<int>({0, 1, 2}));
    }
};

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() == 3 && args[1] == "--leave-lock") {
        QLockFile lock(args[2] + ".lock");
        lock.setStaleLockTime(0);
        if (!lock.tryLock(0))
            return 2;
        std::_Exit(0); // test dead-owner recovery without a live lock destructor
    }
    if (args.size() == 4 && args[1] == "--append-worker") {
        PaletteSampleWriter writer(args[2]);
        return writer.append(QJsonDocument(QJsonObject{{"writer", args[3].toInt()}})
                             .toJson(QJsonDocument::Compact) + '\n').success ? 0 : 3;
    }
    PlaygroundCalibrationTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "tst_playground_calibration.moc"
