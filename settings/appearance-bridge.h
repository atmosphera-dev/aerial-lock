#pragma once
#include <QColor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QObject>
#include <QVariantMap>

class QQmlEngine;
namespace InterActive { class Appearance; }
namespace Aerial::Settings {
// The only application writer for this engine's IA singleton.
class AppearanceBridge : public QObject {
    Q_OBJECT
public:
    explicit AppearanceBridge(QQmlEngine &engine, QDBusConnection bus = QDBusConnection::sessionBus());
    QVariantMap input() const;
protected:
    bool eventFilter(QObject *, QEvent *event) override;
private slots:
    void settingChanged(const QString &group, const QString &key, const QDBusVariant &value, const QDBusMessage &message);
private:
    void adopt(const QString &owner);
    void read(const QString &key);
    void accept(const QString &key, const QVariant &value);
    void publish();
    QDBusConnection bus;
    InterActive::Appearance *appearance;
    QString owner;
    quint64 generation = 0;
    QMap<QString, quint64> serials;
    uint scheme = 0;
    QColor accent;
};
}
