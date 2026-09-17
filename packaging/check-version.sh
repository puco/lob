#!/usr/bin/env bash
# The version is declared in four places that cannot reference each other:
# CMake, the AppStream metainfo, the AUR PKGBUILD and the Fedora spec. CMake is
# the source of truth; this fails the build when the others drift out of step.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

cmake_version=$(sed -nE 's/^project\(lob VERSION ([0-9.]+).*/\1/p' "$root/CMakeLists.txt")
metainfo_version=$(sed -nE 's/.*<release version="([0-9.]+)".*/\1/p' "$root/data/io.github.puco.lob.metainfo.xml" | head -1)
pkgbuild_version=$(sed -nE 's/^pkgver=(.+)/\1/p' "$root/packaging/aur/lob/PKGBUILD")
spec_version=$(sed -nE 's/^Version:[[:space:]]+(.+)/\1/p' "$root/packaging/fedora/lob.spec")

status=0
for pair in "metainfo:$metainfo_version" "PKGBUILD:$pkgbuild_version" "spec:$spec_version"; do
    name=${pair%%:*}
    value=${pair#*:}
    if [[ "$value" != "$cmake_version" ]]; then
        echo "version mismatch: CMakeLists.txt says '$cmake_version', $name says '$value'" >&2
        status=1
    fi
done

[[ $status -eq 0 ]] && echo "version $cmake_version consistent across CMake, metainfo, PKGBUILD and spec"
exit $status
