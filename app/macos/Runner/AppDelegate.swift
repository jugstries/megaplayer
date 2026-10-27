import Cocoa
import FlutterMacOS

@main
class AppDelegate: FlutterAppDelegate {
  // Con il miniplayer nella barra dei menu l'app resta aperta anche senza
  // finestre: si esce dal miniplayer o con Cmd-Q.
  override func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool {
    return miniPlayer?.isShown != true
  }

  override func applicationSupportsSecureRestorableState(_ app: NSApplication) -> Bool {
    return true
  }

  // Un clic sul Dock riapre la finestra nascosta dal miniplayer.
  override func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool)
    -> Bool
  {
    if !flag { miniPlayer?.showWindow() }
    return true
  }

  private var miniPlayer: MiniPlayer? { (mainFlutterWindow as? MainFlutterWindow)?.miniPlayer }
}
