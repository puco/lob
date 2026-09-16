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

   The tarball is built by us rather than using GitHub's auto-generated
   archive, whose checksums are not guaranteed stable over time — if one
   changes, every AUR user's build fails at once.

## Updating the AUR

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
