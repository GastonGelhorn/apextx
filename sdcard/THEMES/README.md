# NB4 themes

**ApexTX Light** and **ApexTX Dark** are built into the firmware and work without a
storage card or separate installation. These directories contain reference
copies.

- **ApexTX Light** uses a light background, dark text, and control accents.
- **ApexTX Dark** uses a dark background and light text. Adjust radio brightness
  to the track lighting conditions.

## Installing a custom theme

To create an external theme, duplicate a directory, change `summary.name`, and
copy the complete directory to `/THEMES`. Built-in names are reserved, so
external copies do not appear twice in the selector.

```bash
cp -R MyTheme "/Volumes/NO NAME/THEMES/"
```

The required layout is `/THEMES/MyTheme/theme.yml`; standalone `.yml` files are
not discovered. Optional background and logo assets belong in the same
directory. Invalid files are skipped without opening dialogs during discovery.
Changing the theme changes only the palette; layout and orientation remain
independent preferences.
