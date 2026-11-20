#pragma once
#include <QDBusConnection>
#include <QDBusMessage>
#include <QObject>
#include <QVariantMap>

namespace Aerial::Settings {
class PortalClient : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit PortalClient(QDBusConnection bus, QObject *parent = nullptr);
    ~PortalClient() override;
    QString error() const { return problem; }
    quint64 choose(bool directory);
    Q_INVOKABLE void openFolder(const QString &path);
    Q_INVOKABLE void openLink(const QString &url);
    void cancel(quint64 id);
signals:
    void changed();
    void finished(quint64 id, const QString &path, bool cancelled, const QString &error);
private slots:
    void response(uint code, const QVariantMap &results, const QDBusMessage &message);
    void ownerChanged(const QString &, const QString &, const QString &);
private:
    struct Request { quint64 id; QString path, owner; bool chooser; };
    quint64 dispatch(const QString &interface, const QString &method, QVariantList arguments, bool chooser);
    void complete(const QString &path, const QString &selected, bool cancelled, const QString &error);
    QDBusConnection bus;
    QMap<QString, Request> requests;
    QMap<QString, Request> cancelledRequests;
    quint64 sequence = 0;
    QString problem;
};
}
