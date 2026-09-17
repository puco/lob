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
lob --help                 # the same list, from the program itself
```

Start the daemon at login so the picker is instant rather than paying a QML
startup on the first click:

```bash
systemctl --user enable --now lob.service
```

### In the picker

`1`–`9` pick · `P` private window · `R` remember for this host · `C` copy
instead of opening · `F5` (or `Ctrl+R`) rescan browsers · `Esc` cancel · arrows
and `Enter` also work.

Holding **Shift** while clicking a link forces the picker even when a rule
would have matched. During the hold bar, any key or click does the same.
This gesture requires a nonzero hold duration; `--pick` also works with a zero hold.

Private mode never silently falls back to a normal window. Unsupported targets
are disabled, and a launch failure leaves the link in the picker for retry.
Escape can cancel while an activation token is pending; once the launch has
been dispatched, Lob waits for its result.
After copying, a one-shot process stays alive while it owns the clipboard so
the selection remains available. On Wayland, even a zero-hold copy briefly
presents a surface to obtain the focus required to own the selection.

Browser/profile changes are discovered automatically. `F5` in the picker
rescans immediately. Explicit profile IDs remain stable when other profiles are
added or removed; a plain desktop ID selects the browser's own default. Old
remembered desktop IDs are upgraded only when a single profile makes the
intended choice unambiguous; otherwise Lob asks again.

The picker never lists a browser and a profile that would do the same thing.
One profile shows as the browser ("Firefox"); several show as the profiles
themselves, since the browser's own entry opens whichever is default anyway.
Both IDs stay usable in rules either way, and `lob --list` shows every one of
them, marking those the picker leaves out.

**Pause Routing** uses the configured browser fallback, then the previous
browser for the URL's scheme. If neither is available, it shows the picker.

This works because a Wayland client can see which keys were already held when
it takes keyboard focus, which is the only moment the gesture is observable --
the modifier is held in the application that opened the link, not in Lob.

### Redirects

A link clicked in Slack, a search result or a mail client usually arrives as a
redirector's URL rather than the page it points at. Lob reads the destination
out of it before deciding anything, so the picker asks about the page, a rule
matches on it, and **remember for this host** records it — rather than recording
`slack.com`, which every link from Slack would then match.

Only redirectors that carry their destination in the URL are read, and reading
them costs nothing: no request is made, so the tracker never hears from Lob.
Known ones include Slack, Google and DuckDuckGo result links, Reddit, Facebook,
Instagram, YouTube, LinkedIn, Steam, VK, Tumblr and Outlook SafeLinks. The
picker shows the destination with a `via <host>` line, so a URL that is not the
one you clicked is never a surprise.

A workspace with single sign-on is the other shape this takes: Slack sends the
link through itself as the identity provider, with the destination inside a
signed login hint rather than in a plain parameter. Lob reads the claim to route
the link and opens the original, because that URL is what signs you in before
you land on the page. The hint is short-lived and carries your identity, which
is one more reason Lob never writes URLs to the log.

Link scanners are the exception to opening what was read. A SafeLinks URL exists
to be visited — that is where the scan happens — so Lob routes by the
destination but still hands the browser the scanner's URL. Everything else opens
as the destination, which leaves the click-tracker out of it entirely.

Shorteners (`t.co`, `bit.ly`, `lnkd.in`) keep their destination on their own
server, so there is nothing to read without asking them. By default those links
are routed as-is and the browser follows the redirect as before. Turning on

```json
"resolveShorteners": true
```

makes Lob ask instead: a cookieless `HEAD` request, at most 1.5s, before the
link is routed. The trade is real — the link waits for it, the shortener sees a
request from this machine before the browser makes one, and an unreachable or
slow shortener simply routes as it arrived. Off unless you want that.

Add a redirector Lob does not know with `redirectWrappers`, mapping
`host` or `host/path` to the query parameter holding the destination:

```json
"redirectWrappers": { "go.corp.example/out": "to" }
```

Set `"unwrapRedirects": false` to switch all of this off.

## Configuration

`~/.config/lob/rules.json`, watched, so edits apply without a restart. A
malformed or unreadable file leaves the last valid configuration active and
blocks saves until it is repaired, so a half-finished edit never costs you the
rules you already had. Deleting the file resets Lob to its defaults, which is
what deleting it is for. Save conflicts and write failures are reported rather
than overwriting an edit made elsewhere.

```json
{
  "version": 1,
  "holdMs": 600,
  "stripTracking": true,
  "fallbackTarget": "",
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
the bare domain as well as subdomains. `pathPrefix` carries host and path in
one pattern and needs both, so `github.com` is refused rather than accepted as
a rule that could never match -- write it as a `host` rule instead. `action` is
`open`, `ask` or `copy`. Target ids come from `lob --list`.

The file is strict JSON (no comments). `holdMs` is an integer from 0 to 60000;
zero skips the hold bar. An empty `fallbackTarget` means ask. An omitted
`trackingParameters` uses the built-in list, while `[]` disables all stripping.
Path-prefix and regex rules support `"caseSensitive": true`. Omission keeps
version 1's case-insensitive behavior; host comparisons are always insensitive.

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
