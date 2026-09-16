# Lob

A link router for KDE Plasma. Lob registers as the system handler for `http`
and `https` and decides where each link should go, instead of sending
everything to one default browser.

- Links matching a rule open immediately, after a short bar you can interrupt.
- Anything else shows a keyboard-driven picker of installed browsers and their
  profiles, with an option to remember the choice for that host.
- `http` and `https` only. Not mail links, not local files, not PDFs. URLs
  carrying credentials (`https://user:pw@host/`) are refused outright.

## Install

### Arch

The release ships a ready-to-build package, so no AUR account is involved:

```bash
curl -LO https://github.com/puco/lob/releases/latest/download/aur-lob.tar.gz
tar xzf aur-lob.tar.gz
makepkg -si
```

That PKGBUILD carries the release tarball's real checksum, so the download is
verified before anything is built.

> **Not on the AUR yet.** `lob` and `lob-git` are prepared and both build
> cleanly, but AUR account registration is closed at the time of writing. Once
> it reopens they will be submitted under those names and this becomes
> `paru -S lob`.

Then start the daemon and claim the link handler:

```bash
systemctl --user enable --now lob.service
lob --set-default
```

`lob --restore-default` puts your previous browser back at any time.

## Build from source

Needs Qt 6.6+, KDE Frameworks 6.19+, layer-shell-qt 6.6+ and
`extra-cmake-modules`. Dependencies may themselves require a newer Qt. On Arch:

```bash
sudo pacman -S --needed extra-cmake-modules cmake ninja qt6-base qt6-declarative qt6-svg \
    kirigami kirigami-addons ki18n kcoreaddons kconfig kdbusaddons knotifications \
    kwindowsystem kiconthemes kcolorscheme kcrash kstatusnotifieritem kservice kio \
    layer-shell-qt qqc2-desktop-style
```

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build build
cmake --install build
ctest --test-dir build
```

## Use

```bash
lob --list                 # discovered browsers, profiles and launch commands
lob --explain <url>        # which rule decides this URL, and which ones lost
lob --status               # who currently handles http/https
lob --set-default          # claim the handler (records what was there first)
lob --restore-default      # put the previous browser back
lob <url>                  # route one URL
lob --pick <url>           # route one URL, ignoring rules
```

Start the daemon at login so the picker is instant rather than paying a QML
startup on the first click:

```bash
systemctl --user enable --now lob.service
```

### In the picker

`1`–`9` pick · `P` private window · `R` remember for this host · `C` copy
instead of opening · `Esc` cancel · arrows and `Enter` also work.

Holding **Shift** while clicking a link forces the picker even when a rule
would have matched. During the hold bar, any key or click does the same.

This works because a Wayland client can see which keys were already held when
it takes keyboard focus, which is the only moment the gesture is observable --
the modifier is held in the application that opened the link, not in Lob.

## Configuration

`~/.config/lob/rules.json`, watched, so edits apply without a restart. A
malformed file is ignored rather than overwritten.

```jsonc
{
  "version": 1,
  "holdMs": 600,                     // 0 skips the hold bar entirely
  "stripTracking": true,
  "fallbackTarget": "",              // used when nothing matches; empty = ask
  "enabledOtherHandlers": ["chatgpt.desktop"],
  "rules": [
    { "match": "hostSuffix", "pattern": "corp.example",
      "action": "open", "target": "microsoft-edge.desktop" },

    { "match": "pathPrefix", "pattern": "github.com/anthropics",
      "action": "open", "target": "firefox.desktop#/home/you/.config/mozilla/firefox/xyz.work" },

    { "match": "host", "pattern": "bank.example", "action": "ask" },

    { "match": "regex", "pattern": "^https://.*\\.pdf$", "action": "copy" }
  ]
}
```

`match` is `host`, `hostSuffix`, `pathPrefix` or `regex`. `hostSuffix` covers
the bare domain as well as subdomains. `action` is `open`, `ask` or `copy`.
Target ids come from `lob --list`.

Explicit rules always win over remembered ones, wherever they sit in the file,
so a choice made in passing can never shadow one you wrote deliberately.

### Other handlers

Applications that register for `https` without being browsers — a chat app
catching OAuth callbacks, say — are discovered but hidden until listed in
`enabledOtherHandlers`. They are never filtered out of discovery: whether
routing a link to one is useful is your call.

## Notes

- Qt on Arch logs to the journal, so use `journalctl --user -f` rather than
  stderr. `QT_LOGGING_RULES="lob.*=true"` turns on the detail.
- `LOB_NO_LAYERSHELL=1` runs the picker as an ordinary window instead of a
  layer-shell overlay with an exclusive keyboard grab. Useful when iterating
  on the UI.
- If `~/.config/kde-mimeapps.list` sets an http/https default it outranks the
  registration Lob writes; `lob --status` warns when that is the case.

## Licence

MIT. See [LICENSE](LICENSE).

Lob links against Qt and KDE Frameworks, which are LGPL; dynamic linking from
MIT-licensed code is fine, and those libraries keep their own terms.

## How this was built

Lob was written in a single session with [Claude Code](https://claude.com/claude-code),
using Claude Opus 5. That is worth stating plainly rather than leaving for
someone to infer from the commit log.

What that meant in practice:

- The architecture was settled in conversation. The choices that shaped
  everything else — a resident daemon over per-click startup, Kirigami over
  QtWidgets, `http`/`https` only, what v1 would and would not include — were
  decisions I made from options put to me, not defaults it picked.
- Claude wrote the code, ran the builds and tests, and drove most of the
  verification itself: CLI output, D-Bus introspection, latency measurements,
  and screenshots of the running overlay.
- Several design errors were caught by that testing rather than by review.
  Classification originally required finding a browser's profile store, which
  demoted a never-launched Firefox to "not a browser". The tracking-parameter
  stripper skipped any URL whose query was *entirely* tracking — exactly the
  links most worth cleaning. Both are fixed; the commit messages explain why.
- The parts a compositor cannot be scripted into doing — window focus after a
  launch, the held-modifier gesture, clicking through the picker — were tested
  by hand, by me.

The commit messages are unusually detailed by intent. They record why each
non-obvious decision was made, since that reasoning is the part hardest to
recover later.
