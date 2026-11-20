import QtQuick
import QtQuick.Shapes
Item {
    id: root
    property string name: "info"
    property color color: Style.text
    implicitWidth: 18
    implicitHeight: 18
    readonly property var geometry: ({
        "plus": "M12 5v14 M5 12h14",
        "tv": "M5 7h14a2 2 0 0 1 2 2v9a2 2 0 0 1-2 2h-14a2 2 0 0 1-2-2v-9a2 2 0 0 1 2-2 M16 3l-4 4-4-4",
        "world": "M3 12a9 9 0 1 0 18 0a9 9 0 1 0-18 0 M3.6 9h16.8 M3.6 15h16.8 M11.5 3a17 17 0 0 0 0 18 M12.5 3a17 17 0 0 1 0 18",
        "folder": "M5 4h4l3 3h7a2 2 0 0 1 2 2v8a2 2 0 0 1-2 2h-14a2 2 0 0 1-2-2v-11a2 2 0 0 1 2-2",
        "movie": "M6 4h12a2 2 0 0 1 2 2v12a2 2 0 0 1-2 2h-12a2 2 0 0 1-2-2v-12a2 2 0 0 1 2-2 M8 4v16 M16 4v16 M4 8h4 M4 16h4 M4 12h16 M16 8h4 M16 16h4",
        "refresh": "M20 11a8.1 8.1 0 0 0-15.5-2 M4 5v4h4 M4 13a8.1 8.1 0 0 0 15.5 2 M20 19v-4h-4",
        "external": "M12 6h-6a2 2 0 0 0-2 2v10a2 2 0 0 0 2 2h10a2 2 0 0 0 2-2v-6 M11 13l9-9 M15 4h5v5",
        "grip": "M9 5a1 1 0 1 0 0 2a1 1 0 1 0 0-2 M9 11a1 1 0 1 0 0 2a1 1 0 1 0 0-2 M9 17a1 1 0 1 0 0 2a1 1 0 1 0 0-2 M15 5a1 1 0 1 0 0 2a1 1 0 1 0 0-2 M15 11a1 1 0 1 0 0 2a1 1 0 1 0 0-2 M15 17a1 1 0 1 0 0 2a1 1 0 1 0 0-2",
        "dots": "M12 4a1 1 0 1 0 0 2a1 1 0 1 0 0-2 M12 11a1 1 0 1 0 0 2a1 1 0 1 0 0-2 M12 18a1 1 0 1 0 0 2a1 1 0 1 0 0-2",
        "checkcircle": "M3 12a9 9 0 1 0 18 0a9 9 0 1 0-18 0 M9 12l2 2 4-4",
        "chevron": "M6 9l6 6 6-6",
        "download": "M4 17v2a2 2 0 0 0 2 2h12a2 2 0 0 0 2-2v-2 M7 11l5 5 5-5 M12 4v12",
        "stack": "M12 4l-8 4 8 4 8-4-8-4 M4 12l8 4 8-4 M4 16l8 4 8-4",
        "database": "M4 6a8 3 0 1 0 16 0a8 3 0 1 0-16 0v6a8 3 0 0 0 16 0v-6 M4 12v6a8 3 0 0 0 16 0v-6",
        "lock": "M7 11h10a2 2 0 0 1 2 2v6a2 2 0 0 1-2 2h-10a2 2 0 0 1-2-2v-6a2 2 0 0 1 2-2 M8 11v-4a4 4 0 1 1 8 0v4 M11 16a1 1 0 1 0 2 0a1 1 0 1 0-2 0",
        "info": "M3 12a9 9 0 1 0 18 0a9 9 0 1 0-18 0 M12 8h.01 M11 12h1v4h1",
        "wifi": "M12 18h.01 M9.17 15.17a4 4 0 0 1 5.66 0 M6.34 12.34a8 8 0 0 1 11.32 0 M3.52 9.52a12 12 0 0 1 16.96 0",
        "ethernet": "M9 3h6v6h-6z M4 15h6v6h-6z M14 15h6v6h-6z M7 15v-1a2 2 0 0 1 2-2h6a2 2 0 0 1 2 2v1 M12 9v3",
        "cell": "M6 18v-3 M10 18v-6 M14 18v-9 M18 18v-12",
        "bluetooth": "M7 8l10 8-5 4v-16l5 4-10 8",
        "vpn": "M12 3a12 12 0 0 0 8.5 3a12 12 0 0 1-8.5 15a12 12 0 0 1-8.5-15a12 12 0 0 0 8.5-3 M11 11a1 1 0 1 0 2 0a1 1 0 1 0-2 0 M12 12v2.5",
        "network": "M6 9a6 6 0 1 0 12 0a6 6 0 1 0-12 0 M12 15v5 M8 20h8 M6.5 9h11 M12 3a9 9 0 0 1 0 12 M12 3a9 9 0 0 0 0 12",
        "help": "M3 12a9 9 0 1 0 18 0a9 9 0 1 0-18 0 M12 17v.01 M12 13.5a1.5 1.5 0 0 1 1-1.5a2.6 2.6 0 1 0-3-4",
        "plug": "M9.785 6l8.215 8.215-2.054 2.054a5.81 5.81 0 1 1-8.215-8.215l2.054-2.054z M4 20l3.5-3.5 M15 4l-3.5 3.5 M20 9l-3.5 3.5",
        "battery": "M6 7h11a2 2 0 0 1 2 2v1h1v4h-1v1a2 2 0 0 1-2 2h-11a2 2 0 0 1-2-2v-6a2 2 0 0 1 2-2 M7 10v4 M10 10v4",
        "alert": "M12 9v4 M10.363 3.591l-8.106 13.534a1.914 1.914 0 0 0 1.636 2.871h16.214a1.914 1.914 0 0 0 1.636-2.87l-8.106-13.536a1.914 1.914 0 0 0-3.274 0z M12 16h.01",
        "play": "M7 4v16l13-8z",
        "x": "M18 6l-12 12 M6 6l12 12",
        "clock": "M3 12a9 9 0 1 0 18 0a9 9 0 1 0-18 0 M12 7v5l3 3"
    })
    Shape {
        width: 24
        height: 24
        scale: Math.min(root.width, root.height) / 24
        transformOrigin: Item.TopLeft
        ShapePath {
            strokeColor: root.color
            strokeWidth: 1.75
            fillColor: "transparent"
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            PathSvg { path: root.geometry[root.name] || root.geometry.info }
        }
    }
}
