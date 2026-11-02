// Reads or sets the display scale of the primary monitor, the way the Settings app does, so the
// repro can be run without clicking through Settings. Windows only.
//
//   setdpi          print the current scale in percent
//   setdpi 150      set the scale to 150 %
//
// It uses the undocumented DisplayConfig request types -3 (get) and -4 (set). They take the scale
// as a step relative to the monitor's recommended one, over the list of steps Settings offers.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>

#define GET_DPI_SCALE ((DISPLAYCONFIG_DEVICE_INFO_TYPE)(-3))
#define SET_DPI_SCALE ((DISPLAYCONFIG_DEVICE_INFO_TYPE)(-4))

typedef struct {
  DISPLAYCONFIG_DEVICE_INFO_HEADER header;
  INT32 minRelative;
  INT32 currentRelative;
  INT32 maxRelative;
} DpiScaleGet;

typedef struct {
  DISPLAYCONFIG_DEVICE_INFO_HEADER header;
  INT32 relative;
} DpiScaleSet;

static const int Steps[] = {100, 125, 150, 175, 200, 225, 250, 300, 350, 400, 450, 500};
#define STEP_COUNT ((int)(sizeof(Steps) / sizeof(Steps[0])))

static int primarySource(LUID *adapter, UINT32 *source) {
  UINT32 pathCount = 0;
  UINT32 modeCount = 0;
  if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS) {
    return 0;
  }
  DISPLAYCONFIG_PATH_INFO *paths = calloc(pathCount, sizeof(*paths));
  DISPLAYCONFIG_MODE_INFO *modes = calloc(modeCount, sizeof(*modes));
  int found = 0;
  if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths, &modeCount, modes, NULL) == ERROR_SUCCESS) {
    for (UINT32 i = 0; i < pathCount && !found; ++i) {
      const UINT32 modeIndex = paths[i].sourceInfo.modeInfoIdx;
      // The primary monitor's source sits at the desktop origin.
      if (modeIndex < modeCount && modes[modeIndex].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE &&
          modes[modeIndex].sourceMode.position.x == 0 && modes[modeIndex].sourceMode.position.y == 0) {
        *adapter = paths[i].sourceInfo.adapterId;
        *source = paths[i].sourceInfo.id;
        found = 1;
      }
    }
  }
  free(paths);
  free(modes);
  return found;
}

int main(int argc, char **argv) {
  LUID adapter;
  UINT32 source;
  if (!primarySource(&adapter, &source)) {
    fprintf(stderr, "no primary display found\n");
    return 1;
  }
  DpiScaleGet get = {{GET_DPI_SCALE, sizeof(get), adapter, source}, 0, 0, 0};
  if (DisplayConfigGetDeviceInfo(&get.header) != ERROR_SUCCESS) {
    fprintf(stderr, "reading the scale failed\n");
    return 1;
  }
  const int recommended = -get.minRelative;
  if (argc < 2) {
    printf("%d\n", Steps[recommended + get.currentRelative]);
    return 0;
  }
  const int wanted = atoi(argv[1]);
  for (int i = 0; i < STEP_COUNT; ++i) {
    if (Steps[i] == wanted) {
      const int relative = i - recommended;
      if (relative < get.minRelative || relative > get.maxRelative) {
        fprintf(stderr, "%d %% is outside what this monitor offers\n", wanted);
        return 1;
      }
      DpiScaleSet set = {{SET_DPI_SCALE, sizeof(set), adapter, source}, relative};
      if (DisplayConfigSetDeviceInfo(&set.header) != ERROR_SUCCESS) {
        fprintf(stderr, "setting the scale failed\n");
        return 1;
      }
      return 0;
    }
  }
  fprintf(stderr, "%d %% is not a scale step\n", wanted);
  return 1;
}
