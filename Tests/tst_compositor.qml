import QtQuick
import QtTest
import Quickshell
import "../Services" as Services

Item {
    id: root
    readonly property var shell: Quickshell
    Component { id: compositorComponent; Services.Compositor {} }

    TestCase {
        name: "CompositorDefaults"
        when: windowShown

        function cleanupTestCase() {
            root.shell.testEnvironment = null
        }

        function test_default_output_without_backend_data() {
            return [
                { tag: "portable", environment: {} },
                { tag: "niri", environment: { NIRI_SOCKET: "/nonexistent/niri.sock" } },
                { tag: "niri-with-stale-hyprland", environment: {
                    NIRI_SOCKET: "/nonexistent/niri.sock",
                    HYPRLAND_INSTANCE_SIGNATURE: "stale"
                } }
            ]
        }

        function test_default_output_without_backend(data) {
            failOnWarning(/.*/)
            root.shell.testEnvironment = data.environment
            var compositor = createTemporaryObject(compositorComponent, root)
            verify(compositor !== null)
            compare(compositor.backendName, "none")
            compare(compositor.backendReady, true)
            compare(compositor.focusedOutputName, "")
        }
    }
}
