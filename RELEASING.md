# Releasing

The version lives in `CMakeLists.txt` and is mirrored into
`data/io.github.puco.lob.metainfo.xml` and `packaging/aur/lob/PKGBUILD`.
Nothing can reference CMake from those files, so `packaging/check-version.sh`
fails the build when they drift. CI runs it, and so does the release workflow
before it publishes anything.

## Cutting a release

1. Bump the version in the three places and confirm they agree:

   ```bash
   ./packaging/check-version.sh
   ```

   Add a matching `<release>` entry to the metainfo with today's date.
   Regenerate `packaging/aur/lob/.SRCINFO` with `makepkg --printsrcinfo`
   after updating the PKGBUILD. CI checks that both agree.

2. Commit, tag and push. The tag must match the declared version or the
   release workflow refuses to run:

   ```bash
   git commit -am "Release 0.1"
   git tag -a v0.1 -m "Lob 0.1"
   git push origin main --tags
   ```

3. The `Release` workflow then builds, runs the tests, produces
   `lob-<version>.tar.gz` with `git archive`, and publishes it with its
    sha256 in the release notes.

    Before publishing, it builds the exact archive through the checksummed
    PKGBUILD, runs `check()` and `package()`, and validates the packaged desktop
    entry, AppStream metadata, and license file. A package failure blocks release.

   The tarball is built by us rather than using GitHub's auto-generated
   archive, whose checksums are not guaranteed stable over time — if one
   changes, every AUR user's build fails at once.

## Updating the AUR

> Not submitted yet: AUR account registration was closed when 0.1 shipped.
> Both `lob` and `lob-git` are unclaimed and both build cleanly, so this
> becomes possible as soon as registration reopens. Until then the release
> asset is the install path, and the README says so.

The release attaches `aur-lob.tar.gz` containing a `PKGBUILD` and `.SRCINFO`
with the real checksum already filled in. To publish:

```bash
git clone ssh://aur@aur.archlinux.org/lob.git aur-lob
cd aur-lob
# copy in the PKGBUILD and .SRCINFO from the release asset
makepkg --printsrcinfo > .SRCINFO   # regenerate if you edited anything
git commit -am "Update to 0.1"
git push
```

`lob-git` only needs pushing when its `PKGBUILD` itself changes — it tracks
`main` and derives its version from `git describe`, so it needs no update per
release.

Before pushing either, build it cleanly:

```bash
cd packaging/aur/lob && makepkg -si --noconfirm
```

`.SRCINFO` must always match its `PKGBUILD`: the AUR reads the former, not the
latter, so a stale one ships the wrong metadata. CI diffs them on every push.

## Requirements

- An AUR account with your SSH key registered.
- Push access to `ssh://aur@aur.archlinux.org/lob.git` and `/lob-git.git`,
   which exist only once you have pushed an initial commit to them.

## Manual reliability checks

Automated tests cover persistence, target discovery, argument construction,
request lifecycles and isolated MIME associations. Before shipping, verify the
compositor/browser interactions on an actual Plasma session:

- Wayland: picker and hold-bar launches focus the browser, including an already
  running browser and a zero-duration hold. Repeat with `LOB_NO_LAYERSHELL=1`.
- Firefox and a Chromium browser: choose each profile, then repeat in private
  mode. Verify the requested account/profile and private window actually open.
- Flatpak: install two browsers, verify both appear, and launch their profiles.
- Rapid selection, Escape, failed launches and several queued links: each link
  has exactly one outcome and the next picker is not dismissed by stale work.
- Add/remove a browser or profile while the daemon runs, then refresh.
- Multi-monitor and scaling: picker appears on the active screen; unplugging a
  screen or locking/unlocking the session does not leave an input grab behind.
- Clipboard copy: content survives completion in daemon and one-shot modes.
- Claim/restore defaults with HTTP and HTTPS initially using different browsers.

For a local package check (without installing), supply a source archive named
`lob-<version>.tar.gz` whose top-level directory has the same name:

```bash
bash packaging/check-package.sh /path/to/lob-0.1.tar.gz packaging/aur/lob/PKGBUILD /tmp/lob-package-check
```
