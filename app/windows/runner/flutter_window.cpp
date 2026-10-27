#include "flutter_window.h"

#include <optional>

#include "flutter/generated_plugin_registrant.h"

FlutterWindow::FlutterWindow(const flutter::DartProject& project)
    : project_(project) {}

FlutterWindow::~FlutterWindow() {}

bool FlutterWindow::OnCreate() {
  if (!Win32Window::OnCreate()) {
    return false;
  }

  RECT frame = GetClientArea();

  // The size here must match the window dimensions to avoid unnecessary surface
  // creation / destruction in the startup path.
  flutter_controller_ = std::make_unique<flutter::FlutterViewController>(
      frame.right - frame.left, frame.bottom - frame.top, project_);
  // Ensure that basic setup of the controller was successful.
  if (!flutter_controller_->engine() || !flutter_controller_->view()) {
    return false;
  }
  RegisterPlugins(flutter_controller_->engine());
  flutter::BinaryMessenger* messenger = flutter_controller_->engine()->messenger();
  files_channel_ = CreateFilesChannel(messenger, GetHandle());
  icon_channel_ = CreateIconChannel(messenger, GetHandle());
  drop_channel_ = CreateDropChannel(messenger, GetHandle());
  tray_channel_ = CreateTrayChannel(messenger, GetHandle());
  SetChildContent(flutter_controller_->view()->GetNativeWindow());

  flutter_controller_->engine()->SetNextFrameCallback([&]() {
    this->Show();
  });

  // Flutter can complete the first frame before the "show window" callback is
  // registered. The following call ensures a frame is pending to ensure the
  // window is shown. It is a no-op if the first frame hasn't completed yet.
  flutter_controller_->ForceRedraw();

  return true;
}

void FlutterWindow::OnDestroy() {
  RevokeDropTarget(GetHandle());
  RemoveTrayIcon();
  files_channel_ = nullptr;
  icon_channel_ = nullptr;
  drop_channel_ = nullptr;
  tray_channel_ = nullptr;
  if (flutter_controller_) {
    flutter_controller_ = nullptr;
  }

  Win32Window::OnDestroy();
}

LRESULT
FlutterWindow::MessageHandler(HWND hwnd, UINT const message,
                              WPARAM const wparam,
                              LPARAM const lparam) noexcept {
  // Il miniplayer nell'area di notifica: i clic sulla sua icona, e finché
  // c'è chiudere la finestra la nasconde e la musica continua.
  if (HandleTrayMessage(hwnd, message, wparam, lparam)) {
    return 0;
  }
  if (message == WM_CLOSE && TrayIconShown()) {
    ::ShowWindow(hwnd, SW_HIDE);
    return 0;
  }

  // Give Flutter, including plugins, an opportunity to handle window messages.
  if (flutter_controller_) {
    std::optional<LRESULT> result =
        flutter_controller_->HandleTopLevelWindowProc(hwnd, message, wparam,
                                                      lparam);
    if (result) {
      return *result;
    }
  }

  switch (message) {
    case WM_FONTCHANGE:
      flutter_controller_->engine()->ReloadSystemFonts();
      break;
    case WM_GETMINMAXINFO: {
      // La finestra più piccola che le skin reggono, come su macOS: 960×640
      // punti di contenuto, più bordi e titolo.
      const UINT dpi = ::GetDpiForWindow(hwnd);
      RECT frame = {0, 0, ::MulDiv(960, dpi, 96), ::MulDiv(640, dpi, 96)};
      ::AdjustWindowRectExForDpi(
          &frame, static_cast<DWORD>(::GetWindowLong(hwnd, GWL_STYLE)), FALSE,
          static_cast<DWORD>(::GetWindowLong(hwnd, GWL_EXSTYLE)), dpi);
      auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
      limits->ptMinTrackSize.x = frame.right - frame.left;
      limits->ptMinTrackSize.y = frame.bottom - frame.top;
      return 0;
    }
  }

  return Win32Window::MessageHandler(hwnd, message, wparam, lparam);
}
