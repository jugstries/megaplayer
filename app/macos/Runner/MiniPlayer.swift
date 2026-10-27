import Cocoa
import FlutterMacOS
import SwiftUI

// Canale "megaplayer/tray": il miniplayer nella barra dei menu. L'app manda
// con "update" lo stato (se c'è, titolo e autore, play o pausa, il volume, i
// testi delle voci); qui c'è l'icona e, al clic, un pannello con i comandi,
// che tornano all'app come "togglePause", "previous", "next" e "setVolume".
// Riaprire la finestra e uscire si fanno qui. Finché l'icona c'è, chiudere la
// finestra la nasconde (MainFlutterWindow) e la musica continua.
final class MiniPlayer: NSObject, NSPopoverDelegate {
  private let channel: FlutterMethodChannel
  private weak var window: NSWindow?
  private let model = MiniPlayerModel()
  private var item: NSStatusItem?
  private var clicksOutside: Any?

  private lazy var popover: NSPopover = {
    let popover = NSPopover()
    popover.behavior = .transient
    popover.delegate = self
    popover.contentViewController = NSHostingController(
      rootView: MiniPlayerView(model: model) { [weak self] action in self?.perform(action) })
    return popover
  }()

  /// Se l'icona è nella barra dei menu.
  var isShown: Bool { item != nil }

  init(messenger: FlutterBinaryMessenger, window: NSWindow) {
    channel = FlutterMethodChannel(name: "megaplayer/tray", binaryMessenger: messenger)
    self.window = window
    super.init()
    channel.setMethodCallHandler { [weak self] call, result in
      guard call.method == "update", let state = call.arguments as? [String: Any] else {
        result(FlutterMethodNotImplemented)
        return
      }
      self?.update(state)
      result(nil)
    }
  }

  /// La finestra di nuovo davanti, anche se era nascosta o nel Dock.
  func showWindow() {
    guard let window else { return }
    if window.isMiniaturized { window.deminiaturize(nil) }
    window.makeKeyAndOrderFront(nil)
    if #available(macOS 14, *) {
      NSApp.activate()
    } else {
      NSApp.activate(ignoringOtherApps: true)
    }
  }

  private func update(_ state: [String: Any]) {
    guard state["visible"] as? Bool == true else {
      if popover.isShown { popover.performClose(nil) }
      if let item { NSStatusBar.system.removeStatusItem(item) }
      item = nil
      return
    }
    model.labels = state["labels"] as? [String: String] ?? model.labels
    model.title = state["title"] as? String ?? ""
    model.subtitle = state["subtitle"] as? String ?? ""
    model.playing = state["playing"] as? Bool ?? false
    model.canPlay = state["canPlay"] as? Bool ?? false
    model.canPrevious = state["canPrevious"] as? Bool ?? false
    model.canNext = state["canNext"] as? Bool ?? false
    // Mentre si trascina il cursore vale quello, non l'eco dall'app.
    if !model.draggingVolume, let volume = state["volume"] as? Double {
      model.volume = volume
    }
    let button = (item ?? addItem()).button
    button?.toolTip = [model.heading, model.subtitle].filter { !$0.isEmpty }.joined(separator: " — ")
  }

  private func addItem() -> NSStatusItem {
    let item = NSStatusBar.system.statusItem(withLength: NSStatusItem.squareLength)
    if let button = item.button {
      // StatusIcon è un'immagine template (tool/make_icons.py): il colore lo
      // dà la barra, chiara o scura.
      button.image = NSImage(named: "StatusIcon")
      button.image?.isTemplate = true
      button.setAccessibilityLabel("megaplayer")
      button.target = self
      button.action = #selector(togglePopover(_:))
      button.sendAction(on: [.leftMouseUp, .rightMouseUp])
    }
    self.item = item
    return item
  }

  @objc private func togglePopover(_ sender: NSStatusBarButton) {
    if popover.isShown {
      popover.performClose(sender)
      return
    }
    popover.show(relativeTo: sender.bounds, of: sender, preferredEdge: .minY)
    popover.contentViewController?.view.window?.makeKey()
    // Con l'app in secondo piano il pannello non si accorge da solo dei clic
    // nelle altre app.
    clicksOutside = NSEvent.addGlobalMonitorForEvents(matching: [.leftMouseDown, .rightMouseDown]) {
      [weak self] _ in self?.popover.performClose(nil)
    }
  }

  func popoverDidClose(_ notification: Notification) {
    if let clicksOutside { NSEvent.removeMonitor(clicksOutside) }
    clicksOutside = nil
  }

  private func perform(_ action: MiniPlayerAction) {
    switch action {
    case .togglePause:
      channel.invokeMethod("togglePause", arguments: nil)
    case .previous:
      channel.invokeMethod("previous", arguments: nil)
    case .next:
      channel.invokeMethod("next", arguments: nil)
    case .volume(let value):
      channel.invokeMethod("setVolume", arguments: value)
    case .open:
      popover.performClose(nil)
      showWindow()
    case .quit:
      NSApp.terminate(nil)
    }
  }
}

