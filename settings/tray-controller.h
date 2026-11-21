#pragma once
#include "menu-exporter.h"
#include "settings-controller.h"
#include <QDBusObjectPath>
#include <memory>
#include <functional>

namespace Aerial::Settings {
class TrayItem : public QObject {
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.kde.StatusNotifierItem")
    Q_PROPERTY(QString Category READ category CONSTANT)
    Q_PROPERTY(QString Id READ id CONSTANT)
    Q_PROPERTY(QString Title READ title CONSTANT)
    Q_PROPERTY(QString Status READ status)
    Q_PROPERTY(uint WindowId READ windowId CONSTANT)
    Q_PROPERTY(QString IconThemePath READ empty CONSTANT)
    Q_PROPERTY(QString IconName READ empty CONSTANT)
    Q_PROPERTY(Aerial::Settings::TrayImages IconPixmap READ images)
    Q_PROPERTY(QString OverlayIconName READ empty CONSTANT)
    Q_PROPERTY(Aerial::Settings::TrayImages OverlayIconPixmap READ noImages CONSTANT)
    Q_PROPERTY(QString AttentionIconName READ empty CONSTANT)
    Q_PROPERTY(Aerial::Settings::TrayImages AttentionIconPixmap READ attentionImages)
    Q_PROPERTY(QString AttentionMovieName READ empty CONSTANT)
    Q_PROPERTY(Aerial::Settings::TrayTooltip ToolTip READ tooltip)
    Q_PROPERTY(bool ItemIsMenu READ itemIsMenu CONSTANT)
    Q_PROPERTY(QDBusObjectPath Menu READ menuPath CONSTANT)
public:
    TrayItem(QMenu &menu, const QString &watcher, QObject *parent = nullptr);
    ~TrayItem() override;
    void update(const QString &status, const TrayImages &images, const TrayImages &attention, const QString &text);
    QString category() const { return "ApplicationStatus"; }
    QString id() const { return "org.aeriallock.AerialSettings"; }
    QString title() const { return "Aerial"; }
    QString empty() const { return {}; }
    QString status() const { return currentStatus; }
    uint windowId() const { return 0; }
    bool itemIsMenu() const { return false; }
    QDBusObjectPath menuPath() const { return QDBusObjectPath("/MenuBar"); }
    TrayImages images() const { return currentImages; }
    TrayImages attentionImages() const { return currentAttention; }
    TrayImages noImages() const { return {}; }
    // SNI descriptive text supports HTML. Keep manifest titles literal at this
    // protocol boundary, while the shared formatter remains plain text.
    TrayTooltip tooltip() const { return {{}, currentImages, "Aerial", QString(text.toHtmlEscaped()).replace('\n', "<br/>")}; }
public slots:
    void Activate(int x, int y);
    void SecondaryActivate(int x, int y);
    void ContextMenu(int x, int y);
    void Scroll(int delta, const QString &orientation);
    void ProvideXdgActivationToken(const QString &token);
signals:
    void NewTitle();
    void NewIcon();
    void NewAttentionIcon();
    void NewOverlayIcon();
    void NewToolTip();
    void NewStatus(const QString &status);
    void openRequested(const QVariantMap &platform);
    void registrationFailed();
private:
    QString connectionName;
    QDBusConnection bus;
    QMenu &actions;
    MenuExporter menu;
    QString currentStatus = "Active", text, activationToken;
    TrayImages currentImages, currentAttention;
};

class TrayController : public QObject {
    Q_OBJECT
public:
    TrayController(SettingsController &settings, QDBusConnection bus, QObject *parent = nullptr, std::function<bool()> desktopAvailable = {});
    bool enabled() const { return desired; }
    bool keepsAlive() const { return desired || persistencePending; }
    void show(); // Startup/forwarding: no persistence change.
    void setEnabled(bool on); // User intent: immediate visibility + future startup.
    QMenu &actions() { return menu; }
    static QString badge(const QVariantMap &properties, bool downloading, bool paused);
    static bool needsAttention(const QVariantMap &properties);
signals:
    void changed();
    void openRequested(int page, const QVariantMap &platform);
private slots:
    void discover();
    void hostChanged();
private:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void sample();
    void publish();
    QString tooltip() const;
    void reconcile();
    void persist();
    SettingsController &settings;
    QDBusConnection bus;
    std::function<bool()> desktopAvailable;
    QString watcherOwner, problem;
    quint64 generation = 0;
    bool hostPending = false, hostDirty = false, explicitlyHidden = false;
    bool desired = false, available = false, persistencePending = false, pendingDesired = false;
    QMenu menu;
    QAction *summary, *transfer, *pause, *open, *hide;
    std::unique_ptr<TrayItem> item;
};
}
