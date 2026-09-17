#!/usr/bin/env bash
# Extracts every translatable string into po/lob.pot.
#
# Named and shaped the way KDE's translation infrastructure expects, so that if
# Lob is ever handled by scripty nothing here has to change. It also runs
# standalone -- the variables scripty would set have defaults -- because today
# nothing else is going to run it.
set -euo pipefail
cd "$(dirname "$0")"

podir=${podir:-po}
XGETTEXT=${XGETTEXT:-xgettext}

# The ki18n call families. Without these, xgettext sees ordinary function
# calls and extracts nothing at all.
keywords=(
    -ki18n:1 -ki18nc:1c,2 -ki18np:1,2 -ki18ncp:1c,2,3
    -ki18nd:2 -ki18ndc:2c,3 -ki18ndp:2,3 -ki18ndcp:2c,3,4
    -kxi18n:1 -kxi18nc:1c,2 -kxi18np:1,2 -kxi18ncp:1c,2,3
    -kI18N_NOOP:1 -kI18NC_NOOP:1c,2
)

common=(
    --from-code=UTF-8
    --add-comments=i18n
    --package-name=lob
    --copyright-holder="Lob contributors"
    --msgid-bugs-address="https://github.com/puco/lob/issues"
)

mkdir -p "$podir"

"$XGETTEXT" -C --kde "${keywords[@]}" "${common[@]}" \
    -o "$podir/lob.pot" \
    $(find src -name '*.cpp' -o -name '*.h' | sort)

# QML is read as JavaScript: C++ rules mis-handle its string literals, and the
# picker's strings are a third of the application's.
"$XGETTEXT" -L JavaScript "${keywords[@]}" "${common[@]}" \
    --join-existing \
    -o "$podir/lob.pot" \
    $(find qml -name '*.qml' | sort)

# POT-Creation-Date would otherwise change on every run, so the file would look
# modified whenever anyone regenerated it and a real change would be invisible
# in the diff. Dropping it makes the committed .pot change only when the
# messages do, which is what makes checking it in CI worth anything.
sed -i '/^"POT-Creation-Date:/d' "$podir/lob.pot"

echo "wrote $podir/lob.pot ($(grep -c '^msgid ' "$podir/lob.pot") messages)"