enum MiniPlayerAction {
  case togglePause, previous, next, open, quit
  case volume(Double)
}

/// Lo stato del pannello, come arriva dall'app.
final class MiniPlayerModel: ObservableObject {
  @Published var title = ""
  @Published var subtitle = ""
  @Published var playing = false
  @Published var canPlay = false
  @Published var canPrevious = false
  @Published var canNext = false
  @Published var volume = 0.8
  @Published var labels: [String: String] = [:]
  var draggingVolume = false

  func label(_ key: String) -> String { labels[key] ?? "" }

  /// Il titolo, o che non suona niente.
  var heading: String { title.isEmpty ? label("idle") : title }
}

/// Il pannello: titolo e autore, i tre comandi, il volume, la finestra e
/// l'uscita.
struct MiniPlayerView: View {
  @ObservedObject var model: MiniPlayerModel
  let perform: (MiniPlayerAction) -> Void

  var body: some View {
    VStack(alignment: .leading, spacing: 14) {
      VStack(alignment: .leading, spacing: 3) {
        Text(model.heading)
          .font(.headline)
          .foregroundStyle(model.title.isEmpty ? .secondary : .primary)
          .lineLimit(2)
        if !model.subtitle.isEmpty {
          Text(model.subtitle)
            .font(.subheadline)
            .foregroundStyle(.secondary)
            .lineLimit(1)
        }
      }
      .frame(maxWidth: .infinity, alignment: .leading)

      HStack(spacing: 30) {
        transport("backward.fill", size: 18, label: "previous", enabled: model.canPrevious, action: .previous)
        transport(
          model.playing ? "pause.fill" : "play.fill", size: 26,
          label: model.playing ? "pause" : "play", enabled: model.canPlay, action: .togglePause)
        transport("forward.fill", size: 18, label: "next", enabled: model.canNext, action: .next)
      }
      .frame(maxWidth: .infinity)

      HStack(spacing: 8) {
        Image(systemName: "speaker.fill").foregroundStyle(.secondary)
        Slider(
          value: Binding(
            get: { model.volume },
            set: { value in
              model.volume = value
              perform(.volume(value))
            }),
          in: 0...1,
          onEditingChanged: { editing in model.draggingVolume = editing })
        .controlSize(.small)
        .accessibilityLabel(model.label("volume"))
        Image(systemName: "speaker.wave.3.fill").foregroundStyle(.secondary)
      }
      .font(.system(size: 11))

      Divider()

      HStack {
        Button(model.label("open")) { perform(.open) }
        Spacer()
        Button(model.label("quit")) { perform(.quit) }
      }
      .buttonStyle(.borderless)
    }
    .padding(16)
    .frame(width: 300)
  }

  private func transport(
    _ symbol: String, size: CGFloat, label key: String, enabled: Bool, action: MiniPlayerAction
  ) -> some View {
    Button {
      perform(action)
    } label: {
      Image(systemName: symbol)
        .font(.system(size: size))
        .frame(width: 36, height: 36)
        .contentShape(Rectangle())
    }
    .buttonStyle(.plain)
    .disabled(!enabled)
    .help(model.label(key))
    .accessibilityLabel(model.label(key))
  }
}
