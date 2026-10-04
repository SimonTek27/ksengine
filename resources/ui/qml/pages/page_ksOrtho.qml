import QtQuick 2.15
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick3D 6.5
import ksEditor.Ortho 1.0

Rectangle {
    id: pageOrtho
    objectName: "pageOrthoRoot"
    width: 1280
    height: 720
    color: "#1e1e1e"
    focus: true

    property var ribbonDefs: [
        {
            title: "File", groups: [
                { name: "File", buttons: [
                    { label: "Load Model", icon: "\u2913", cmd: "load" },
                    { label: "Save Result", icon: "\u2913", cmd: "save" }
                ]}
            ]
        },
        {
            title: "Views", groups: [
                { name: "Views", buttons: [
                    { label: "Front", icon: "\u25B6", cmd: "view_front" },
                    { label: "Back", icon: "\u25C0", cmd: "view_back" },
                    { label: "Left", icon: "\u25B6", cmd: "view_left" },
                    { label: "Right", icon: "\u25C0", cmd: "view_right" },
                    { label: "Top", icon: "\u25B2", cmd: "view_top" },
                    { label: "Bottom", icon: "\u25BC", cmd: "view_bottom" }
                ]}
            ]
        },
        {
            title: "Render", groups: [
                { name: "Output", buttons: [
                    { label: "Generate", icon: "\u25B6", cmd: "generate", style: "primary" }
                ]}
            ]
        }
    ]

    property int activeRibbonTab: 0

    function runCommand(cmd) {
        var c = cmd.toLowerCase()
        if (c === "load") fileDialog.open()
        else if (c === "save") saveDialog.open()
        else if (c === "generate") Ortho.generate()
        else if (c.startsWith("view_")) {
            var viewName = c.substring(5)
            toggleView(viewName)
        }
    }

    function toggleView(name) {
        var views = Ortho.selectedViews.slice()
        var idx = views.indexOf(name)
        if (idx >= 0) views.splice(idx, 1)
        else views.push(name)
        Ortho.selectedViews = views
    }

    Connections {
        target: Ortho
        function onGenerationComplete() { resultImage.source = "" }
        function onErrorOccurred(msg) { errorLabel.text = msg; errorLabel.visible = true }
    }

    FileDialog {
        id: fileDialog
        title: "Open 3D Model"
        nameFilters: [
            "All Supported (*.obj *.kn5 *.gltf *.glb *.stl *.dae)",
            "OBJ Files (*.obj)",
            "KN5 Files (*.kn5)",
            "GLTF/GLB Files (*.gltf *.glb)",
            "STL Files (*.stl)",
            "Collada Files (*.dae)"
        ]
        onAccepted: Ortho.loadFile(fileDialog.fileUrl.toString().replace("file:///", ""))
    }

    FileDialog {
        id: saveDialog
        title: "Save Result Image"
        nameFilters: ["PNG Images (*.png)", "JPEG Images (*.jpg)"]
        fileMode: FileDialog.SaveFile
        onAccepted: Ortho.saveResult(saveDialog.fileUrl.toString().replace("file:///", ""))
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 12

        // Left panel - Settings
        Rectangle {
            Layout.preferredWidth: 300
            Layout.fillHeight: true
            color: "#252526"
            radius: 6

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 16

                // Model info
                Rectangle {
                    Layout.fillWidth: true
                    height: 60
                    color: "#333333"
                    radius: 4

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 8

                        Text {
                            text: Ortho.currentFileName || "No model loaded"
                            color: "#cccccc"
                            font.bold: true
                            Layout.fillWidth: true
                            elide: Text.ElideMiddle
                        }
                        Text {
                            text: Ortho.modelLoaded_flag ?
                                (Ortho.vertexCount + " vertices, " + Ortho.faceCount + " faces") :
                                "Drop a file or click Load"
                            color: "#888888"
                            font.pixelSize: 11
                        }
                    }
                }

                // Parts list
                Text { text: "PARTS"; color: "#cccccc"; font.bold: true; font.pixelSize: 12 }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "#1e1e1e"
                    radius: 4

                    ListView {
                        id: partsList
                        anchors.fill: parent
                        anchors.margins: 4
                        clip: true
                        model: Ortho.partNames

                        delegate: CheckBox {
                            text: modelData
                            checked: true
                            color: "#cccccc"
                            font.pixelSize: 11
                            onCheckedChanged: {
                                var excluded = Ortho.excludedParts.slice()
                                if (!checked) {
                                    if (excluded.indexOf(modelData) < 0) excluded.push(modelData)
                                } else {
                                    var idx = excluded.indexOf(modelData)
                                    if (idx >= 0) excluded.splice(idx, 1)
                                }
                                Ortho.excludedParts = excluded
                            }
                        }
                    }
                }

                // Views
                Text { text: "VIEWS"; color: "#cccccc"; font.bold: true; font.pixelSize: 12 }
                Flow {
                    Layout.fillWidth: true
                    spacing: 4
                    Repeater {
                        model: ["front", "back", "left", "right", "top", "bottom"]
                        delegate: CheckBox {
                            text: modelData.toUpperCase()
                            checked: Ortho.selectedViews.indexOf(modelData) >= 0
                            color: "#cccccc"
                            font.pixelSize: 11
                            onCheckedChanged: toggleView(modelData)
                        }
                    }
                }

                // AO settings
                Text { text: "AMBIENT OCCLUSION"; color: "#cccccc"; font.bold: true; font.pixelSize: 12 }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    CheckBox {
                        text: "Enable AO"
                        checked: Ortho.aoEnabled
                        color: "#cccccc"
                        onCheckedChanged: Ortho.aoEnabled = checked
                    }
                    ComboBox {
                        Layout.preferredWidth: 100
                        model: ["ssao", "vertex", "directional"]
                        currentIndex: model.indexOf(Ortho.aoMode)
                        onCurrentIndexChanged: Ortho.aoMode = model[currentIndex]
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text { text: "Darkness:"; color: "#888888"; font.pixelSize: 11 }
                    Slider {
                        Layout.fillWidth: true
                        from: 0; to: 100
                        value: Ortho.aoDarkness * 100
                        onMoved: Ortho.aoDarkness = value / 100
                    }
                    Text { text: Math.round(Ortho.aoDarkness * 100) + "%"; color: "#888888"; font.pixelSize: 11 }
                }

                // Colors
                Text { text: "COLORS"; color: "#cccccc"; font.bold: true; font.pixelSize: 12 }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Text { text: "Background:"; color: "#888888"; font.pixelSize: 11 }
                    Rectangle {
                        width: 24; height: 24
                        color: Ortho.bgColor
                        border.color: "#555555"
                        MouseArea {
                            anchors.fill: parent
                            onClicked: bgColorDialog.open()
                        }
                    }
                    Text { text: "Lines:"; color: "#888888"; font.pixelSize: 11 }
                    Rectangle {
                        width: 24; height: 24
                        color: Ortho.lineColor
                        border.color: "#555555"
                        MouseArea {
                            anchors.fill: parent
                            onClicked: lineColorDialog.open()
                        }
                    }
                }

                ColorDialog {
                    id: bgColorDialog
                    color: Ortho.bgColor
                    onAccepted: Ortho.bgColor = color.toString()
                }
                ColorDialog {
                    id: lineColorDialog
                    color: Ortho.lineColor
                    onAccepted: Ortho.lineColor = color.toString()
                }

                // Rotation
                Text { text: "ROTATION"; color: "#cccccc"; font.bold: true; font.pixelSize: 12 }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Slider {
                        Layout.fillWidth: true
                        from: -180; to: 180
                        value: Ortho.rotationDeg
                        onMoved: Ortho.rotationDeg = value
                    }
                    Text { text: Math.round(Ortho.rotationDeg) + "°"; color: "#888888"; font.pixelSize: 11 }
                }

                // Scale
                Text { text: "SCALE"; color: "#cccccc"; font.bold: true; font.pixelSize: 12 }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Slider {
                        Layout.fillWidth: true
                        from: 25; to: 100
                        value: Ortho.scalePct
                        onMoved: Ortho.scalePct = value
                    }
                    Text { text: Ortho.scalePct + "%"; color: "#888888"; font.pixelSize: 11 }
                }

                // Advanced options
                Text { text: "OPTIONS"; color: "#cccccc"; font.bold: true; font.pixelSize: 12 }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    spacing: 4

                    CheckBox { text: "Rib cuts"; checked: Ortho.includeRib; onCheckedChanged: Ortho.includeRib = checked; color: "#cccccc"; font.pixelSize: 11 }
                    SpinBox { from: 1; to: 10; value: Ortho.ribCuts; onValueModified: Ortho.ribCuts = value }
                    CheckBox { text: "Label parts"; checked: Ortho.labelParts; onCheckedChanged: Ortho.labelParts = checked; color: "#cccccc"; font.pixelSize: 11 }
                    ComboBox { Layout.preferredWidth: 80; model: ["y", "z"]; currentIndex: model.indexOf(Ortho.upAxis); onCurrentIndexChanged: Ortho.upAxis = model[currentIndex] }
                    CheckBox { text: "Flip front"; checked: Ortho.frontFlip; onCheckedChanged: Ortho.frontFlip = checked; color: "#cccccc"; font.pixelSize: 11 }
                }

                Item { Layout.fillHeight: true }
            }
        }

        // Right panel - Result
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#252526"
            radius: 6

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 8

                // Progress bar
                RowLayout {
                    Layout.fillWidth: true
                    visible: Ortho.progress > 0 && Ortho.progress < 1
                    spacing: 8
                    ProgressBar {
                        Layout.fillWidth: true
                        value: Ortho.progress
                    }
                    Text {
                        text: Ortho.statusMessage
                        color: "#888888"
                        font.pixelSize: 11
                    }
                }

                // Error
                Rectangle {
                    Layout.fillWidth: true
                    height: 30
                    color: "#5a1d1d"
                    radius: 4
                    visible: errorLabel.visible

                    Text {
                        id: errorLabel
                        anchors.centerIn: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        color: "#ff6b6b"
                        font.pixelSize: 12
                        text: ""
                        visible: false
                    }
                }

                // Drop zone / result image
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "#1e1e1e"
                    radius: 4
                    border.color: dropArea.containsDrag ? "#4fc3f7" : "#333333"
                    border.width: dropArea.containsDrag ? 2 : 1

                    Image {
                        id: resultImage
                        anchors.fill: parent
                        anchors.margins: 8
                        fillMode: Image.PreserveAspectFit
                        source: Ortho.resultImagePath ? "file:///" + Ortho.resultImagePath : ""

                        BusyIndicator {
                            anchors.centerIn: parent
                            running: Ortho.progress > 0 && Ortho.progress < 1
                            width: 64; height: 64
                        }

                        Text {
                            anchors.centerIn: parent
                            text: "Drop a 3D model file here\nor click Load Model"
                            color: "#555555"
                            font.pixelSize: 16
                            horizontalAlignment: Text.AlignHCenter
                            visible: !resultImage.source.toString()
                        }
                    }

                    DropArea {
                        id: dropArea
                        anchors.fill: parent
                        onDropped: {
                            if (drop.hasUrls) {
                                var url = drop.urls[0].toString()
                                var path = url.replace("file:///", "")
                                Ortho.loadFile(path)
                            }
                        }
                    }
                }
            }
        }
    }
}
