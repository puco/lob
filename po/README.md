# Translations

`po/lob.pot` holds every translatable string in Lob, extracted from the C++ and
QML sources by [`Messages.sh`](../Messages.sh) at the repository root. Run that
script after adding or changing a string; CI checks that the committed template
matches the sources.

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
- Strings in `data/*.desktop.in` and `data/*.metainfo.xml` are translated
  through their own mechanisms (`Name[de]=` entries and `xml:lang` attributes)
  rather than through this catalogue.
