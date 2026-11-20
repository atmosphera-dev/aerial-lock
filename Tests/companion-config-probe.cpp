#include "../common/config-document.h"
#include "../companion/config-repository.h"
#include "../native/local-config.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSaveFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <sys/stat.h>

using namespace Aerial;

static void writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) qFatal("fixture publication failed");
}

class ConfigTests : public QObject {
    Q_OBJECT
public:
    QString source;
private slots:
    void readerAndFacade_data()
    {
        QTest::addColumn<QByteArray>("user");
        QTest::addColumn<bool>("present");
        QTest::newRow("missing") << QByteArray() << false;
        QTest::newRow("partial") << QByteArray(R"({"panel":{"fontSize":19},"unknown":{"future":1}})") << true;
        QTest::newRow("empty-sources") << QByteArray(R"({"sources":[]})") << true;
        QTest::newRow("legacy-local") << QByteArray(R"({"video":"/personal/clip.mp4","videoDir":"/personal/folder"})") << true;
        QTest::newRow("legacy-disabled") << QByteArray(R"({"apple":{"enabled":false}})") << true;
        QTest::newRow("legacy-apple") << QByteArray(R"({"apple":{"enabled":true}})") << true;
        QTest::newRow("explicit-older-apple") << QByteArray("{\"sources\":[{\"id\":\"old-apple\",\"type\":\"apple\",\"name\":\"Saved Apple\",\"source\":\"https://sylvan.apple.com/Aerials/resources-16.tar\"}]}") << true;
        QTest::newRow("legacy-explicit-empty") << QByteArray(R"({"video":"/ignore","sources":[]})") << true;
        QTest::newRow("custom") << QByteArray(R"({"sources":[{"type":"folder","name":"Personal","source":"~/Videos","extra":7}]})") << true;
        QTest::newRow("mixed-sources") << QByteArray(R"({"sources":[{},null,{"type":"folder","name":"Good","source":"/data"}]})") << true;
        QTest::newRow("invalid-sources") << QByteArray(R"({"sources":false})") << true;
        QTest::newRow("invalid-leaves") << QByteArray(R"({"idle":{"timeoutSeconds":-1},"preferredResolution":1440,"panel":false})") << true;
        QTest::newRow("malformed") << QByteArray("{") << true;
        QTest::newRow("empty-file") << QByteArray() << true;
        QTest::newRow("null-file") << QByteArray("null") << true;
    }

