// Minimal SDL3 program for issue #423: does a maximized window keep the frame of the old DPI
// after the display scale changes? Windows only.
//
//   repro [none|remaximize|restore|placement] [seconds]
//
// It opens a resizable window maximized and logs, after every display scale change, the window
// rectangle Windows has against the one a maximized window has at the current DPI (the work area
// grown by the resize border on each side, the way Windows places it). The argument picks what is
// done on SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED for a maximized window: nothing, SDL_MaximizeWindow
// again, SDL_RestoreWindow followed by SDL_MaximizeWindow, or SetWindowPlacement with its own
// placement shown maximized. It quits after the given seconds
// (default 120) or when the window is closed, and prints how many checks matched.
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
  MODE_NONE,
  MODE_REMAXIMIZE,
  MODE_RESTORE,
  MODE_PLACEMENT
} Mode;
static const char *const ModeNames[] = {"none", "remaximize", "restore", "placement"};

static HWND hwndOf(SDL_Window *window) {
  return (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
}

// The rectangle Windows gives a maximized captioned window at this DPI: the monitor's work area
// with the resize border and padding hanging off each side.
static RECT expectedMaximized(HWND hwnd) {
  const UINT dpi = GetDpiForWindow(hwnd);
  MONITORINFO info = {sizeof(info)};
  GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &info);
  const int border = GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
  RECT rect = info.rcWork;
  InflateRect(&rect, border, border);
  return rect;
}

static int g_checks = 0;
static int g_matches = 0;

static void check(SDL_Window *window, const char *when) {
  HWND hwnd = hwndOf(window);
  RECT actual;
  GetWindowRect(hwnd, &actual);
  const RECT expected = expectedMaximized(hwnd);
  const bool maximized = (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0;
  const bool zoomed = IsZoomed(hwnd) != 0;
  const bool match = memcmp(&actual, &expected, sizeof(RECT)) == 0;
  int pw = 0;
  int ph = 0;
  SDL_GetWindowSizeInPixels(window, &pw, &ph);
  ++g_checks;
  if (match) {
    ++g_matches;
  }
  printf("[%s] dpi %u scale %.2f sdl-maximized %d zoomed %d pixels %dx%d\n"
         "    window (%d,%d)-(%d,%d)  expected (%d,%d)-(%d,%d)  %s\n",
         when, GetDpiForWindow(hwnd), SDL_GetWindowDisplayScale(window), maximized, zoomed, pw, ph, actual.left,
         actual.top, actual.right, actual.bottom, expected.left, expected.top, expected.right, expected.bottom,
         match ? "OK" : (maximized ? "STALE" : "not maximized"));
  fflush(stdout);
}

int main(int argc, char **argv) {
  Mode mode = MODE_NONE;
  if (argc > 1 && strcmp(argv[1], "remaximize") == 0) {
    mode = MODE_REMAXIMIZE;
  } else if (argc > 1 && strcmp(argv[1], "restore") == 0) {
    mode = MODE_RESTORE;
  } else if (argc > 1 && strcmp(argv[1], "placement") == 0) {
    mode = MODE_PLACEMENT;
  }
  const Uint64 seconds = argc > 2 ? (Uint64)atoi(argv[2]) : 120;

  if (!SDL_Init(SDL_INIT_VIDEO)) {
    fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
    return 1;
  }
  printf("SDL %d.%d.%d, mode %s\n", SDL_VERSIONNUM_MAJOR(SDL_GetVersion()), SDL_VERSIONNUM_MINOR(SDL_GetVersion()),
         SDL_VERSIONNUM_MICRO(SDL_GetVersion()), ModeNames[mode]);
  SDL_Window *window = SDL_CreateWindow("sdl-maximized-dpi-repro", 1280, 720,
                                        SDL_WINDOW_RESIZABLE | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_HIGH_PIXEL_DENSITY);
  if (window == NULL) {
    fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
    return 1;
  }
  SDL_Renderer *renderer = SDL_CreateRenderer(window, NULL);
  SDL_SyncWindow(window);
  check(window, "start");

  const Uint64 end = SDL_GetTicks() + seconds * 1000;
  bool running = true;
  while (running && SDL_GetTicks() < end) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        running = false;
      } else if (event.type == SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED) {
        check(window, "scale changed");
        if ((SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0 && mode != MODE_NONE) {
          if (mode == MODE_PLACEMENT) {
            WINDOWPLACEMENT placement = {sizeof(placement)};
            GetWindowPlacement(hwndOf(window), &placement);
            placement.showCmd = SW_SHOWMAXIMIZED;
            SetWindowPlacement(hwndOf(window), &placement);
          } else {
            if (mode == MODE_RESTORE) {
              SDL_RestoreWindow(window);
            }
            SDL_MaximizeWindow(window);
          }
          SDL_SyncWindow(window);
          check(window, ModeNames[mode]);
        }
      }
    }
    // A frame drawn to the edges shows which part of the screen the client area covers.
    SDL_SetRenderDrawColor(renderer, 40, 90, 160, 255);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);
    SDL_Delay(16);
  }
  check(window, "end");
  printf("%d of %d checks matched\n", g_matches, g_checks);
  SDL_DestroyRenderer(renderer);
  SDL_DestroyWindow(window);
  SDL_Quit();
  return 0;
}
