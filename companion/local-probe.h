#pragma once
#include <QJsonObject>
#include <QProcess>
#include <QTimer>

namespace Aerial {
class LocalProbe : public QObject {
    Q_OBJECT
public:
    explicit LocalProbe(const QString &executable, QObject *parent = nullptr);
    ~LocalProbe();
    void start(int ownedReadOnlyFd);
    void cancel();
    QJsonObject result() const { return output; }
signals:
    void finished();
private:
    void settle(bool success);
    QString executable;
    QProcess child;
    QTimer deadline;
    QByteArray bytes;
    QJsonObject output;
    int file = -1;
    bool terminal = false;
};
}
