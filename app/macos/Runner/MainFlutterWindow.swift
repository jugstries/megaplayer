import Cocoa
import FlutterMacOS

class MainFlutterWindow: NSWindow {
  override func awakeFromNib() {
    let flutterViewController = FlutterViewController()
    self.contentViewController = flutterViewController
    self.setContentSize(NSSize(width: 1440, height: 900))
    self.contentMinSize = NSSize(width: 960, height: 640)
    self.center()

    RegisterGeneratedPlugins(registry: flutterViewController)
    FilesChannel.register(with: flutterViewController.engine.binaryMessenger, window: self)
    IconChannel.register(with: flutterViewController.engine.binaryMessenger)

    super.awakeFromNib()
  }
}

// Canale "megaplayer/icon": l'icona nel Dock segue la skin finché l'app è
// aperta. Il bundle porta la moderna; la retrò è RetroIcon in Assets.xcassets
// (tutte e due da tool/make_icons.py).
private enum IconChannel {
  static func register(with messenger: FlutterBinaryMessenger) {
    let channel = FlutterMethodChannel(name: "megaplayer/icon", binaryMessenger: messenger)
    channel.setMethodCallHandler { call, result in
      guard call.method == "setSkin", let skin = call.arguments as? String else {
        result(FlutterMethodNotImplemented)
        return
      }
      // nil rimette l'icona del bundle.
      NSApp.applicationIconImage = skin == "retro" ? NSImage(named: "RetroIcon") : nil
      result(nil)
    }
  }
}

// Canale "megaplayer/files": la finestra di apertura file di macOS e i file
// passati da riga di comando, che Flutter da solo non offre.
private enum FilesChannel {
  static func register(with messenger: FlutterBinaryMessenger, window: NSWindow) {
    let channel = FlutterMethodChannel(name: "megaplayer/files", binaryMessenger: messenger)
    channel.setMethodCallHandler { call, result in
      switch call.method {
      case "openFiles":
        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = true
        panel.canChooseDirectories = false
        panel.canChooseFiles = true
        panel.message = "Scegli moduli, brani o archivi"
        panel.beginSheetModal(for: window) { response in
          result(response == .OK ? panel.urls.map { $0.path } : [String]())
        }
      case "initialFiles":
        // Solo percorsi assoluti: Xcode aggiunge argomenti suoi al lancio.
        result(CommandLine.arguments.dropFirst().filter { $0.hasPrefix("/") })
      default:
        result(FlutterMethodNotImplemented)
      }
    }
  }
}
