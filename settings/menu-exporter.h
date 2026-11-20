#pragma once
#include "tray-types.h"
#include <QDBusContext>
#include <QMenu>
#include <QPointer>

namespace Aerial::Settings {
// A protocol projection, not an action model. QAction remains the source of truth.
class MenuExporter : public QObject, protected QDBusContext {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.canonical.dbusmenu")
    Q_PROPERTY(uint Version READ version CONSTANT)
    Q_PROPERTY(QString TextDirection READ textDirection CONSTANT)
    Q_PROPERTY(QString Status READ status CONSTANT)
    Q_PROPERTY(QStringList IconThemePath READ iconThemePath CONSTANT)
public:
    explicit MenuExporter(QMenu &menu, QObject *parent = nullptr);
    uint version() const { return 4; }
    QString textDirection() const;
    QString status() const { return "normal"; }
    QStringList iconThemePath() const { return {}; }
    void update();
public slots:
    uint GetLayout(int parentId, int recursionDepth, const QStringList &propertyNames, Aerial::Settings::MenuLayout &layout);
    Aerial::Settings::MenuPropertiesList GetGroupProperties(const Aerial::Settings::MenuIds &ids, const QStringList &propertyNames);
    QDBusVariant GetProperty(int id, const QString &name);
    void Event(int id, const QString &eventId, const QDBusVariant &data, uint timestamp);
    Aerial::Settings::MenuIds EventGroup(const Aerial::Settings::MenuEvents &events);
    bool AboutToShow(int id);
    Aerial::Settings::MenuIds AboutToShowGroup(const Aerial::Settings::MenuIds &ids, Aerial::Settings::MenuIds &idErrors);
signals:
    void LayoutUpdated(uint revision, int parent);
    void ItemsPropertiesUpdated(const Aerial::Settings::MenuPropertiesList &updatedProps, const Aerial::Settings::MenuRemovedList &removedProps);
    void ItemActivationRequested(int id, uint timestamp);
private:
    bool eventFilter(QObject *watched, QEvent *event) override;
    MenuLayout layout(int id, int depth, const QStringList &names) const;
    QVariantMap properties(int id, const QStringList &names = {}) const;
    void collect(QMenu &menu);
    int itemId(QAction *action) const;
    QMenu &menu;
    QMap<int, QPointer<QAction>> actions;
    QMap<int, QVariantMap> previous;
    QList<int> previousOrder;
    uint revision = 1;
    int nextId = 1;
};
}
