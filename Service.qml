import QtQuick
import Quickshell
import Quickshell.Io
import qs.Commons

// Omanoise service: the single owner of the native audio engine. The bar is
// instantiated once per monitor, so the engine cannot live in the widget —
// it would play twice. Widgets find this object through
// bar.shell.serviceFor("io.github.pepsoren.omanoise") and mirror its state.
Item {
  id: root

  property var shell: null
  property var manifest: null
  property var pluginRegistry: null

  readonly property string pluginDir: manifest && manifest.__sourceDir
    ? String(manifest.__sourceDir)
    : Quickshell.env("HOME") + "/.config/omarchy/plugins/io.github.pepsoren.omanoise"
  readonly property string enginePath: pluginDir + "/bin/omanoise-engine"

  // Engine-mirrored state
  property bool playing: false
  property string mode: "focus"
  property real volume: 0.7
  property real intensity: 0.5
  property real brightness: 0.5
  property real tonal: 0.5
  property var env: ({ ocean: 0.8, rain: 0, fire: 0, wind: 0, stream: 0, birds: 0 })
  // Which environment slots can actually make a sound. The two synthesized
  // ones are always true; the recorded ones flip to true when their WAV has
  // loaded (a few hundred ms after the engine starts).
  property var sounds: ({ ocean: true, rain: true, fire: false, wind: false, stream: false, birds: false })
  property bool binaural: false
  property bool modulation: false
  property bool adaptive: true
  property bool pulse: false
  property string daypart: "day"
  property bool bankReady: false
  property bool sf2Ready: false
  // Whether FluidR3_GM.sf2 is on disk at all, so the panel can tell "still
  // loading" apart from "the package is not installed".
  property bool soundfontFound: true
  readonly property string soundfontPath: "/usr/share/soundfonts/FluidR3_GM.sf2"
  property real meter: 0

  property bool engineFound: false
  property bool engineChecked: false
  property bool engineRunning: false
  property bool building: false
  property string buildError: ""
  property bool autoplayDone: false
  property var settings: ({})

  // Stop-after timer (minutes chosen, 0 = off; seconds remaining)
  property int timerMinutes: 0
  property int timerRemainingSec: 0

  readonly property var modes: [
    { id: "focus", label: "Focus", icon: "󰛨", blurb: "Soft piano and vibraphone over a warm pad" },
    { id: "relax", label: "Relax", icon: "󰈸", blurb: "Harp, low piano, glass pad, distant waves" },
    { id: "environment", label: "Environment", icon: "󰞍", blurb: "Blend ocean, rain, fire, wind, stream and birds" }
  ]

  // The six Environment levels, mirrored from the engine's `env` object. Ocean
  // and rain are synthesized; fire, wind, stream and birds are recordings in
  // `sounds/`. `file` is what the panel names when one is missing. Every glyph
  // is checked against the bar font (JetBrainsMono Nerd Font covers
  // U+F0001–U+F1AF0, which contains all six).
  readonly property var envSounds: [
    { id: "ocean", label: "Ocean", icon: "󰞍", file: "ocean" },
    { id: "rain", label: "Rain", icon: "󰖗", file: "rain" },
    { id: "fire", label: "Fire", icon: "󰈸", file: "fireplace" },
    { id: "wind", label: "Wind", icon: "󰖝", file: "wind" },
    { id: "stream", label: "Stream", icon: "󰶟", file: "stream" },
    { id: "birds", label: "Birds", icon: "󱗆", file: "birds" }
  ]

  function modeInfo(id) {
    for (var i = 0; i < modes.length; i++) if (modes[i].id === id) return modes[i]
    return modes[0]
  }

  function daypartLabel() {
    switch (daypart) {
      case "morning": return "morning"
      case "evening": return "evening"
      case "night": return "night"
      case "off": return ""
      default: return "daytime"
    }
  }

  // The shell hands settings to the bar widget, which pushes them here.
  function applySettings(s) {
    root.settings = s || ({})
    maybeAutoplay()
  }

  function maybeAutoplay() {
    if (root.autoplayDone || !root.engineRunning) return
    if (root.settings && root.settings.autoplay === true) {
      root.autoplayDone = true
      play()
    }
  }

  function send(cmd) {
    if (!engine.running) return
    engine.write(cmd + "\n")
  }

  function handleLine(line) {
    line = String(line || "").trim()
    if (line.indexOf("meter ") === 0) {
      root.meter = Number(line.slice(6)) || 0
      return
    }
    if (line.indexOf("state ") !== 0) return
    try {
      var s = JSON.parse(line.slice(6))
      root.playing = !!s.playing
      root.mode = String(s.mode || "focus")
      root.volume = Number(s.volume)
      root.intensity = Number(s.intensity)
      root.brightness = Number(s.brightness)
      root.tonal = Number(s.tonal)
      root.binaural = !!s.binaural
      root.modulation = !!s.modulation
      root.adaptive = !!s.adaptive
      root.pulse = !!s.pulse
      if (s.env !== undefined && s.env !== null) root.env = s.env
      if (s.sounds !== undefined && s.sounds !== null) root.sounds = s.sounds
      root.daypart = String(s.daypart || "day")
      if (s.bank !== undefined) root.bankReady = !!s.bank
      if (s.sf2 !== undefined) root.sf2Ready = !!s.sf2
      if (!root.playing) root.meter = 0
    } catch (e) {
      console.warn("omanoise: bad state line: " + line)
    }
  }

  function play() { send("play") }
  function pause() { send("pause") }
  function playPause() { send("toggle") }
  function setMode(id) { send("mode " + id) }
  function nextMode() {
    for (var i = 0; i < modes.length; i++)
      if (modes[i].id === root.mode) return setMode(modes[(i + 1) % modes.length].id)
    setMode(modes[0].id)
  }
  function setParam(name, v) { send(name + " " + Math.max(0, Math.min(1, Number(v) || 0)).toFixed(3)) }
  function setEnv(name, v) { send("env " + name + " " + Math.max(0, Math.min(1, Number(v) || 0)).toFixed(3)) }
  function envLevel(name) { var e = root.env; return e && e[name] !== undefined ? Number(e[name]) : 0 }
  function envAvailable(name) { var s = root.sounds; return !s || s[name] === undefined ? true : !!s[name] }
  function setVolume(v) { setParam("volume", v) }
  function nudgeVolume(delta) { setVolume(root.volume + delta) }
  function setFlag(name, on) { send(name + " " + (on ? "1" : "0")) }

  function setTimer(minutes) {
    root.timerMinutes = minutes
    root.timerRemainingSec = minutes * 60
    if (minutes > 0 && !root.playing) play()
  }

  function timerLabel() {
    if (root.timerMinutes <= 0) return ""
    var m = Math.floor(root.timerRemainingSec / 60)
    var s = root.timerRemainingSec % 60
    return m + ":" + (s < 10 ? "0" : "") + s
  }

  function stateJson() {
    return JSON.stringify({ playing: playing, mode: mode, volume: volume, intensity: intensity,
      brightness: brightness, tonal: tonal, binaural: binaural, adaptive: adaptive, pulse: pulse,
      modulation: modulation, env: env, sounds: sounds,
      bank: bankReady, sf2: sf2Ready, soundfont: soundfontFound,
      daypart: daypart, timerRemainingSec: timerRemainingSec, engine: engineRunning })
  }

  function checkEngine() { if (!checkProc.running) checkProc.running = true }

  function build() {
    if (buildProc.running) return
    root.building = true
    root.buildError = ""
    buildProc.running = true
  }

  Component.onCompleted: { checkEngine(); sf2Proc.running = true }
  onPluginDirChanged: checkEngine()

  Process {
    id: checkProc
    command: ["sh", "-c", "test -x \"$1\" && echo yes || echo no", "omanoise", root.enginePath]
    stdout: StdioCollector {
      waitForEnd: true
      onStreamFinished: {
        root.engineFound = String(text).trim() === "yes"
        root.engineChecked = true
      }
    }
  }

  Process {
    id: sf2Proc
    command: ["sh", "-c", "test -r \"$1\" && echo yes || echo no", "omanoise", root.soundfontPath]
    stdout: StdioCollector {
      waitForEnd: true
      onStreamFinished: root.soundfontFound = String(text).trim() === "yes"
    }
  }

  Process {
    id: buildProc
    command: [root.pluginDir + "/build.sh"]
    stderr: StdioCollector { id: buildErr; waitForEnd: true }
    onExited: function(code) {
      root.building = false
      if (code === 0) { root.buildError = ""; root.checkEngine() }
      else root.buildError = String(buildErr.text || "").trim().split("\n").slice(-3).join("\n") || ("exit " + code)
    }
  }

  // The engine lives as long as this service does; stdin closing on shell
  // exit makes it quit on its own.
  Process {
    id: engine
    command: [root.enginePath]
    running: root.engineFound
    stdinEnabled: true
    stdout: SplitParser { onRead: function(line) { root.handleLine(line) } }
    stderr: SplitParser { onRead: function(line) { console.warn("omanoise engine: " + line) } }
    onStarted: { root.engineRunning = true; root.maybeAutoplay() }
    onExited: function(code) {
      root.engineRunning = false
      root.playing = false
      root.meter = 0
      if (root.engineFound) restartTimer.restart()
    }
  }

  Timer {
    id: restartTimer
    interval: 3000
    repeat: false
    onTriggered: if (root.engineFound && !engine.running) engine.running = true
  }

  Timer {
    interval: 1000
    repeat: true
    running: root.timerMinutes > 0 && root.playing
    onTriggered: {
      if (root.timerRemainingSec > 1) { root.timerRemainingSec -= 1; return }
      root.timerMinutes = 0
      root.timerRemainingSec = 0
      root.pause()
    }
  }
  onPlayingChanged: if (!playing && timerMinutes > 0) { timerMinutes = 0; timerRemainingSec = 0 }
}
