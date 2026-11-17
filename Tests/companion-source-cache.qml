import QtQuick
import Quickshell
import "../Services" as Services

Scope {
    id: fixture
    property bool begun: false
    Services.ConfigStore { id: fixtureConfig }
    Services.SourceCache { id: content; config: fixtureConfig; topics: ["mountain"] }
    Timer {
        interval: 20
        repeat: true
        running: true
        onTriggered: {
            if (!fixtureConfig.ready) return
            if (!fixture.begun) {
                fixture.begun = true
                content.begin()
            }
            if (!content.resolved) return
            var result = { playlist: content.playlist, metadata: [] }
            for (var i = 0; i < content.playlist.length; i++) {
                var path = content.playlist[i].path
                result.metadata.push(content.lookup(path))
                content.reportPlayback(path)
            }
            console.info("SOURCE_CACHE_RESULT " + JSON.stringify(result))
            Qt.quit()
        }
    }
    Timer { interval: 7000; running: true; onTriggered: Qt.exit(1) }
}
