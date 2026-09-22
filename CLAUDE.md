# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Lob is a link router for KDE Plasma (Qt 6 / KDE Frameworks 6 / Kirigami, C++20 + QML). It registers as the `http`/`https` handler and sends each link to a browser or profile chosen by a rule, or asks through a layer-shell picker overlay. Wayland is the target platform.

## Commands

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$HOME/.local" -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure          # everything that runs without a display
ctest --test-dir build -R RuleEngineTest            # one test executable
build/bin/RuleEngineTest <testFunctionName>         # one QtTest function (ECM puts binaries in build/bin)
tests/run-under-compositor.sh --test-dir build --output-on-failure   # adds the layer-shell tests, inside a headless two-output sway
```

- The compositor tests look for `LOB_TEST_COMPOSITOR=1`, not `WAYLAND_DISPLAY`, so a plain `ctest` on a real desktop never grabs the keyboard. `LayerShellSmokeTest` reports "Skipped" rather than "Passed" when it did not run.
- `LauncherIntegrationTest` and `AppSmokeTest` run under `dbus-run-session` with `tests/dbus-session.conf`. `AppSmokeTest` drives the real `lob` binary, and `LaunchRecorder` stands in for a browser.
- Other tests use the `offscreen` QPA platform.

CI (`.github/workflows/ci.yml`, run in an Arch container) also runs these checks, and they are easy to break:

```bash
./packaging/check-version.sh                  # version in CMakeLists.txt matches metainfo, PKGBUILD and Fedora spec
./Messages.sh && git diff --exit-code po/lob.pot   # rerun Messages.sh after adding or changing any i18n() string (C++ or QML)
msgfmt -c --check-format -o /dev/null po/de/lob.po # a catalogue that drops a %N placeholder fails the build
```

When a PKGBUILD changes, regenerate its `.SRCINFO` with `makepkg --printsrcinfo > .SRCINFO`. CI diffs the two. The release steps are in `RELEASING.md`.

For debugging, use `QT_LOGGING_RULES="lob.*=true"` (categories `lob.controller`, `lob.picker`, `lob.default`, …). On Arch, Qt logs to the journal, so read it with `journalctl --user -f`. `LOB_NO_LAYERSHELL=1` runs the picker as an ordinary window. Use `lob --list` and `lob --explain <url>` to inspect what discovery and the rules do.

## Architecture

There are three CMake targets, so tests link the same code the app runs:

- **`lobcore`** (`src/core/`, static): Qt/KF logic with no QML. It holds URL handling, rules, target discovery and launching.
- **`lobui`** (`src/ui/`, static): the QObject controllers and models that QML binds to (`PickerController`, `SettingsController`, `TargetModel`, `RuleModel`). It links LayerShellQt.
- **`lob`** (`src/main.cpp`, `src/app/`, and `qml/Picker.qml` and `qml/Settings.qml` as the QML module `io.github.puco.lob`).

### Process model

`main.cpp` answers the diagnostic flags (`--version`, `--help`, `--list`, `--status`, `--set-default`, `--restore-default`, `--explain`, `--forget`) with only a `QCoreApplication`, before any GUI or D-Bus exists. Everything else registers a `KDBusService::Unique`. If a daemon (`lob --daemon`, started by `lob.service`) already owns the name, the new process forwards its argv or URLs to that daemon and exits. So after rebuilding, restart the daemon, or the old binary keeps answering.

The app id `io.github.puco.lob` is load-bearing. The D-Bus name comes from the reversed `organizationDomain` (`puco.github.io`) plus `applicationName`, and it must equal the `.desktop` basename, or D-Bus activation silently falls back to `Exec=`. `KAboutData` overwrites these values, so they are set twice in `main.cpp`. Keep both in sync.

### Routing pipeline

This is `Controller::prepare` → `Controller::route`. `runExplain` in `main.cpp` repeats it and has to stay consistent with it.

1. `UrlSanitizer::isRoutable`: only `http`/`https` are accepted, and URLs with credentials are refused.
2. `RedirectUnwrapper::unwrap` produces a `Link`. `destination` is what rules and the picker see. `toOpen` is what the browser receives, and it differs only for link scanners such as SafeLinks. `wrapper` is the `via` host. This step makes no network request. Shorteners are the exception: `ShortenerResolver` sends a HEAD request, and only when `resolveShorteners` is on.
3. `UrlSanitizer::strip` removes tracking parameters.
4. `RuleEngine::decide` returns a `Decision`. Explicit rules always beat remembered ones (`Rule::remembered`), whatever the file order. Remembered rules are kept narrowest-first.
5. `PickerController::showHold` (a countdown that any key interrupts) or `showPicker`. Then `Launcher` starts the target.

Each queued URL carries its own XDG activation token. Tokens are single-use, and `Launcher` mints a fresh one from the picker window so the browser gets focus.

### Key pieces

- **`RuleStore`**: `~/.config/lob/rules.json`, strict JSON, watched for external edits. It is the one source of truth for the daemon, the picker and the settings window. A malformed file keeps the last good config and blocks saves. Concurrent writes are refused, not overwritten. Unknown fields survive in `Rule::extensions`. Regex rules must be `compile()`d by whoever builds them.
- **`TargetRegistry` / `BrowserProfiles`**: find `x-scheme-handler/https` handlers through KService, then expand Gecko and Chromium profiles. Target ids are `<desktop-id>` or `<desktop-id>#<profileKey>`. Non-browsers are `OtherHandler` and stay hidden until listed in `enabledOtherHandlers`. `withoutRedundantProfiles` removes duplicates from the picker, but every id stays routable.
- **`Launcher`**: when there is no profile, it launches the desktop file through KIO. For a profile or a private window, it builds an argv (`buildArgv`). `execIsSafe`/`commandIsSafe` refuse shells and interpreters, including through `env`/`flatpak run` wrappers and symlink hops. Private mode never falls back to a normal window.
- **`DefaultBrowserManager`**: claims and restores the http/https handler, and records the previous handler so it can be restored.
- **`PickerController`**: a `Mode` state machine (Idle/Picker/Hold/Launching). It holds an exclusive layer-shell keyboard grab, and a 60s watchdog always takes the grab down.

## Conventions

- Comments explain *why*, often at length. Keep that density and don't strip existing rationale.
- User-visible strings go through `i18n*()` in both C++ and QML. The desktop entry and metainfo in `data/` carry their own translations and are not in the catalogue.
- Never log URLs. They can carry identity, for example signed SSO login hints. Log hosts at most.
- Any change to installed files has to keep `packaging/fedora/lob.spec` `%files` and the PKGBUILDs working, since CI builds both packages.
