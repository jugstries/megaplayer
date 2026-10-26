import Cocoa
import FlutterMacOS
import UniformTypeIdentifiers

class MainFlutterWindow: NSWindow {
  // Canale "megaplayer/drop": i file trascinati dal Finder sulla finestra,
  // con "enter" ed "exit" mentre passano e "drop" con i percorsi.
  private var dropChannel: FlutterMethodChannel?

  override func awakeFromNib() {
    let flutterViewController = FlutterViewController()
    self.contentViewController = flutterViewController
    self.setContentSize(NSSize(width: 1440, height: 900))
    self.contentMinSize = NSSize(width: 960, height: 640)
    self.center()

    RegisterGeneratedPlugins(registry: flutterViewController)
    FilesChannel.register(with: flutterViewController.engine.binaryMessenger, window: self)
    IconChannel.register(with: flutterViewController.engine.binaryMessenger)
    dropChannel = FlutterMethodChannel(
      name: "megaplayer/drop", binaryMessenger: flutterViewController.engine.binaryMessenger)
    registerForDraggedTypes([.fileURL])

    super.awakeFromNib()
  }

  // I metodi di NSDraggingDestination: AppKit li chiama sulla finestra
  // registrata per i file, se ci sono.
  @objc func draggingEntered(_ sender: NSDraggingInfo) -> NSDragOperation {
    guard !droppedPaths(sender).isEmpty else { return [] }
    dropChannel?.invokeMethod("enter", arguments: nil)
    return .copy
  }

  @objc func draggingExited(_ sender: NSDraggingInfo?) {
    dropChannel?.invokeMethod("exit", arguments: nil)
  }

  @objc func performDragOperation(_ sender: NSDraggingInfo) -> Bool {
    let paths = droppedPaths(sender)
    dropChannel?.invokeMethod("drop", arguments: paths)
    return !paths.isEmpty
  }

  private func droppedPaths(_ info: NSDraggingInfo) -> [String] {
    let urls = info.draggingPasteboard.readObjects(
      forClasses: [NSURL.self], options: [.urlReadingFileURLsOnly: true]) as? [URL]
    return urls?.map { $0.path } ?? []
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

// Canale "megaplayer/files": le finestre di macOS per aprire file e scegliere
// cartelle, e i file passati da riga di comando, che Flutter da solo non offre.
private enum FilesChannel {
  static func register(with messenger: FlutterBinaryMessenger, window: NSWindow) {
    let channel = FlutterMethodChannel(name: "megaplayer/files", binaryMessenger: messenger)
    channel.setMethodCallHandler { call, result in
      switch call.method {
      case "openFiles":
        // Di solito brani e archivi, anche più d'uno; l'app può chiedere un
        // file solo, di certe estensioni (un SoundFont), con il suo messaggio.
        let arguments = call.arguments as? [String: Any]
        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = !(arguments?["single"] as? Bool ?? false)
        panel.canChooseDirectories = false
        panel.canChooseFiles = true
        panel.message = arguments?["message"] as? String ?? "Scegli moduli, brani o archivi"
        if let extensions = arguments?["extensions"] as? [String] {
          panel.allowedContentTypes = extensions.compactMap { UTType(filenameExtension: $0) }
        }
        panel.beginSheetModal(for: window) { response in
          result(response == .OK ? panel.urls.map { $0.path } : [String]())
        }
      case "openFolder":
        // Una cartella (della libreria, delle ROM): si parte da quella di
        // prima, se c'è, con il messaggio che dà l'app.
        let arguments = call.arguments as? [String: Any]
        let panel = NSOpenPanel()
        panel.allowsMultipleSelection = false
        panel.canChooseDirectories = true
        panel.canChooseFiles = false
        panel.message = arguments?["message"] as? String ?? "Scegli una cartella"
        panel.prompt = "Scegli"
        if let start = arguments?["start"] as? String, !start.isEmpty {
          panel.directoryURL = URL(fileURLWithPath: start)
        }
        panel.beginSheetModal(for: window) { response in
          result(response == .OK ? panel.url?.path : nil)
        }
      case "saveFile":
        // Dove salvare un file che l'app scrive (un brano esportato), con il
        // nome proposto e l'estensione che deve avere.
        let arguments = call.arguments as? [String: Any]
        let panel = NSSavePanel()
        panel.message = arguments?["message"] as? String ?? ""
        panel.nameFieldStringValue = arguments?["name"] as? String ?? ""
        panel.canCreateDirectories = true
        if let extensions = arguments?["extensions"] as? [String] {
          panel.allowedContentTypes = extensions.compactMap { UTType(filenameExtension: $0) }
        }
        panel.beginSheetModal(for: window) { response in
          result(response == .OK ? panel.url?.path : nil)
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
