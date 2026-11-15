#pragma once

#include <QObject>
#include <QSocketNotifier>
#include <QTimer>
#include <QString>

// Owns only authentication transport/lifecycle. The Lock controller owns user
// interaction, status projection and the inactivity watchdog.
class AuthContext : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString config MEMBER service NOTIFY configChanged)
    Q_PROPERTY(QString configDirectory MEMBER directory NOTIFY configDirectoryChanged)
    Q_PROPERTY(QString user MEMBER username NOTIFY userChanged)
    Q_PROPERTY(bool active READ active NOTIFY activeChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)
    Q_PROPERTY(bool messageIsError READ messageIsError NOTIFY messageChanged)
    Q_PROPERTY(bool responseRequired READ responseRequired NOTIFY messageChanged)
    Q_PROPERTY(bool responseVisible READ responseVisible NOTIFY messageChanged)
public:
    enum Result { Success = 0, Failed = 1, Error = 2, MaxTries = 3 };
    Q_ENUM(Result)
    explicit AuthContext(QObject *parent = nullptr);
    // Native test seam, never configurable from QML or the environment.
    AuthContext(const QString &helper, QObject *parent);
    ~AuthContext() override;
    bool active() const { return pid > 0; }
    QString message() const { return text; }
    bool messageIsError() const { return isError; }
    bool responseRequired() const { return required; }
    bool responseVisible() const { return visible; }
    Q_INVOKABLE bool start();
    Q_INVOKABLE void respond(const QString &response);
    Q_INVOKABLE void abort();
    QString service = QStringLiteral("login");
    QString directory = QStringLiteral("/etc/pam.d");
    QString username;
signals:
    void configChanged();
    void configDirectoryChanged();
    void userChanged();
    void activeChanged();
    void messageChanged();
    void pamMessage();
    void completed(int result);
    void error(int error);
private:
    void receive();
    void reap();
    void fail();
    bool frame(const QByteArray &line);
    void reset();
    QString executable;
    QString text;
    bool isError = false, required = false, visible = false;
    int pid = 0, fd = -1, terminal = -1;
    quint64 generation = 0;
    QByteArray input;
    QSocketNotifier *notifier = nullptr;
    QTimer timer;
};
