#include "portal-client.h"
#include "async-call.h"
#include "common/client/client-support.h"
#include <QDBusConnectionInterface>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusUnixFileDescriptor>
#include <QDir>
#include <QFile>
#include <QUrl>
#include <QUuid>
#include <fcntl.h>
#include <unistd.h>

namespace Aerial::Settings {
namespace { constexpr auto Desktop = "org.freedesktop.portal.Desktop"; constexpr auto RequestInterface = "org.freedesktop.portal.Request"; }
PortalClient::PortalClient(QDBusConnection bus, QObject *parent) : QObject(parent), bus(std::move(bus))
{
    auto *watcher = new QDBusServiceWatcher(Desktop, this->bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, &PortalClient::ownerChanged);
}
PortalClient::~PortalClient()
{
    const auto pending = requests.values();
    for (const auto &request : pending) cancel(request.id);
}
quint64 PortalClient::dispatch(const QString &interface, const QString &method, QVariantList arguments, bool chooser)
{
    const auto id = ++sequence;
    auto sender = bus.baseService().mid(1); sender.replace('.', '_');
    const auto token = "aerial_" + QUuid::createUuid().toString(QUuid::Id128);
    const auto path = "/org/freedesktop/portal/desktop/request/" + sender + '/' + token;
    requests.insert(path, {id, path, {}, chooser});
    problem.clear(); emit changed();
    bus.connect(Desktop, path, RequestInterface, "Response", this, SLOT(response(uint,QVariantMap,QDBusMessage)));
    QVariantMap options{{"handle_token", token}};
    if (chooser) { options["directory"] = arguments.takeLast(); options["multiple"] = false; options["modal"] = true; }
    arguments << options;
    auto call = QDBusMessage::createMethodCall(Desktop, "/org/freedesktop/portal/desktop", interface, method);
    call.setArguments(arguments);
    watch(this, bus.asyncCall(call, 30000), [this, path](const QDBusMessage &message) {
        const QDBusPendingReply<QDBusObjectPath> reply(message);
        if (cancelledRequests.remove(path)) {
            if (!reply.isError()) {
                auto close = QDBusMessage::createMethodCall(message.service().isEmpty() ? QString(Desktop) : message.service(), reply.value().path(), RequestInterface, "Close");
                close.setAutoStartService(false); bus.asyncCall(close);
            }
            return;
        }
        if (!requests.contains(path)) return; // Response may precede the method reply.
        if (reply.isError()) { complete(path, {}, false, "Desktop portal unavailable: " + reply.error().name().section('.', -1)); return; }
        const auto actual = reply.value().path();
        if (actual != path) {
            auto request = requests.take(path); request.path = actual; request.owner = message.service(); requests.insert(actual, request);
            bus.disconnect(Desktop, path, RequestInterface, "Response", this, SLOT(response(uint,QVariantMap,QDBusMessage)));
            bus.connect(Desktop, actual, RequestInterface, "Response", this, SLOT(response(uint,QVariantMap,QDBusMessage)));
        } else requests[path].owner = message.service();
        emit changed();
    });
    return id;
}
quint64 PortalClient::choose(bool directory)
{
    return dispatch("org.freedesktop.portal.FileChooser", "OpenFile", {QString(), directory ? "Choose a video folder" : "Choose a video file", directory}, true);
}
void PortalClient::openFolder(const QString &path)
{
    if (!QDir::isAbsolutePath(path)) { problem = "The service did not provide an absolute folder path."; emit changed(); return; }
    const int fd = ::open(QFile::encodeName(path).constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) { problem = "Couldn't open this folder. Check that it exists and is readable."; emit changed(); return; }
    QDBusUnixFileDescriptor descriptor(fd); ::close(fd);
    dispatch("org.freedesktop.portal.OpenURI", "OpenFile", {QString(), QVariant::fromValue(descriptor)}, false);
}
void PortalClient::openLink(const QString &text)
{
    const QUrl url(text, QUrl::StrictMode);
    if (!url.isValid() || !QStringList{"http", "https"}.contains(url.scheme()) || url.host().isEmpty()) { problem = "This link is not an HTTP or HTTPS address."; emit changed(); return; }
    dispatch("org.freedesktop.portal.OpenURI", "OpenURI", {QString(), url.toString(QUrl::FullyEncoded)}, false);
}
void PortalClient::cancel(quint64 id)
{
    for (const auto &request : requests.values()) if (request.id == id) {
        auto call = QDBusMessage::createMethodCall(request.owner.isEmpty() ? QString(Desktop) : request.owner, request.path, RequestInterface, "Close");
        call.setAutoStartService(false); bus.asyncCall(call);
        if (request.owner.isEmpty()) cancelledRequests.insert(request.path, request);
        complete(request.path, {}, true, {}); return;
    }
}
void PortalClient::complete(const QString &path, const QString &selected, bool cancelled, const QString &error)
{
    if (!requests.contains(path)) return;
    const auto request = requests.take(path);
    bus.disconnect(Desktop, path, RequestInterface, "Response", this, SLOT(response(uint,QVariantMap,QDBusMessage)));
    problem = error; emit changed(); emit finished(request.id, selected, cancelled, error);
}
void PortalClient::response(uint code, const QVariantMap &results, const QDBusMessage &message)
{
    if (!requests.contains(message.path())) return;
    const auto request = requests.value(message.path());
    if (!request.owner.isEmpty() && request.owner != message.service()) return;
    if (code == 1) { complete(message.path(), {}, true, {}); return; }
    // Response 2 covers both a refused request and a chooser closed by its
    // window or Escape (GTK reports those as 2, not as a user cancellation).
    if (code != 0) { complete(message.path(), {}, false, request.chooser ? "The chooser closed without a selection. Your fields are kept." : "The desktop didn't open this. Try again or check portal permissions."); return; }
    QString selected;
    if (request.chooser) {
        const auto uris = Client::unpack(results.value("uris")).toStringList();
        const QUrl url(uris.size() == 1 ? uris.first() : QString(), QUrl::StrictMode);
        if (!url.isValid() || !url.isLocalFile() || !url.host().isEmpty() || url.hasQuery() || url.hasFragment() || !QDir::isAbsolutePath(url.toLocalFile())) {
            complete(message.path(), {}, false, "Choose one local folder or file, not a remote address."); return;
        }
        selected = url.toLocalFile();
    }
    complete(message.path(), selected, false, {});
}
void PortalClient::ownerChanged(const QString &, const QString &oldOwner, const QString &)
{
    if (oldOwner.isEmpty()) return;
    cancelledRequests.clear();
    for (const auto &request : requests.values()) complete(request.path, {}, false, "The desktop portal stopped. Your fields are kept; try again.");
}
}
