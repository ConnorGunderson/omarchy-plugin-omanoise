import QtQuick
import Quickshell
import Quickshell.Io
import qs.Commons
import qs.Ui
import QtQuick.Effects

// Omanoise bar widget: a headphones icon (accent-lit while playing) plus a
// popup panel. All state lives in Service.qml — the bar exists once per
// monitor, so this file only mirrors and drives the shared service.
Panel {
  id: root
  moduleName: "io.github.pepsoren.omanoise"
  ipcTarget: "omanoise"
  manageIpc: false

  readonly property var svc: bar && bar.shell ? bar.shell.serviceFor("io.github.pepsoren.omanoise") : null

  readonly property bool playing: svc ? svc.playing : false
  readonly property string mode: svc ? svc.mode : "focus"
  readonly property real volume: svc ? svc.volume : 0
  readonly property real intensity: svc ? svc.intensity : 0
  readonly property real brightness: svc ? svc.brightness : 0
  readonly property real tonal: svc ? svc.tonal : 0
  readonly property bool binaural: svc ? svc.binaural : false
  readonly property bool modulation: svc ? svc.modulation : false
  readonly property bool adaptive: svc ? svc.adaptive : false
  readonly property bool pulse: svc ? svc.pulse : false
  readonly property var envSounds: svc ? svc.envSounds : []
  readonly property real meter: svc ? svc.meter : 0
  readonly property bool engineFound: svc ? svc.engineFound : false
  readonly property bool engineChecked: svc ? svc.engineChecked : false
  readonly property bool building: svc ? svc.building : false
  readonly property string buildError: svc ? svc.buildError : ""
  readonly property int timerMinutes: svc ? svc.timerMinutes : 0
  readonly property bool bankReady: svc ? svc.bankReady : false
  readonly property bool sf2Ready: svc ? svc.sf2Ready : false
  readonly property bool soundfontFound: svc ? svc.soundfontFound : true

  readonly property var modes: svc ? svc.modes : []
  function modeInfo(id) { return svc ? svc.modeInfo(id) : { id: "focus", label: "Focus", icon: "󰋋", blurb: "" } }
  function daypartLabel() { return svc ? svc.daypartLabel() : "" }
  function timerLabel() { return svc ? svc.timerLabel() : "" }

  function send(cmd) { if (svc) svc.send(cmd) }
  function play() { if (svc) svc.play() }
  function pause() { if (svc) svc.pause() }
  function playPause() { if (svc) svc.playPause() }
  function setMode(id) { if (svc) svc.setMode(id) }
  function nextMode() { if (svc) svc.nextMode() }
  function setVolume(v) { if (svc) svc.setVolume(v) }
  function nudgeVolume(d) { if (svc) svc.nudgeVolume(d) }
  function setTimer(m) { if (svc) svc.setTimer(m) }

  // The shell hands settings to the widget; the service is what acts on them.
  function pushSettings() { if (svc && typeof svc.applySettings === "function") svc.applySettings(settings) }
  onSettingsChanged: pushSettings()
  onSvcChanged: pushSettings()

  implicitWidth: button.implicitWidth
  implicitHeight: button.implicitHeight

  IpcHandler {
    target: "omanoise"
    function open(): void { root.open() }
    function close(): void { root.close() }
    function toggle(): void { root.toggle() }
    function play(): void { root.play() }
    function pause(): void { root.pause() }
    function playpause(): void { root.playPause() }
    function next(): void { root.nextMode() }
    function mode(id: string): void { root.setMode(id) }
    function volume(v: string): void { root.setVolume(Number(v)) }
    function env(name: string, v: string): void { if (root.svc) root.svc.setEnv(name, Number(v)) }
    function timer(minutes: string): void { root.setTimer(Math.max(0, Math.round(Number(minutes) || 0))) }
    function state(): string { return root.svc ? root.svc.stateJson() : "{\"error\":\"service not loaded\"}" }
  }

  // ---- bar button
  BarIconButton {
    id: button
    anchors.fill: parent
    bar: root.bar
    active: root.playing
    activeColor: Color.accent
    // Custom mark (omanoise.svg): an "O" with a noise wave through it,
    // recoloured to the bar foreground, accent while playing.
    iconComponent: Component {
      Item {
        Image {
          anchors.centerIn: parent
          width: Math.round(Style.bar.iconCanvas * 0.92)
          height: width
          source: Qt.resolvedUrl("omanoise.svg")
          sourceSize: Qt.size(48, 48)
          smooth: true
          layer.enabled: true
          layer.effect: MultiEffect {
            colorization: 1
            colorizationColor: root.playing ? Color.accent : button.foreground
          }
        }
      }
    }
    tooltipText: root.engineFound
      ? "Omanoise · " + root.modeInfo(root.mode).label + (root.playing ? " · playing" : " · paused")
        + "\nleft: panel · right: play/pause · middle: next mode · scroll: volume"
      : "Omanoise · engine not built (open panel)"
    onPressed: function(b) {
      if (b === Qt.RightButton) root.playPause()
      else if (b === Qt.MiddleButton) root.nextMode()
      else root.toggle()
    }
    onWheelMoved: function(delta) {
      if (delta > 0) root.nudgeVolume(0.05)
      else if (delta < 0) root.nudgeVolume(-0.05)
    }

    // Soft level glow behind the icon while playing
    Rectangle {
      anchors.centerIn: parent
      width: parent.width * 0.9
      height: parent.height * 0.7
      radius: Style.cornerRadius
      z: -1
      color: Color.accent
      opacity: root.playing ? 0.08 + Math.min(0.22, root.meter * 1.6) : 0
      Behavior on opacity { NumberAnimation { duration: 120 } }
    }
  }

  // ---- panel
  KeyboardPanel {
    id: panel
    anchorItem: button
    owner: root
    bar: root.bar
    open: root.opened
    focusTarget: keyCatcher
    contentWidth: panel.fittedContentWidth(Style.space(340))
    contentHeight: panel.fittedContentHeight(column.implicitHeight)

    PanelKeyCatcher {
      id: keyCatcher
      anchors.fill: parent
      onCloseRequested: root.close()
      onTabRequested: function(direction) { root.switchPanel(direction) }
      Keys.onSpacePressed: root.playPause()

      Column {
        id: column
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        spacing: Style.space(14)

        // ---------- Hero
        Item {
          width: parent.width
          implicitHeight: Math.max(heroIcon.implicitHeight, heroLabels.implicitHeight, playButton.implicitHeight)

          Text {
            id: heroIcon
            textFormat: Text.PlainText
            text: root.modeInfo(root.mode).icon
            color: root.playing ? Color.accent : root.bar.foreground
            font.family: root.bar.fontFamily
            font.pixelSize: Style.font.displayLarge
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            Behavior on color { ColorAnimation { duration: 250 } }
          }

          Column {
            id: heroLabels
            anchors.left: heroIcon.right
            anchors.leftMargin: Style.space(14)
            anchors.right: playButton.left
            anchors.rightMargin: Style.space(10)
            anchors.verticalCenter: parent.verticalCenter
            spacing: Style.space(2)

            Text {
              text: "Omanoise"
              color: root.bar.foreground
              font.family: root.bar.fontFamily
              font.pixelSize: Style.font.title
              font.bold: true
              elide: Text.ElideRight
              width: parent.width
            }

            Text {
              textFormat: Text.PlainText
              text: {
                var m = root.modeInfo(root.mode).label.toUpperCase()
                var d = root.adaptive && root.daypartLabel() !== "" ? " · " + root.daypartLabel().toUpperCase() : ""
                var st = !root.svc ? "SERVICE LOADING"
                       : !root.engineFound ? "ENGINE MISSING"
                       : (!root.sf2Ready && root.soundfontFound) ? "LOADING SOUNDS"
                       : root.playing ? "PLAYING" : "PAUSED"
                return m + d + " · " + st
              }
              color: Qt.darker(root.bar.foreground, 1.4)
              font.family: root.bar.fontFamily
              font.pixelSize: Style.font.caption
              font.bold: true
              font.letterSpacing: 1.2
              elide: Text.ElideRight
              width: parent.width
            }
          }

          Button {
            id: playButton
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            iconText: root.playing ? "󰏤" : "󰐊"
            iconSize: Style.font.display
            foreground: root.playing ? Color.accent : root.bar.foreground
            fontFamily: root.bar.fontFamily
            bordered: true
            horizontalPadding: Style.space(12)
            verticalPadding: Style.space(6)
            tooltipText: root.playing ? "Pause" : "Play"
            onClicked: root.playPause()
          }
        }

        // ---------- Level meter
        Item {
          width: parent.width
          implicitHeight: Style.space(6)

          Rectangle {
            id: meterTrack
            anchors.fill: parent
            radius: height / 2
            color: Qt.rgba(root.bar.foreground.r, root.bar.foreground.g, root.bar.foreground.b, 0.12)
          }

          Rectangle {
            anchors.left: meterTrack.left
            anchors.verticalCenter: meterTrack.verticalCenter
            height: meterTrack.height
            radius: meterTrack.radius
            color: Color.accent
            width: root.playing ? Math.min(meterTrack.width, meterTrack.width * Math.min(1, root.meter * 3.2)) : 0
            Behavior on width { NumberAnimation { duration: 100 } }
          }
        }

        // ---------- Engine missing / build
        Column {
          visible: root.engineChecked && !root.engineFound
          width: parent.width
          spacing: Style.space(8)

          Text {
            width: parent.width
            wrapMode: Text.WordWrap
            text: root.building ? "Building the engine…"
              : "The native audio engine is not built yet. It compiles in a second with gcc and the PipeWire headers."
            color: Qt.darker(root.bar.foreground, 1.3)
            font.family: root.bar.fontFamily
            font.pixelSize: Style.font.bodySmall
          }

          Text {
            visible: root.buildError !== ""
            width: parent.width
            wrapMode: Text.WrapAnywhere
            text: root.buildError
            color: Color.urgent
            font.family: root.bar.fontFamily
            font.pixelSize: Style.font.caption
          }

          Button {
            width: parent.width
            text: root.building ? "Building…" : "Build engine"
            iconText: "󰣪"
            iconSpinning: root.building
            fontSize: Style.font.bodySmall
            foreground: root.bar.foreground
            fontFamily: root.bar.fontFamily
            bordered: true
            onClicked: if (!root.building && root.svc) root.svc.build()
          }
        }

        // ---------- Mode
        PanelSeparator { foreground: root.bar.foreground }

        Column {
          width: parent.width
          spacing: Style.space(8)

          PanelSectionHeader {
            text: "MODE"
            foreground: root.bar.foreground
            fontFamily: root.bar.fontFamily
          }

          Row {
            id: modeRow
            width: parent.width
            spacing: Style.space(6)
            readonly property real cellWidth: root.modes.length > 0
              ? (width - spacing * (root.modes.length - 1)) / root.modes.length : width

            Repeater {
              model: root.modes
              Button {
                required property var modelData
                width: modeRow.cellWidth
                iconText: modelData.icon
                text: modelData.label
                fontSize: Style.font.bodySmall
                iconSize: Style.font.body
                foreground: root.bar.foreground
                fontFamily: root.bar.fontFamily
                horizontalPadding: Style.space(4)
                verticalPadding: Style.spacing.controlPaddingY
                bordered: true
                selected: root.mode === modelData.id
                tooltipText: modelData.blurb
                onClicked: root.setMode(modelData.id)
              }
            }
          }

          Text {
            width: parent.width
            text: root.modeInfo(root.mode).blurb
            color: Qt.darker(root.bar.foreground, 1.5)
            font.family: root.bar.fontFamily
            font.pixelSize: Style.font.caption
            elide: Text.ElideRight
          }

          Text {
            width: parent.width
            visible: !root.soundfontFound
            text: "Install soundfont-fluid for piano and harp"
            color: Qt.darker(root.bar.foreground, 1.3)
            font.family: root.bar.fontFamily
            font.pixelSize: Style.font.caption
            wrapMode: Text.WordWrap
          }
        }

        // ---------- Environment bank (Environment mode only)
        PanelSeparator {
          visible: root.mode === "environment"
          foreground: root.bar.foreground
        }

        Column {
          visible: root.mode === "environment"
          width: parent.width
          spacing: Style.space(6)

          PanelSectionHeader {
            text: "ENVIRONMENT"
            foreground: root.bar.foreground
            fontFamily: root.bar.fontFamily
          }

          Repeater {
            model: root.envSounds
            ParamRow {
              required property var modelData
              readonly property bool available: root.svc ? root.svc.envAvailable(modelData.id) : true
              label: modelData.label
              icon: modelData.icon
              // A recorded slot whose WAV is missing is dimmed and inert, and
              // says which file it wants.
              opacity: available ? 1.0 : 0.4
              enabled: available
              paramValue: root.svc ? root.svc.envLevel(modelData.id) : 0
              hint: available
                ? "Blend this sound into the environment — several at once is fine"
                : "sounds/" + modelData.file + ".wav missing"
              apply: function(v) { if (root.svc) root.svc.setEnv(modelData.id, v) }
            }
          }
        }

        // ---------- Sound
        PanelSeparator { foreground: root.bar.foreground }

        Column {
          width: parent.width
          spacing: Style.space(6)

          PanelSectionHeader {
            text: "SOUND"
            foreground: root.bar.foreground
            fontFamily: root.bar.fontFamily
          }

          ParamRow { label: "Volume"; icon: "󰕾"; param: "volume"; paramValue: root.volume }
          ParamRow { label: "Intensity"; icon: "󰓅"; param: "intensity"; paramValue: root.intensity
                     hint: "How busy: gust and tone density, breathing depth" }
          ParamRow { label: "Brightness"; icon: "󰃠"; param: "brightness"; paramValue: root.brightness
                     hint: "Darker, muffled wash ↔ airy, open wash" }
          ParamRow { label: "Tonal"; icon: "󰝚"; param: "tonal"; paramValue: root.tonal
                     hint: "Pure noise ↔ pads and bells on top" }
        }

        // ---------- Options
        PanelSeparator { foreground: root.bar.foreground }

        Column {
          width: parent.width
          spacing: Style.space(6)

          OptionRow {
            visible: root.mode === "focus"
            label: "Neural modulation"
            description: "16 Hz amplitude modulation on the low-mids, the one focus effect with EEG evidence (can feel tense — off by default)"
            checked: root.modulation
            onClicked: if (root.svc) root.svc.setFlag("modulation", !root.modulation)
          }

          OptionRow {
            visible: root.mode !== "environment"
            label: "Binaural beats"
            description: "Weak evidence — off by default"
            checked: root.binaural
            onClicked: if (root.svc) root.svc.setFlag("binaural", !root.binaural)
          }

          OptionRow {
            label: "Adapt to time of day"
            description: root.adaptive
              ? "Darker and slower in the evening and at night — now: " + (root.daypartLabel() || "…")
              : "Same sound at every hour"
            checked: root.adaptive
            onClicked: if (root.svc) root.svc.setFlag("adaptive", !root.adaptive)
          }

          OptionRow {
            visible: root.mode === "focus"
            label: "Pulse"
            description: "Soft 64 bpm tick under the wash"
            checked: root.pulse
            onClicked: if (root.svc) root.svc.setFlag("pulse", !root.pulse)
          }
        }

        // ---------- Timer
        PanelSeparator { foreground: root.bar.foreground }

        Column {
          width: parent.width
          spacing: Style.space(8)

          Item {
            width: parent.width
            implicitHeight: timerHeader.implicitHeight

            PanelSectionHeader {
              id: timerHeader
              anchors.left: parent.left
              text: "STOP AFTER"
              foreground: root.bar.foreground
              fontFamily: root.bar.fontFamily
            }

            Text {
              anchors.right: parent.right
              anchors.verticalCenter: parent.verticalCenter
              visible: root.timerMinutes > 0
              text: root.timerLabel()
              color: Color.accent
              font.family: root.bar.fontFamily
              font.pixelSize: Style.font.caption
              font.bold: true
            }
          }

          Row {
            id: timerRow
            width: parent.width
            spacing: Style.space(6)
            readonly property var options: [
              { label: "Off", value: 0 },
              { label: "25m", value: 25 },
              { label: "50m", value: 50 },
              { label: "90m", value: 90 },
              { label: "3h", value: 180 }
            ]
            readonly property real cellWidth: (width - spacing * (options.length - 1)) / options.length

            Repeater {
              model: timerRow.options
              Button {
                required property var modelData
                width: timerRow.cellWidth
                text: modelData.label
                fontSize: Style.font.bodySmall
                foreground: root.bar.foreground
                fontFamily: root.bar.fontFamily
                horizontalPadding: Style.space(4)
                verticalPadding: Style.spacing.controlPaddingY
                bordered: true
                selected: root.timerMinutes === modelData.value
                onClicked: root.setTimer(modelData.value)
              }
            }
          }
        }
      }
    }
  }

  // ---- reusable rows
  component ParamRow: Item {
    property string label: ""
    property string icon: ""
    property string hint: ""
    property string param: ""
    property real paramValue: 0
    // Optional: a slider that is not one of the engine's named 0..1 parameters
    // (the Environment levels) supplies its own setter here instead of `param`.
    property var apply: null

    width: parent.width
    height: Style.space(24)

    Text {
      id: rowIcon
      anchors.left: parent.left
      anchors.verticalCenter: parent.verticalCenter
      text: icon
      color: root.bar.foreground
      opacity: 0.7
      font.family: root.bar.fontFamily
      font.pixelSize: Style.font.body
      width: Style.space(18)
    }

    Text {
      id: rowLabel
      anchors.left: rowIcon.right
      anchors.verticalCenter: parent.verticalCenter
      text: label
      color: root.bar.foreground
      opacity: 0.8
      font.family: root.bar.fontFamily
      font.pixelSize: Style.font.bodySmall
      width: Style.space(68)
      elide: Text.ElideRight

      HoverHandler { id: labelHover }
      PanelToolTip { text: hint; visible: hint !== "" && labelHover.hovered }
    }

    PanelSlider {
      bar: root.bar
      anchors.left: rowLabel.right
      anchors.leftMargin: Style.space(6)
      anchors.right: rowValue.left
      anchors.rightMargin: Style.space(8)
      anchors.verticalCenter: parent.verticalCenter
      height: parent.height
      minimum: 0
      maximum: 1
      step: 0.05
      value: paramValue
      onMoved: function(v) {
        if (apply) apply(v)
        else if (root.svc) root.svc.setParam(param, v)
      }
    }

    Text {
      id: rowValue
      anchors.right: parent.right
      anchors.verticalCenter: parent.verticalCenter
      text: Math.round(paramValue * 100) + "%"
      color: root.bar.foreground
      opacity: 0.75
      font.family: root.bar.fontFamily
      font.pixelSize: Style.font.bodySmall
      width: Style.space(34)
      horizontalAlignment: Text.AlignRight
    }
  }

  component OptionRow: Item {
    id: optionRow
    property string label: ""
    property string description: ""
    property bool checked: false
    signal clicked()

    width: parent.width
    implicitHeight: visible ? Math.max(labels.implicitHeight, sw.implicitHeight) + Style.space(4) : 0

    Column {
      id: labels
      anchors.left: parent.left
      anchors.right: sw.left
      anchors.rightMargin: Style.space(10)
      anchors.verticalCenter: parent.verticalCenter
      spacing: Style.space(1)

      Text {
        width: parent.width
        text: optionRow.label
        color: root.bar.foreground
        font.family: root.bar.fontFamily
        font.pixelSize: Style.font.bodySmall
        font.bold: true
        elide: Text.ElideRight
      }
      Text {
        width: parent.width
        text: optionRow.description
        color: Qt.darker(root.bar.foreground, 1.5)
        font.family: root.bar.fontFamily
        font.pixelSize: Style.font.caption
        wrapMode: Text.WordWrap
      }
    }

    ToggleSwitch {
      id: sw
      anchors.right: parent.right
      anchors.verticalCenter: parent.verticalCenter
      checked: optionRow.checked
      foreground: root.bar.foreground
      trackHeight: Math.max(16, Math.round(Style.spacing.controlHeight * 0.42))
      onToggled: optionRow.clicked()
    }

    MouseArea {
      anchors.fill: labels
      cursorShape: Qt.PointingHandCursor
      onClicked: optionRow.clicked()
    }
  }
}
