#include "tray-controller.h"
#include "async-call.h"
#include "common/client/client-support.h"
#include <QApplication>
#include <QDBusConnectionInterface>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QEvent>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QUuid>
#include <QtEndian>
#include <utility>

namespace Aerial::Settings {
namespace {
constexpr auto Watcher = "org.kde.StatusNotifierWatcher";
constexpr auto WatcherPath = "/StatusNotifierWatcher";
QImage artwork(int size, const QString &badge)
{
    QImage image(size, size, QImage::Format_ARGB32); image.fill(Qt::transparent);
    QPainter p(&image); p.setRenderHint(QPainter::Antialiasing); p.scale(size / 24., size / 24.);
    const auto palette = QApplication::palette(); const auto ink = palette.color(QPalette::WindowText), background = palette.color(QPalette::Window);
    p.setPen(QPen(ink, 1.75, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    QPainterPath mountains; mountains.moveTo(1.5, 20); mountains.lineTo(8.5, 10); mountains.lineTo(13.5, 16.5); mountains.lineTo(16.5, 13); mountains.lineTo(22.5, 20); p.drawPath(mountains);
    p.drawEllipse(QPointF(16.5, 5.5), 2.75, 2.75);
    if (badge != "ready") {
        p.setPen(QPen(background, 2)); p.setBrush(badge == "problem" ? QColor(background.lightnessF() < .5 ? "#fd4663" : "#c81d3c") : ink); p.drawEllipse(QPointF(18.5, 18.5), 5.25, 5.25);
        p.setBrush(Qt::NoBrush); p.setPen(QPen(badge == "problem" ? QColor(Qt::white) : background, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (badge == "downloading") { p.drawLine(QPointF(18.5, 15.6), QPointF(18.5, 21)); p.drawLine(QPointF(16.3, 18.8), QPointF(18.5, 21)); p.drawLine(QPointF(18.5, 21), QPointF(20.7, 18.8)); }
        else if (badge == "paused") { p.drawLine(QPointF(17, 16.3), QPointF(17, 20.7)); p.drawLine(QPointF(20, 16.3), QPointF(20, 20.7)); }
        else { p.drawLine(QPointF(18.5, 15.8), QPointF(18.5, 19)); p.drawPoint(QPointF(18.5, 21.2)); }
    }
    return image;
}
TrayImages pixmaps(const QString &badge)
{
    TrayImages images;
    for (int size : {16, 22, 24, 32, 44, 48, 64}) {
        auto image = artwork(size, badge); QByteArray bytes(size * size * 4, '\0');
        for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) qToBigEndian<quint32>(image.pixel(x, y), bytes.data() + (y * size + x) * 4);
        images << TrayImage{size, size, bytes};
    }
    return images;
}
bool same(const TrayImages &a, const TrayImages &b)
{
    if (a.size() != b.size()) return false;
    for (int i = 0; i < a.size(); ++i) if (a[i].width != b[i].width || a[i].height != b[i].height || a[i].bytes != b[i].bytes) return false;
    return true;
}
QIcon actionIcon(const QString &name)
{
    QPixmap image(24, 24); image.fill(Qt::transparent); QPainter p(&image); p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QApplication::palette().color(QPalette::WindowText), 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (name == "pause") { p.drawLine(8, 5, 8, 19); p.drawLine(16, 5, 16, 19); }
    else if (name == "stop") p.drawRoundedRect(QRectF(6, 6, 12, 12), 1, 1);
    else if (name == "play") { QPolygonF points{QPointF(8, 5), QPointF(19, 12), QPointF(8, 19), QPointF(8, 5)}; p.drawPolyline(points); }
    else if (name == "download") { p.drawLine(12, 3, 12, 16); p.drawLine(7, 11, 12, 16); p.drawLine(17, 11, 12, 16); p.drawLine(4, 18, 4, 21); p.drawLine(4, 21, 20, 21); p.drawLine(20, 21, 20, 18); }
    else if (name == "cache") { p.drawEllipse(QRectF(4, 3, 16, 6)); p.drawLine(4, 6, 4, 18); p.drawLine(20, 6, 20, 18); p.drawArc(QRectF(4, 15, 16, 6), 180 * 16, 180 * 16); p.drawArc(QRectF(4, 9, 16, 6), 180 * 16, 180 * 16); }
    else if (name == "hide") { QPainterPath eye; eye.moveTo(2, 12); eye.cubicTo(8, 3, 16, 3, 22, 12); eye.cubicTo(16, 21, 8, 21, 2, 12); p.drawPath(eye); p.drawEllipse(QPointF(12, 12), 3, 3); p.drawLine(3, 3, 21, 21); }
    else { p.drawEllipse(QPointF(12, 12), 7, 7); p.drawEllipse(QPointF(12, 12), 2.5, 2.5); for (int i = 0; i < 8; ++i) { p.save(); p.translate(12, 12); p.rotate(i * 45); p.drawLine(0, 7, 0, 10); p.restore(); } }
    return QIcon(image);
}
}
TrayItem::TrayItem(QMenu &actions, const QString &watcher, QObject *parent)
    : QObject(parent), connectionName("aerial-sni-" + QUuid::createUuid().toString(QUuid::WithoutBraces)),
      bus(QDBusConnection::connectToBus(QDBusConnection::SessionBus, connectionName)), actions(actions), menu(actions)
{
    registerTrayTypes();
    const bool exported = bus.registerObject("/StatusNotifierItem", this, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllProperties | QDBusConnection::ExportAllSignals)
        && bus.registerObject("/MenuBar", &menu, QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllProperties | QDBusConnection::ExportAllSignals);
    QTimer::singleShot(0, this, [this, watcher, exported] {
        if (!exported) { emit registrationFailed(); return; }
        auto call = QDBusMessage::createMethodCall(watcher, WatcherPath, Watcher, "RegisterStatusNotifierItem"); call << QString("/StatusNotifierItem"); call.setAutoStartService(false);
        watch(this, bus.asyncCall(call, 5000), [this](const QDBusMessage &reply) { if (reply.type() == QDBusMessage::ErrorMessage) emit registrationFailed(); });
    });
}
TrayItem::~TrayItem() { bus.unregisterObject("/StatusNotifierItem"); bus.unregisterObject("/MenuBar"); QDBusConnection::disconnectFromBus(connectionName); }
void TrayItem::update(const QString &status, const TrayImages &images, const TrayImages &attention, const QString &nextText)
{
    if (currentStatus != status) { currentStatus = status; emit NewStatus(status); }
    const bool iconChanged = !same(currentImages, images);
    if (iconChanged) { currentImages = images; emit NewIcon(); }
    if (!same(currentAttention, attention)) { currentAttention = attention; emit NewAttentionIcon(); }
    if (text != nextText || iconChanged) { text = nextText; emit NewToolTip(); }
}
void TrayItem::ProvideXdgActivationToken(const QString &token) { activationToken = token; }
void TrayItem::Activate(int, int) { const auto token = std::exchange(activationToken, {}); emit openRequested(token.isEmpty() ? QVariantMap{} : QVariantMap{{"activation-token", token}}); }
void TrayItem::SecondaryActivate(int x, int y) { Activate(x, y); }
void TrayItem::ContextMenu(int x, int y)
{
    // Wayland needs an input-bearing transient parent for a grabbing popup;
    // a tray-only item has none. Those hosts render /MenuBar instead. Neither
    // that unsupported route nor headless platforms imply primary activation.
    const auto platform = QGuiApplication::platformName();
    if (!platform.startsWith("wayland") && platform != "offscreen" && platform != "minimal") actions.popup(QPoint(x, y));
}
void TrayItem::Scroll(int, const QString &) {} // No scrolling action is advertised.

TrayController::TrayController(SettingsController &settings, QDBusConnection bus, QObject *parent, std::function<bool()> desktopAvailable)
    : QObject(parent), settings(settings), bus(std::move(bus)), desktopAvailable(std::move(desktopAvailable))
{
    if (!this->desktopAvailable) this->desktopAvailable = [] { return QSystemTrayIcon::isSystemTrayAvailable(); };
    summary = menu.addAction(settings.status()); menu.addSeparator(); transfer = menu.addAction("Fetch now"); pause = menu.addAction("Pause downloads"); menu.addSeparator(); open = menu.addAction("Open settings"); hide = menu.addAction("Hide tray icon");
    connect(summary, &QAction::triggered, this, [this] { emit openRequested(2, {}); });
    connect(open, &QAction::triggered, this, [this] { emit openRequested(-1, {}); });
    connect(transfer, &QAction::triggered, this, [this] {
        if (this->settings.panel() == "settings") this->settings.dispatchAction(this->settings.statusModel()->facts().value("primaryActionId").toString());
        else { emit openRequested(2, {}); this->settings.dispatchAction("retry-service"); }
    });
    connect(pause, &QAction::triggered, this, [this] {
        if (this->settings.panel() == "settings") this->settings.dispatchAction(this->settings.statusModel()->facts().value("secondaryActionId").toString());
        else emit openRequested(4, {});
    });
    connect(hide, &QAction::triggered, this, [this] { setEnabled(false); });
    connect(&settings, &SettingsController::changed, this, &TrayController::sample);
    connect(&settings, &SettingsController::trayChangeRequested, this, &TrayController::setEnabled);
    connect(settings.systemd(), &SystemdController::changed, this, &TrayController::publish);
    connect(settings.systemd(), &SystemdController::operationFinished, this, [this](const QString &unit, const QString &, bool success) {
        if (unit != "aerial-tray.service" || !persistencePending) return;
        persistencePending = false;
        if (pendingDesired != desired) { persist(); return; }
        // Failed persistence must not disappear with a tray-only process.
        if (!success) emit openRequested(4, {});
        publish(); emit changed();
    });
    auto *watcher = new QDBusServiceWatcher(Watcher, this->bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, [this](const QString &, const QString &, const QString &next) {
        ++generation; hostPending = hostDirty = false; item.reset(); watcherOwner = next; available = false;
        if (next.isEmpty()) reconcile(); else hostChanged();
    });
    for (const auto &signal : {"StatusNotifierHostRegistered", "StatusNotifierHostUnregistered"})
        this->bus.connect(Watcher, WatcherPath, Watcher, signal, this, SLOT(hostChanged()));
    // Qt's public detection is also sampled, but never instantiate its exporter.
    auto *timer = new QTimer(this); timer->setInterval(2000); connect(timer, &QTimer::timeout, this, &TrayController::hostChanged); timer->start();
    qApp->installEventFilter(this);
    discover(); sample();
}
bool TrayController::eventFilter(QObject *, QEvent *event)
{
    if (event->type() == QEvent::ApplicationPaletteChange) sample();
    return false;
}
void TrayController::discover()
{
    const auto epoch = ++generation; hostPending = hostDirty = false;
    discoverOwner(this, bus, QString(Watcher), [this, epoch] { return epoch == generation; }, [this](const QDBusMessage &reply) {
        const QDBusPendingReply<QString> owner(reply); const auto next = owner.isError() ? QString() : owner.value();
        if (next != watcherOwner) { item.reset(); watcherOwner = next; available = false; }
        if (next.isEmpty()) reconcile(); else hostChanged();
    });
}
void TrayController::hostChanged()
{
    if (watcherOwner.isEmpty()) return;
    if (hostPending) { hostDirty = true; return; }
    hostPending = true;
    const auto epoch = generation;
    auto call = QDBusMessage::createMethodCall(watcherOwner, WatcherPath, "org.freedesktop.DBus.Properties", "Get"); call << QString(Watcher) << QString("IsStatusNotifierHostRegistered"); call.setAutoStartService(false);
    watch(this, bus.asyncCall(call, 5000), [this, epoch](const QDBusMessage &reply) {
        if (epoch != generation) return;
        hostPending = false;
        const QDBusPendingReply<QDBusVariant> host(reply);
        // SNI is the promised transport, not an XEmbed fallback. Qt can report
        // XEmbed availability too, so the protocol property remains authoritative.
        available = desktopAvailable() && !host.isError() && host.value().variant().toBool(); reconcile();
        if (std::exchange(hostDirty, false)) hostChanged();
    });
}
void TrayController::show() { if (explicitlyHidden) return; desired = true; reconcile(); }
void TrayController::setEnabled(bool on)
{
    desired = on;
    explicitlyHidden = !on;
    // Serialize persistence, but reflect each user intent immediately. A late
    // enable/start completion must not overwrite a newer Hide request.
    const bool pending = persistencePending;
    persistencePending = true; reconcile();
    if (!pending) { persistencePending = false; persist(); }
}
void TrayController::persist()
{
    persistencePending = true; pendingDesired = desired;
    settings.systemd()->operate("aerial-tray.service", desired ? "enableStart" : "disable");
}
void TrayController::reconcile()
{
    if (!desired || !available) item.reset();
    else if (!item) {
        item = std::make_unique<TrayItem>(menu, watcherOwner);
        connect(item.get(), &TrayItem::openRequested, this, [this](const QVariantMap &platform) { emit openRequested(-1, platform); });
        connect(item.get(), &TrayItem::registrationFailed, this, [this, failed = QPointer<TrayItem>(item.get())] {
            if (!failed || item.get() != failed) return;
            problem = "Couldn't show the tray icon. Check the desktop tray, then try again."; item.reset(); publish();
        }, Qt::QueuedConnection);
        problem.clear(); sample();
    }
    publish(); emit changed();
}
bool TrayController::needsAttention(const QVariantMap &p)
{
    return StatusModel::needsAttention(p);
}
QString TrayController::badge(const QVariantMap &p, bool downloading, bool paused)
{
    return StatusModel::badge(p, downloading, paused);
}
void TrayController::sample()
{
    const bool ready = settings.panel() == "settings", downloading = settings.cache()->facts().value("downloading").toBool();
    const bool paused = !settings.controls().value("automatic").toMap().value("on").toBool();
    summary->setText(settings.status()); summary->setIcon(actionIcon("cache"));
    transfer->setText(!ready ? "Open service recovery" : downloading ? "Stop download" : "Fetch now"); transfer->setIcon(actionIcon(downloading ? "stop" : "download"));
    pause->setText(!ready ? "Open settings" : paused ? "Resume downloads" : "Pause downloads"); pause->setIcon(actionIcon(paused ? "play" : "pause"));
    open->setIcon(actionIcon("settings")); hide->setIcon(actionIcon("hide"));
    if (item) {
        const auto facts = settings.statusModel()->facts();
        item->update(facts.value("attention").toBool() ? "NeedsAttention" : "Active", pixmaps(facts.value("badge").toString()), pixmaps("problem"), tooltip());
    }
}
QString TrayController::tooltip() const
{
    // The status line, then the ready count unless the status already ends with it.
    const auto status = settings.status(), ready = settings.cache()->facts().value("ready").toString();
    return status.endsWith(ready) ? status : status + '\n' + ready;
}
void TrayController::publish()
{
    const bool persistent = settings.systemd()->persistentlyEnabled("aerial-tray.service");
    QString message = !available ? "No desktop tray was found. An enabled startup preference is kept until a tray returns."
        : !settings.systemd()->available() ? "This session has no systemd user manager. Add aerial-settings --tray to compositor autostart to keep it across logins."
        : desired && !persistent ? "Shown for this session. Startup is not enabled; check the user unit."
        : "Status and quick actions in the tray. Comes back each time you log in when turned on.";
    if (!problem.isEmpty()) message += " " + problem;
    settings.setTrayState({{"available", available}, {"on", desired}, {"visible", bool(item)}, {"persistent", persistent}, {"message", message}});
}
}
