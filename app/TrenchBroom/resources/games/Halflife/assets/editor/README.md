# Entity Icons

These original TrenchBroom editor icons are distributed under GPL-3.0-or-later,
like the project. The SVG files are the editable sources; the PNG files are the
32-unit, camera-facing entity models referenced by the bundled FGD files.

Regenerate a PNG with ImageMagick from this directory:

```sh
magick -background none -density 384 multi_manager.svg -resize 32x32 -strip PNG32:multi_manager.png
```

Use the same command for `env_shake` and `trigger_changetarget`.

The FGD editor bounds match the 32-unit icons. Capture the integrated scene with:

```powershell
& scripts/ui-theme-acceptance.ps1 -Targets goldsrc-entity-links
```
