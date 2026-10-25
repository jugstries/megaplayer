#include <flutter/dart_project.h>
#include <flutter/flutter_view_controller.h>
#include <flutter_windows.h>
#include <windows.h>

#include <algorithm>

#include "flutter_window.h"
#include "utils.h"

// La finestra parte grande come su macOS, ma mai più del 90% dello schermo
// principale, e al centro. Misure in punti logici, come vuole Create.
static void PlaceOnPrimaryScreen(Win32Window::Point& origin,
                                 Win32Window::Size& size) {
  HMONITOR monitor = ::MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  if (!::GetMonitorInfo(monitor, &info)) {
    return;
  }
  const double scale = FlutterDesktopGetDpiForMonitor(monitor) / 96.0;
  const RECT& work = info.rcWork;
  const auto logical = [scale](LONG pixels) {
    return static_cast<unsigned int>(pixels / scale);
  };
  const unsigned int width = logical(work.right - work.left);
  const unsigned int height = logical(work.bottom - work.top);
  size.width = std::min(size.width, width * 9 / 10);
  size.height = std::min(size.height, height * 9 / 10);
  origin.x = logical(work.left) + (width - size.width) / 2;
  origin.y = logical(work.top) + (height - size.height) / 2;
}

int APIENTRY wWinMain(_In_ HINSTANCE instance, _In_opt_ HINSTANCE prev,
                      _In_ wchar_t *command_line, _In_ int show_command) {
  // Attach to console when present (e.g., 'flutter run') or create a
  // new console when running with a debugger.
  if (!::AttachConsole(ATTACH_PARENT_PROCESS) && ::IsDebuggerPresent()) {
    CreateAndAttachConsole();
  }

  // Initialize COM, so that it is available for use in the library and/or
  // plugins.
  ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

  flutter::DartProject project(L"data");

  std::vector<std::string> command_line_arguments =
      GetCommandLineArguments();

  project.set_dart_entrypoint_arguments(std::move(command_line_arguments));

  FlutterWindow window(project);
  Win32Window::Point origin(10, 10);
  Win32Window::Size size(1440, 900);
  PlaceOnPrimaryScreen(origin, size);
  if (!window.Create(L"megaplayer", origin, size)) {
    return EXIT_FAILURE;
  }
  window.SetQuitOnClose(true);

  ::MSG msg;
  while (::GetMessage(&msg, nullptr, 0, 0)) {
    ::TranslateMessage(&msg);
    ::DispatchMessage(&msg);
  }

  ::CoUninitialize();
  return EXIT_SUCCESS;
}
