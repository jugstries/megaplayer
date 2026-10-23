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

    super.awakeFromNib()
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