    void readerAndFacade()
    {
        QFETCH(QByteArray, user);
        QFETCH(bool, present);
        QTemporaryDir root;
        QVERIFY(root.isValid());
        auto paths = ConfigPaths::environment(source);
        paths.config = root.path() + "/config.json";
        if (present) writeFile(paths.config, user);
        const auto document = readConfig(paths);
        QVERIFY2(document.bundleValid, qPrintable(document.diagnostics.join('\n')));
        QCOMPARE(document.present, present);
        qmlRegisterType<LocalConfig>("AerialLock.ConfigTest", 1, 0, "LocalConfig");
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import AerialLock.ConfigTest 1.0\nLocalConfig {}", QUrl());
        QScopedPointer<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        bool loaded = false;
        QVERIFY(QMetaObject::invokeMethod(object.data(), "load", Q_RETURN_ARG(bool, loaded), Q_ARG(QString, source), Q_ARG(QString, paths.config)));
        QVERIFY(loaded);
        QCOMPARE(QJsonObject::fromVariantMap(object->property("data").toMap()), document.effective);
        QCOMPARE(QJsonObject::fromVariantMap(object->property("rawUser").toMap()), document.user);
        QCOMPARE(object->property("revision").toString(), document.revision);
        QCOMPARE(object->property("profile").toString(), document.profile);
        if (present) { QFile file(paths.config); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), user); }
        else QVERIFY(!QFileInfo::exists(paths.config));
        if (!document.userValid || !document.sourcesValid) {
            QVERIFY(!document.acquisitionAllowed());
            QVERIFY(document.effective.value("sources").toArray().isEmpty());
        }
        if (user.contains("/personal/clip")) {
            QCOMPARE(document.effective.value("sources").toArray().size(), 2);
            QVERIFY(document.migrated);
        }
        const auto sources = document.effective.value("sources").toArray();
        if (!present || (!document.user.contains("sources") && !document.migrated && document.userValid)) {
            QCOMPARE(sources.size(), 1);
            const auto inherited = sources.first().toObject();
            QCOMPARE(inherited.value("id").toString(), "apple-tv");
            QCOMPARE(inherited.value("type").toString(), "apple");
            QCOMPARE(inherited.value("safeName").toString(), "Apple-TV");
        }
        if (user.contains("old-apple")) {
            QCOMPARE(sources.size(), 1);
            QCOMPARE(sources.first().toObject().value("source").toString(), document.user.value("sources").toArray().first().toObject().value("source").toString());
        }
    }

    void inheritedSourcesAreNormalized()
    {
        QTemporaryDir root;
        const auto bundle = root.path() + "/bundle";
        QFile original(source + "/Config/defaults.json"); QVERIFY(original.open(QIODevice::ReadOnly));
        auto defaults = QJsonDocument::fromJson(original.readAll()).object();
        defaults["sources"] = QJsonArray{QJsonObject{{"type", "apple"}, {"name", "Apple TV"}, {"source", "https://example.test/catalogue"}}};
        writeFile(bundle + "/Config/defaults.json", QJsonDocument(defaults).toJson());
        QVERIFY(QFile::copy(source + "/Config/settings-schema.json", bundle + "/Config/settings-schema.json"));
        auto paths = ConfigPaths::environment(bundle); paths.config = root.path() + "/user/config.json";
        const auto inherited = readConfig(paths);
        QVERIFY(inherited.bundleValid);
        const auto entry = inherited.effective.value("sources").toArray().at(0).toObject();
        QCOMPARE(entry.value("safeName").toString(), "Apple-TV");
        QVERIFY(!entry.value("id").toString().isEmpty());
        QCOMPARE(entry.value("enabled"), QJsonValue(true));
        QVERIFY(!inherited.user.contains("sources"));
        writeFile(paths.config, R"({"sources":[]})");
        QVERIFY(readConfig(paths).effective.value("sources").toArray().isEmpty());
        writeFile(paths.config, R"({"video":"/my/file"})");
        QCOMPARE(readConfig(paths).effective.value("sources").toArray().at(0).toObject().value("type").toString(), "file");
        writeFile(paths.config, R"({"sources":false})");
        QVERIFY(!readConfig(paths).acquisitionAllowed());
        QVERIFY(readConfig(paths).effective.value("sources").toArray().isEmpty());
        // Broken shipped defaults must list all invalid leaves, not invent values.
        defaults.remove("cache"); defaults.remove("language");
        writeFile(bundle + "/Config/defaults.json", QJsonDocument(defaults).toJson());
        const auto broken = readConfig(paths);
        QVERIFY(!broken.bundleValid);
        QVERIFY(broken.diagnostics.join('\n').contains("cache.maxBytes"));
        QVERIFY(broken.diagnostics.join('\n').contains("language"));
    }

    void sourceEnablement_data()
    {
        QTest::addColumn<QString>("type"); QTest::addColumn<QJsonValue>("enabled");
        for (const auto *type : {"apple", "community", "folder", "file"}) {
            for (const auto &value : {QJsonValue(QJsonValue::Undefined), QJsonValue(true), QJsonValue(false),
                     QJsonValue("false"), QJsonValue(0), QJsonValue(QJsonValue::Null), QJsonValue(QJsonObject{})})
                QTest::newRow(qPrintable(QString(type) + '-' + QString::number(value.type()) + '-' + value.toVariant().toString())) << QString(type) << value;
        }
    }

    void sourceEnablement()
    {
        QFETCH(QString, type); QFETCH(QJsonValue, enabled);
        QTemporaryDir root;
        auto paths = ConfigPaths::environment(source); paths.config = root.path() + "/config.json";
        QJsonObject entry{{"type", type}, {"name", "Source"}, {"source", "https://example.test/pack.json"},
            {"extra", QJsonObject{{"future", 7}}}, {"minResolution", 1080}, {"maxResolution", 2160}};
        if (!enabled.isUndefined()) entry["enabled"] = enabled;
        const bool valid = enabled.isUndefined() || enabled.isBool();
        const auto raw = QJsonDocument(QJsonObject{{"sources", QJsonArray{entry}}}).toJson();
        writeFile(paths.config, raw);
        const auto doc = readConfig(paths);
        const auto normalized = doc.effective.value("sources").toArray();
        QCOMPARE(normalized.size(), valid ? 1 : 0);
        if (valid) {
            const auto item = normalized[0].toObject();
            QCOMPARE(item.value("enabled"), QJsonValue(enabled.toBool(true)));
            QCOMPARE(item.value("extra"), entry.value("extra"));
            QCOMPARE(item.value("minResolution"), entry.value("minResolution"));
            QCOMPARE(item.value("maxResolution"), entry.value("maxResolution"));
            QCOMPARE(doc.user.value("sources").toArray()[0].toObject().contains("enabled"), !enabled.isUndefined());
            entry["enabled"] = !enabled.toBool(true);
        }
        ConfigRepository repository(paths);
        const auto transaction = repository.update(repository.document().revision, {{"sources", QJsonArray{entry}}, {"language", "fr"}});
        QCOMPARE(transaction.ok(), valid);
        if (valid) {
            const auto saved = repository.document().user.value("sources").toArray()[0].toObject();
            QCOMPARE(saved.value("id"), normalized[0].toObject().value("id"));
            QCOMPARE(saved.value("extra"), entry.value("extra"));
            QVERIFY(!saved.contains("safeName"));
        } else {
            QCOMPARE(transaction.error, "InvalidSetting");
            QFile file(paths.config); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), raw);
            QVERIFY(!repository.document().user.contains("language"));
        }
    }

    void transactions()
    {
        QTemporaryDir root;
        auto paths = ConfigPaths::environment(source); paths.config = root.path() + "/user/config.json";
        const QByteArray raw = R"({"future":{"unknown":7},"panel":{"fontSize":21},"preferredResolution":1440})";
        writeFile(paths.config, raw);
        ConfigRepository first(paths), second(paths);
        const auto old = first.document().revision;
        const auto saved = first.update(old, {{"powerActions", QJsonObject{{"enabled", false}}}});
        QVERIFY2(saved.ok(), qPrintable(saved.error));
        QCOMPARE(second.update(old, {{"language", "fr"}}).error, "Conflict");
        QCOMPARE(readConfig(paths).user.value("future").toObject().value("unknown").toInt(), 7);
        QCOMPARE(readConfig(paths).user.value("panel").toObject().value("fontSize").toInt(), 21);
        QCOMPARE(readConfig(paths).user.value("preferredResolution").toInt(), 1440);
        QVERIFY(first.observeHeight(2160).ok());
        QCOMPARE(readConfig(paths).user.value("preferredResolution").toInt(), 1440);
        auto rejected = first.update(first.document().revision, {{"cache", QJsonObject{{"maxBytes", -1}}}, {"language", "fr"}});
        QCOMPARE(rejected.error, "InvalidSetting");
        QVERIFY(!readConfig(paths).user.contains("language"));
        QCOMPARE(first.update(first.document().revision, {{"newUnknown", true}}).error, "InvalidSetting");
        QVERIFY(first.update(first.document().revision, {{"powerActions", QJsonValue::Null}}).ok());
        QVERIFY(!readConfig(paths).user.contains("powerActions"));
        struct stat mode; QVERIFY(::stat(QFile::encodeName(paths.config).constData(), &mode) == 0);
        QCOMPARE(mode.st_mode & 0777, mode_t(0600));
        writeFile(paths.config, "{ invalid"); first.reload();
        QCOMPARE(first.update(first.document().revision, {{"language", "fr"}}).error, "InvalidSetting");
        QFile file(paths.config); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("{ invalid"));
    }

    void preferenceMigrationAndWatcher()
    {
        QTemporaryDir root;
        auto paths = ConfigPaths::environment(source); paths.config = root.path() + "/nested/config.json";
        ConfigRepository repository(paths);
        const auto absent = repository.document().revision;
        QVERIFY(repository.observeHeight(0).ok());
        QVERIFY(!QFileInfo::exists(paths.config));
        QVERIFY(repository.observeHeight(1440).ok());
        QCOMPARE(repository.document().user, QJsonObject({{"preferredResolution", 1080}}));
        QSignalSpy changed(&repository, &ConfigRepository::changed);
        writeFile(paths.config, R"({"video":"/my/clip","unknown":9})");
        QTRY_VERIFY_WITH_TIMEOUT(repository.document().user.contains("video"), 2000);
        QVERIFY(changed.count() > 0);
        QVERIFY(repository.migrate().ok());
        QVERIFY(!repository.document().user.contains("video"));
        QCOMPARE(repository.document().user.value("unknown").toInt(), 9);
        QCOMPARE(repository.document().effective.value("sources").toArray().size(), 1);
        QVERIFY(repository.document().revision != absent);
        const auto revision = repository.document().revision;
        QFile f(paths.config); QVERIFY(f.open(QIODevice::ReadOnly)); const auto bytes = f.readAll(); f.close();
        writeFile(paths.config, bytes + "\n");
        QTRY_VERIFY_WITH_TIMEOUT(repository.document().revision != revision, 2000);
    }

    void sourceIdentityEdits()
    {
        QTemporaryDir root;
        auto paths = ConfigPaths::environment(source); paths.config = root.path() + "/config.json";
        writeFile(paths.config, "{\"sources\":[{\"type\":\"apple\",\"name\":\"Old\",\"source\":\"https://example.test/old\"}]}");
        ConfigRepository repository(paths);
        const auto original = repository.document().effective.value("sources").toArray().at(0).toObject().value("id");
        QVERIFY(repository.update(repository.document().revision, {{"sources", QJsonArray{QJsonObject{{"type","apple"},{"name","New"},{"source","https://example.test/old"}}}}}).ok());
        QCOMPARE(repository.document().user.value("sources").toArray().at(0).toObject().value("id"), original);
        QVERIFY(repository.update(repository.document().revision, {{"sources", QJsonArray{QJsonObject{{"type","apple"},{"name","New"},{"source","https://example.test/new"}}}}}).ok());
        QCOMPARE(repository.document().user.value("sources").toArray().at(0).toObject().value("id"), original);
        QVERIFY(repository.update(repository.document().revision, {{"sources", QJsonArray{QJsonObject{{"type","folder"},{"name","Personal"},{"source","/personal"}}}}}).ok());
        QVERIFY(repository.document().user.value("sources").toArray().at(0).toObject().contains("id"));
    }

    void powerSettings_data()
    {
        QTest::addColumn<QJsonValue>("value"); QTest::addColumn<bool>("expected"); QTest::addColumn<int>("warnings");
        QTest::newRow("missing") << QJsonValue(QJsonValue::Undefined) << true << 0;
        QTest::newRow("partial") << QJsonValue(QJsonObject{}) << true << 0;
        QTest::newRow("false") << QJsonValue(QJsonObject{{"enabled",false}}) << false << 0;
        QTest::newRow("true") << QJsonValue(QJsonObject{{"enabled",true}}) << true << 0;
        QTest::newRow("null-object") << QJsonValue(QJsonValue::Null) << true << 1;
        QTest::newRow("array-object") << QJsonValue(QJsonArray{}) << true << 1;
        QTest::newRow("scalar-object") << QJsonValue(false) << true << 1;
        for (const auto &v : {QJsonValue("false"), QJsonValue(QJsonValue::Null), QJsonValue(0)})
            QTest::newRow(qPrintable(QJsonDocument(QJsonArray{v}).toJson(QJsonDocument::Compact))) << QJsonValue(QJsonObject{{"enabled",v}}) << true << 1;
        QTest::newRow("siblings") << QJsonValue(QJsonObject{{"enabled",false},{"extra",9}}) << false << 0;
    }

    void powerSettings()
    {
        QFETCH(QJsonValue, value); QFETCH(bool, expected); QFETCH(int, warnings);
        QTemporaryDir root; auto paths = ConfigPaths::environment(source); paths.config = root.path() + "/config.json";
        QJsonObject user{{"idle", QJsonObject{{"timeoutSeconds",42}}}};
        if (!value.isUndefined()) user["powerActions"] = value;
        const auto bytes = QJsonDocument(user).toJson(); writeFile(paths.config, bytes);
        const auto document = readConfig(paths);
        QCOMPARE(document.effective.value("powerActions").toObject().value("enabled").toBool(), expected);
        QCOMPARE(document.diagnostics.size(), warnings);
        QCOMPARE(document.effective.value("idle").toObject().value("timeoutSeconds").toInt(), 42);
        if (value.toObject().contains("extra")) QCOMPARE(document.effective.value("powerActions").toObject().value("extra").toInt(), 9);
        QFile file(paths.config); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), bytes);
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc == 4 && QString::fromLocal8Bit(argv[1]) == "--read") {
        LocalConfig local;
        if (!local.load(QString::fromLocal8Bit(argv[2]), QString::fromLocal8Bit(argv[3]))) return 1;
        const auto bytes = QJsonDocument(QJsonObject::fromVariantMap(local.data())).toJson(QJsonDocument::Compact);
        fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout);
        return 0;
    }
    if (argc != 2) return 2;
    ConfigTests tests; tests.source = QString::fromLocal8Bit(argv[1]);
    char *args[] = {argv[0]};
    return QTest::qExec(&tests, 1, args);
}
#include "companion-config-probe.moc"
