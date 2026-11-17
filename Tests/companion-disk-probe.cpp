#include "../companion/cache-store.h"
#include "../common/content-snapshot.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <cerrno>
#include <fcntl.h>
#include <sys/statvfs.h>
#include <unistd.h>

using namespace Aerial;

namespace {
int filler = -1, target = -1, actualErrors = 0;
bool armAny = false;
const QByteArray payload(4096, 'x');

void require(bool condition, const char *message)
{
    if (!condition) qFatal("disk fault: %s", message);
}

Representation rep(const char *name)
{
    Representation value;
    value.key = contentDigest(name); value.assetId = name; value.sourceId = "disk-fixture";
    value.url = QUrl("http://localhost/fixture"); value.height = 1080;
    return value;
}

void prepare(CacheStore &cache, const Representation &value)
{
    const int fd = cache.openPartial(value.key, true);
    require(fd >= 0 && cache.reserve(payload.size()) && cache.append(fd, payload) && fsync(fd) == 0, "prepare bytes");
    close(fd);
}

void clearFiller()
{
    target = -1; armAny = false;
    require(ftruncate(filler, 0) == 0 && fsync(filler) == 0 && lseek(filler, 0, SEEK_SET) == 0, "release fixture space");
}
}

extern "C" ssize_t __real_write(int, const void *, size_t);
extern "C" ssize_t __wrap_write(int fd, const void *bytes, size_t count)
{
    if (filler >= 0 && (fd == target || armAny)) {
        target = -1; armAny = false;
        // Consume space after CacheStore's available-space check, then invoke
        // the real kernel write. This is an ENOSPC race, not a mocked errno.
        const QByteArray block(65536, 'f');
        for (const size_t size : {size_t(65536), size_t(4096), size_t(1)}) {
            while (__real_write(filler, block.constData(), size) > 0) {}
            require(errno == ENOSPC, "fill fixture tail blocks");
        }
        require(errno == ENOSPC && fsync(filler) == 0, "fill isolated filesystem");
        const auto result = __real_write(fd, bytes, count);
        require(result < 0 && errno == ENOSPC, "actual target write must encounter ENOSPC");
        ++actualErrors;
        return result;
    }
    return __real_write(fd, bytes, count);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    require(argc == 2, "pass a dedicated mounted fixture filesystem");
    const auto root = QString::fromLocal8Bit(argv[1]);
    struct statvfs space{};
    require(statvfs(QFile::encodeName(root), &space) == 0 && space.f_blocks * space.f_frsize <= 32 * 1024 * 1024,
        "fixture must be an isolated filesystem of at most 32 MiB");
    filler = ::open(QFile::encodeName(root + "/filler"), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    require(filler >= 0, "create filler");
    const auto path = root + "/cache";
    const auto first = rep("first"), second = rep("second"), third = rep("third");
    {
        CacheStore cache(path); require(cache.valid(), "cache owner"); cache.configure(10000000000, 0);
        prepare(cache, first); require(cache.complete(first, {{"initialFrameDecoded", true}}, 1), "initial publication");
        target = cache.openPartial(second.key, true); require(target >= 0, "open next partial");
        const int fd = target;
        require(cache.reserve(65536), "reserve before concurrent fill");
        require(!cache.append(fd, QByteArray(65536, 'y')), "append must fail safely");
        close(fd); cache.releaseReservation(); clearFiller();
        require(cache.entries().size() == 1 && cache.usage().safe, "failed append preserves inventory");
    }
    for (const auto &step : {QString("generation"), QString("committed")}) {
        CacheStore cache(path, std::nullopt, [&step](const QString &point) { if (point == step) armAny = true; });
        require(cache.valid(), "reopen owner"); cache.configure(10000000000, 0);
        const auto value = step == "generation" ? second : third;
        prepare(cache, value);
        require(!cache.complete(value, {{"initialFrameDecoded", true}}, 2), "publication must report disk failure");
        clearFiller();
        ContentSnapshot old(path); require(old.valid() && old.entries().size() == 1, "public old snapshot survives ENOSPC");
    }
    {
        CacheStore recovered(path); require(recovered.valid(), "recover committed publication");
        require(recovered.entries().size() == 2 && recovered.contains(first.key) && recovered.contains(third.key)
            && !recovered.contains(second.key), "recover only committed decoded inventory");
        ContentSnapshot snapshot(path); require(snapshot.valid() && snapshot.entries().size() == 2, "consumer sees repaired pointer");
        QFile prior(path + "/objects/" + first.key + ".media"); require(prior.open(QIODevice::ReadOnly) && prior.readAll() == payload,
            "prior bytes unchanged");
        QFile partial(path + "/partials/" + second.key + ".part"); require(partial.open(QIODevice::ReadOnly) && partial.readAll() == payload,
            "precommit verified bytes remain resumable");
        require(actualErrors == 3, "all three target writes reached real ENOSPC");
        const auto result = QJsonDocument(QJsonObject{{"result", "passed"}, {"actualEnospcWrites", actualErrors},
            {"readyClips", recovered.entries().size()}, {"usageKnown", recovered.usage().safe}}).toJson(QJsonDocument::Compact);
        puts(result.constData());
    }
    close(filler);
}
