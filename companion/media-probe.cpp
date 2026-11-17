// Only this executable loads FFmpeg. It receives an already-open, read-only
// completed local object on FD 3; demuxers can never open nested resources.
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/mem.h>
}

namespace {
struct Input { int fd; qint64 read = 0; };
int readPacket(void *opaque, uint8_t *buffer, int size)
{
    auto &in = *static_cast<Input *>(opaque);
    if (in.read >= 128 * 1024 * 1024) return AVERROR(EIO);
    const auto count = ::read(in.fd, buffer, std::min<qint64>(size, 128 * 1024 * 1024 - in.read));
    if (count < 0) return AVERROR(errno);
    in.read += count;
    return count ? int(count) : AVERROR_EOF;
}
int64_t seekPacket(void *opaque, int64_t offset, int whence)
{
    const int fd = static_cast<Input *>(opaque)->fd;
    if (whence == AVSEEK_SIZE) { struct stat st{}; return fstat(fd, &st) ? AVERROR(errno) : st.st_size; }
    const auto pos = lseek(fd, offset, whence & ~AVSEEK_FORCE);
    return pos < 0 ? AVERROR(errno) : pos;
}
int denyOpen(AVFormatContext *, AVIOContext **, const char *, int, AVDictionary **) { return AVERROR(EACCES); }
int interrupted(void *opaque)
{
    return std::chrono::steady_clock::now() >= *static_cast<std::chrono::steady_clock::time_point *>(opaque);
}
void bound(int resource, rlim_t value) { const rlimit limit{value, value}; if (setrlimit(resource, &limit)) _exit(1); }
}

int main(int argc, char **argv)
{
    bound(RLIMIT_AS, 1024ULL * 1024 * 1024);
    bound(RLIMIT_CPU, 15);
    bound(RLIMIT_CORE, 0);
    bound(RLIMIT_FSIZE, 0);
    QCoreApplication app(argc, argv);
    if (argc != 1) return 2;
    struct stat st{};
    if (fstat(3, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 || (fcntl(3, F_GETFL) & O_ACCMODE) != O_RDONLY) return 1;
    av_log_set_level(AV_LOG_QUIET);
    av_max_alloc(256 * 1024 * 1024);
    Input input{3};
    auto *buffer = static_cast<unsigned char *>(av_malloc(32768));
    if (!buffer) return 1;
    AVIOContext *io = avio_alloc_context(buffer, 32768, 0, &input, readPacket, nullptr, seekPacket);
    if (!io) { av_free(buffer); return 1; }
    AVFormatContext *format = avformat_alloc_context();
    if (!format) { av_freep(&io->buffer); avio_context_free(&io); return 1; }
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    format->pb = io;
    format->flags |= AVFMT_FLAG_CUSTOM_IO;
    format->io_open = denyOpen;
    format->interrupt_callback = {interrupted, &deadline};
    format->probesize = 8 * 1024 * 1024;
    format->max_analyze_duration = 3 * AV_TIME_BASE;
    AVDictionary *options = nullptr;
    av_dict_set(&options, "format_whitelist", "mov,matroska,webm,avi,mpegts,ogg", 0);
    av_dict_set(&options, "protocol_whitelist", "", 0);
    bool success = avformat_open_input(&format, nullptr, nullptr, &options) >= 0;
    av_dict_free(&options);
    AVCodecContext *decoder = nullptr;
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    int stream = -1;
    if (success && packet && frame && avformat_find_stream_info(format, nullptr) >= 0)
        stream = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (stream >= 0) {
        auto *parameters = format->streams[stream]->codecpar;
        const auto *codec = avcodec_find_decoder(parameters->codec_id);
        if (codec) decoder = avcodec_alloc_context3(codec);
        if (decoder) {
            decoder->thread_count = 1;
            decoder->max_pixels = 8192 * 4320;
            if (avcodec_parameters_to_context(decoder, parameters) < 0 || avcodec_open2(decoder, codec, nullptr) < 0)
                avcodec_free_context(&decoder);
        }
    }
    success = false;
    if (decoder) {
        for (int i = 0; i < 4096 && !interrupted(&deadline); ++i) {
            if (av_read_frame(format, packet) < 0) break;
            if (packet->stream_index == stream && avcodec_send_packet(decoder, packet) >= 0
                && avcodec_receive_frame(decoder, frame) >= 0) success = frame->width > 0 && frame->height > 0;
            av_packet_unref(packet);
            if (success) break;
        }
    }
    if (success) {
        const QJsonObject result{{"container", QString::fromUtf8(format->iformat->name)},
            {"codec", QString::fromUtf8(avcodec_get_name(decoder->codec_id))}, {"width", frame->width},
            {"height", frame->height}, {"initialFrameDecoded", true}};
        QFile output;
        const auto json = QJsonDocument(result).toJson(QJsonDocument::Compact);
        success = output.open(1, QIODevice::WriteOnly) && output.write(json) == json.size() && output.flush();
    }
    av_frame_free(&frame); av_packet_free(&packet); avcodec_free_context(&decoder);
    avformat_close_input(&format);
    av_freep(&io->buffer); avio_context_free(&io);
    return success ? 0 : 1;
}
