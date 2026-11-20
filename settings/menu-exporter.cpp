#include "menu-exporter.h"
#include <QApplication>
#include <QBuffer>
#include <QEvent>
#include <QTimer>

namespace Aerial::Settings {
MenuExporter::MenuExporter(QMenu &menu, QObject *parent) : QObject(parent), menu(menu) { registerTrayTypes(); collect(menu); update(); }
QString MenuExporter::textDirection() const { return QApplication::isRightToLeft() ? "rtl" : "ltr"; }
int MenuExporter::itemId(QAction *action) const
{
    for (auto it = actions.begin(); it != actions.end(); ++it) if (it.value() == action) return it.key();
    return -1;
}
void MenuExporter::collect(QMenu &branch)
{
    branch.installEventFilter(this);
    for (auto *action : branch.actions()) {
        if (itemId(action) < 0) {
            actions[nextId++] = action;
            connect(action, &QAction::changed, this, &MenuExporter::update);
            connect(action, &QObject::destroyed, this, [this] { QTimer::singleShot(0, this, &MenuExporter::update); });
        }
        if (action->menu()) collect(*action->menu());
    }
}
bool MenuExporter::eventFilter(QObject *, QEvent *event)
{
    if (event->type() == QEvent::ActionAdded || event->type() == QEvent::ActionRemoved)
        QTimer::singleShot(0, this, [this] { collect(menu); update(); });
    return false;
}
QVariantMap MenuExporter::properties(int id, const QStringList &names) const
{
    auto *action = actions.value(id).data();
    QVariantMap p;
    if (!id) p = {{"children-display", "submenu"}};
    else if (action) {
        p = {{"label", QString(action->text()).replace('_', "__")}, {"enabled", action->isEnabled()}, {"visible", action->isVisible()}};
        if (action->isSeparator()) p["type"] = "separator";
        if (action->menu()) p["children-display"] = "submenu";
        if (action->isCheckable()) { p["toggle-type"] = "checkmark"; p["toggle-state"] = action->isChecked() ? 1 : 0; }
        if (!action->icon().isNull()) {
            QByteArray bytes; QBuffer buffer(&bytes); buffer.open(QIODevice::WriteOnly);
            action->icon().pixmap(20, 20).save(&buffer, "PNG"); p["icon-data"] = bytes;
        }
    }
    if (!names.isEmpty()) for (auto it = p.begin(); it != p.end();) { if (!names.contains(it.key())) it = p.erase(it); else ++it; }
    return p;
}
MenuLayout MenuExporter::layout(int id, int depth, const QStringList &names) const
{
    MenuLayout result{id, properties(id, names), {}};
    auto *branch = id == 0 ? &menu : actions.value(id) ? actions.value(id)->menu() : nullptr;
    if (branch && depth != 0) for (auto *action : branch->actions()) result.children << QDBusVariant(QVariant::fromValue(layout(itemId(action), depth < 0 ? -1 : depth - 1, names)));
    return result;
}
void MenuExporter::update()
{
    QList<int> order;
    std::function<void(QMenu &)> visit = [&](QMenu &branch) { for (auto *action : branch.actions()) { order << itemId(action); if (action->menu()) visit(*action->menu()); } };
    visit(menu);
    MenuPropertiesList changed; MenuRemovedList removed;
    for (int id : order) {
        auto p = properties(id); if (previous.value(id) == p) continue;
        QStringList missing; for (auto it = previous[id].begin(); it != previous[id].end(); ++it) if (!p.contains(it.key())) missing << it.key();
        changed << MenuProperties{id, p}; if (!missing.isEmpty()) removed << MenuRemoved{id, missing}; previous[id] = p;
    }
    if (!changed.isEmpty() || !removed.isEmpty()) emit ItemsPropertiesUpdated(changed, removed);
    if (order != previousOrder) { previousOrder = order; emit LayoutUpdated(++revision, 0); }
}
uint MenuExporter::GetLayout(int id, int depth, const QStringList &names, MenuLayout &out)
{
    if (id && !actions.value(id)) { if (calledFromDBus()) sendErrorReply("com.canonical.dbusmenu.Error.InvalidMenuItem", "Unknown item"); return revision; }
    out = layout(id, depth, names); return revision;
}
MenuPropertiesList MenuExporter::GetGroupProperties(const MenuIds &ids, const QStringList &names)
{
    MenuPropertiesList out; const auto selected = ids.isEmpty() ? previousOrder : ids;
    for (int id : selected) if (id == 0 || actions.value(id)) out << MenuProperties{id, properties(id, names)};
    return out;
}
QDBusVariant MenuExporter::GetProperty(int id, const QString &name)
{
    const auto p = properties(id);
    if (!p.contains(name)) { if (calledFromDBus()) sendErrorReply("com.canonical.dbusmenu.Error.InvalidProperty", "Unknown item or property"); return QDBusVariant(QString()); }
    return QDBusVariant(p.value(name));
}
void MenuExporter::Event(int id, const QString &event, const QDBusVariant &, uint)
{
    // The root has lifecycle events but no QAction. Hosts send opened/closed
    // to item 0 when loading and displaying the menu.
    if (id == 0) return;
    auto action = actions.value(id);
    if (!action) { if (calledFromDBus()) sendErrorReply("com.canonical.dbusmenu.Error.InvalidMenuItem", "Unknown item"); return; }
    // Queue after the D-Bus reply: Hide may retire this exporter/connection.
    if (event == "clicked" && action->isEnabled() && action->isVisible() && !action->isSeparator())
        QTimer::singleShot(0, action, [action] { if (action) action->trigger(); });
}
MenuIds MenuExporter::EventGroup(const MenuEvents &events)
{
    MenuIds errors;
    for (const auto &event : events) { if (event.id && !actions.value(event.id)) errors << event.id; else Event(event.id, event.event, event.data, event.timestamp); }
    return errors;
}
bool MenuExporter::AboutToShow(int id) { if (id && !actions.value(id) && calledFromDBus()) sendErrorReply("com.canonical.dbusmenu.Error.InvalidMenuItem", "Unknown item"); return false; }
MenuIds MenuExporter::AboutToShowGroup(const MenuIds &ids, MenuIds &errors)
{
    for (int id : ids) if (id && !actions.value(id)) errors << id;
    return {};
}
}
