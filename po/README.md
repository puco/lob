# Translations

`po/lob.pot` holds every translatable string in Lob, extracted from the C++ and
QML sources by [`Messages.sh`](../Messages.sh) at the repository root. Run that
script after adding or changing a string; CI checks that the committed template
matches the sources.

## What ships, and how good it is

German, Italian and French ship. **None of them has been reviewed by a native
speaker.** They were produced with machine assistance, and each catalogue says
so in its own `Last-Translator` header rather than claiming an author who does
not exist.

That matters more here than the string count suggests. Most of this interface
is terms of art -- routing, the hold bar, remembering a choice for a host, a
target, a scope -- and those are exactly the phrases an unreviewed translation
gets subtly wrong. If something reads oddly in your language, it probably is
odd: please open an issue or a pull request. A correction from someone who
speaks the language is worth more than the whole catalogue it lands in.

Submitting these through the KDE localization teams, who review properly,
remains the better long-term home for them.

## Adding a language

```bash
msginit --input=po/lob.pot --locale=de --output=po/de/lob.po
```

Translate `po/de/lob.po`, then build as usual -- `ki18n_install` compiles it and
installs it where `KLocalizedString` will find it. Nothing else needs changing:
the application domain is `lob` and the QML side reads the same catalogue.

## Keeping a language current

```bash
./Messages.sh
msgmerge --update po/de/lob.po po/lob.pot
```

## Notes

- The template carries no `POT-Creation-Date`. It is dropped deliberately so the
  file changes only when the messages do, which is what makes the CI check
  meaningful rather than noise.
- The Fedora spec uses `%find_lang %{name}` and `-f %{name}.lang`, so a new
  language needs no packaging change at all -- the file list is generated from
  what was installed.
- CI runs `msgfmt -c --check-format` over every catalogue. A translation that
  drops a placeholder (`%2` from `"Could not launch %1 (error %2)."`) is a
  runtime bug in that language only, and this is what catches it.
- Strings in `data/*.desktop.in` and `data/*.metainfo.xml` are translated
  through their own mechanisms (`Name[de]=` entries and `xml:lang` attributes)
  rather than through this catalogue.
