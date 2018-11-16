#include <lib/system/logger.hpp>

#ifdef _WIN32
#include <windows.h>
#endif

extern thread_local bool trace = true;

namespace PrettyLogging {
void redrawTickSymbol() {
  static char pb[] = { '/', '-', '\\', '|' };
  static uint8_t lastShown = 0;

#ifdef _WIN32
  static bool cursorHidden = false;

  if (!cursorHidden) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    CONSOLE_CURSOR_INFO     cursorInfo;

    GetConsoleCursorInfo(out, &cursorInfo);
    cursorInfo.bVisible = false;
    SetConsoleCursorInfo(out, &cursorInfo);
    cursorHidden = true;
  }
#endif

  std::clog << '\b' << pb[lastShown];
  if (++lastShown == sizeof(pb)) lastShown = 0;
}

void drawTick() {
  redrawTickSymbol();
  std::clog.flush();
}

void drawTickProgress(const uint32_t curr, const uint32_t total, const uint32_t tickEvery) {
  if (!total) return;
  if (curr != total && curr != 1 && curr % tickEvery) return;

  const auto prc = (100 * curr / total);
  std::clog << "\b\b\b\b\b";
  redrawTickSymbol();
  std::clog << " " << prc << "%" << (prc < 10 ? "  " : (prc < 100 ? " " : ""));
  std::clog.flush();
}

void finish() {
  std::clog << std::endl;
}
}
