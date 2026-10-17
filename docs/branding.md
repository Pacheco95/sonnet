# Branding

The Sonnet icon is one vector master and seven PNG exports in `assets/branding/`. Every platform's icon is cut from those files by `tools/generate_icons.py`, and the outputs are committed, so a build needs no extra tool.

| File | Use |
|---|---|
| `sonnet-icon-master.svg` | the 1024 px vector master, layers kept apart; the editable `.ai` stays out of the repository |
| `icon-foreground.png`, `icon-background.png`, `icon-monochrome.png` | Android's adaptive layers; the S sits in the central 61% (the 66 dp safe zone), and the foreground and monochrome layers share size and centre because the launcher swaps one for the other |
| `icon-ios.png` | iOS, opaque, with no pre-applied corners |
| `icon-macos.png` | macOS tile with its own shadow |
| `icon-desktop.png` | Linux and Windows tile, transparent corners, no shadow |
| `icon-small.png` | the desktop tile with a simplified, flat S, for 16 to 48 px |

The glyph has no glow or shadow outside its shapes, on any platform except the macOS tile's own shadow: a halo turns into a smudge at 16 px and on a themed Android icon.

## Regenerating

```sh
python3 tools/generate_icons.py   # needs Pillow
```

Sizes up to 48 px are cut from `icon-small.png`, larger ones from `icon-desktop.png`. Running it twice leaves `git status` unchanged. It writes:

| Output | Platform |
|---|---|
| `packaging/linux/icons/hicolor/<n>x<n>/apps/sonnet.png` | Linux icon theme, 16 to 512 px |
| `apps/icon.ico` | Windows, 16 to 256 px |
| `apps/player/android/res/mipmap-<density>/ic_launcher_<layer>.png` | Android, 108 dp per density |
| `apps/player/ios/Assets.xcassets/AppIcon.appiconset` | iOS, the single 1024 px icon (Xcode derives the other sizes) |
| `modules/platform/src/WindowIconData.cpp` | the 256 px window icons, embedded in `platform` |

## Where each platform takes it from

- **Window (Linux, macOS).** `SdlWindow` decodes the embedded 256 px PNG with SDL's `SDL_LoadPNG_IO` and calls `SDL_SetWindowIcon`, so the editor and the player show it with no data file next to the binary. macOS uses the `icon-macos.png` cut, and the Dock shows it, since the executables are not app bundles there. On Linux the entry point also sets `SDL_HINT_APP_ID` to the executable's name with dashes (`sonnet_editor` is `sonnet-editor`), which Wayland and X11 match against the desktop entry.
- **Linux desktop entries.** `packaging/linux/sonnet-editor.desktop` and `sonnet-player.desktop`. The build has no install rules, so the files only ship. GNOME Shell shows the icon in Alt+Tab and the dock only for a window it can match to a desktop entry, and GLib ignores an entry whose `Exec` it cannot resolve, so `Exec` must be the binary's full path (or a name on `PATH`). From the repository root, after a build:

  ```sh
  mkdir -p ~/.local/share/applications ~/.local/share/icons
  cp -r packaging/linux/icons/hicolor ~/.local/share/icons/
  for app in editor player; do
    sed "s|^Exec=.*|Exec=$PWD/build/linux-debug/apps/$app/sonnet_$app|" \
      packaging/linux/sonnet-$app.desktop > ~/.local/share/applications/sonnet-$app.desktop
  done
  gtk-update-icon-cache -f -t ~/.local/share/icons/hicolor
  ```

  The shell matches the window by its class, `sonnet-editor` or `sonnet-player` (the entry's `StartupWMClass`); without a matching entry Alt+Tab shows a generic gear, whatever icon the window itself carries.
- **Windows.** `apps/icon.rc` has the icon as resource 1 and `sonnet_add_executable` adds it to every executable; SDL takes the window and taskbar icon from it and Explorer shows it for the `.exe`.
- **Android.** `res/mipmap-anydpi/ic_launcher.xml` is an adaptive icon with background, foreground and monochrome layers, referenced by `android:icon="@mipmap/ic_launcher"`. `minSdkVersion` is 36, so no legacy per-density icon is needed.
- **iOS.** `sonnet_add_ios_bundle` adds `Assets.xcassets` as a bundle resource and sets `ASSETCATALOG_COMPILER_APPICON_NAME` to `AppIcon`.

Not done: a per-game icon for exported games (the player carries the Sonnet icon), iOS dark and tinted variants, and a macOS `.app` with an `.icns`.
