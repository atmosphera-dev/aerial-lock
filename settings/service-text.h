#pragma once
#include <QMap>
#include <QString>

namespace Aerial::Settings::ServiceText {
// Protocol identifiers stay in model metadata. Every human-facing consumer
// uses this projection, including unknown codes from a newer service.
inline QString reason(const QString &code)
{
    static const QMap<QString, QString> messages{
        {"NoRemoteSources", "No online sources are enabled."},
        {"NoEligibleAssets", "No videos match the current source and quality settings."},
        {"NoAllowedResolution", "No download quality is allowed by the current settings."},
        {"Paused", "Automatic downloads are paused."},
        {"Metered", "Downloads are waiting for an unmetered connection."},
        {"ConnectionTypeDisallowed", "Downloads aren't allowed on this connection."},
        {"PowerDisallowed", "Downloads aren't allowed on this power source."},
        {"Offline", "There is no network connection."},
        {"Initializing", "Checking the network connection."},
        {"CacheFull", "Storage is full. Free some space or raise the storage limit."},
        {"PlaybackProtected", "Videos in use can't be replaced yet."},
        {"Backoff", "Waiting before trying the source again."},
        {"InvalidConfig", "Download settings need attention."},
        {"StorageError", "Couldn't read or write the video storage."},
        {"Cancelled", "The operation was cancelled."},
        {"TransferFailed", "Couldn't download the videos. Check your sources and connection."},
        {"TransportError", "Couldn't reach the source. Check your connection and try again."},
        {"Timeout", "The source didn't answer in time. Try again."},
        {"HttpError", "The source couldn't complete the request. Try again."},
        {"TlsError", "Couldn't establish a secure connection to the source."},
        {"UnsafeUrl", "The source address isn't a supported web address."},
        {"UnsafeRedirect", "The source redirected to an unsupported address."},
        {"InvalidResponse", "The source returned an unreadable response."},
        {"InvalidManifest", "The source returned an unreadable video list."},
        {"UnsafeManifestLink", "The video list contains an unsupported address."},
        {"InvalidRepresentation", "This video doesn't have a usable download."},
        {"LengthMismatch", "The download ended before the whole video arrived."},
        {"ChecksumMismatch", "The downloaded video didn't match its source."},
        {"ProbeFailed", "The downloaded file couldn't be read as a video."}};
    return messages.value(code, "The operation couldn't finish. Try again or check your sources.");
}
inline QString state(const QString &code)
{
    static const QMap<QString, QString> labels{{"succeeded", "Done"}, {"failed", "Needs attention"},
        {"cancelled", "Cancelled"}, {"deferred", "Waiting"}, {"queued", "Queued"},
        {"downloading", "Downloading"}, {"refreshing", "Checking sources"},
        {"pruning", "Cleaning up"}, {"recovering", "Recovering"}};
    return labels.value(code, "In progress");
}
}
