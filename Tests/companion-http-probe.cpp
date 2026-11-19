#include "../companion/http-operation.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QSslCertificate>
#include <QSslConfiguration>
#include <QSslError>
#include <cstdio>

using namespace Aerial;

static QByteArray read(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) qFatal("missing HTTP fixture input");
    return file.readAll();
}

// Only DNS/TCP routing is replaced. Real Qt TLS validates the original peer
// name, certificate chain and each production request's own trust settings.
// Qt documents this seam at QNetworkRequest::setPeerVerifyName.
class LoopbackTransport : public QNetworkAccessManager {
public:
    QJsonArray &requests;
    int port, attempt;
    LoopbackTransport(QJsonArray &requests, int port, int attempt, QObject *parent)
        : QNetworkAccessManager(parent), requests(requests), port(port), attempt(attempt)
    {
        setProxy(QNetworkProxy::NoProxy);
    }
protected:
    QNetworkReply *createRequest(Operation operation, const QNetworkRequest &request, QIODevice *data) override
    {
        const auto host = request.url().host();
        if (host != "sylvan.apple.com" && host != "custom.test") qFatal("HTTP fixture refused an unmapped peer");
        requests.append(QJsonObject{{"host", host}, {"path", request.url().path()}, {"attempt", attempt},
            {"caCount", request.sslConfiguration().caCertificates().size()}});
        auto routed = request;
        auto url = request.url(); url.setHost("127.0.0.1"); url.setPort(port);
        routed.setUrl(url); routed.setPeerVerifyName(host);
        routed.setRawHeader("Host", host.toLatin1());
        return QNetworkAccessManager::createRequest(operation, routed, data);
    }
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 2) return 2;
    const auto fixture = QJsonDocument::fromJson(read(QString::fromLocal8Bit(argv[1]))).object();
    auto ssl = QSslConfiguration::defaultConfiguration();
    ssl.addCaCertificates(QSslCertificate::fromData(read(fixture.value("normalRoot").toString())));
    QSslConfiguration::setDefaultConfiguration(ssl);
    const auto root = read(fixture.value("fallbackRoot").toString());
    QJsonArray requests, tlsErrors;
    int attempts = 0;
    HttpTransport transport([&](QObject *parent) {
        auto *manager = new LoopbackTransport(requests, fixture.value("port").toInt(), ++attempts, parent);
        QObject::connect(manager, &QNetworkAccessManager::sslErrors, manager,
            [&tlsErrors](QNetworkReply *, const QList<QSslError> &errors) {
                QJsonArray attempt;
                for (const auto &error : errors) attempt.append(int(error.error()));
                tlsErrors.append(attempt);
            });
        return manager;
    });
    HttpRequest request; request.url = QUrl(fixture.value("url").toString());
    QByteArray body;
    HttpOperation operation(transport, request, root, {}, [&body](const QByteArray &chunk) {
        body += chunk; return QString();
    });
    QEventLoop loop;
    QObject::connect(&operation, &HttpOperation::finished, &loop, &QEventLoop::quit);
    QTimer timeout; timeout.setSingleShot(true);
    QObject::connect(&timeout, &QTimer::timeout, &operation, [&operation] { operation.cancel("FixtureTimeout"); });
    timeout.start(15000);
    operation.start(); loop.exec();
    const auto result = operation.result();
    const QJsonObject output{{"error", result.error}, {"status", result.status}, {"body", QString::fromLatin1(body)},
        {"requests", requests}, {"tlsErrors", tlsErrors},
        {"defaultTrustUnchanged", QSslConfiguration::defaultConfiguration() == ssl}};
    const auto bytes = QJsonDocument(output).toJson(QJsonDocument::Compact);
    fwrite(bytes.constData(), 1, size_t(bytes.size()), stdout);
    return 0;
}
