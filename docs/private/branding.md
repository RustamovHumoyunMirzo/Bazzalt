# Branding assets

Canonical SVG sources are `bazzalt/assets/branding/BazzaltIcon.svg` (the shaped
symbol) and `BazzaltText.svg` (the BAZZALT wordmark). Change these files rather
than placing alternate logos in individual applications or the project root.

`bazzalt.branding.LogoIcon` supplies application/window icons and the editor's
leading menubar symbol. `WordmarkPixmap` supplies the Hub header. Both preserve the
original aspect ratio, keep transparent backgrounds and tint SVG fill/stroke
to the editor theme or native Hub palette. The wordmark is decorative, does not
participate in keyboard menu navigation, and has an accessible name.

`scripts/compile_resources.py` embeds both SVGs in the Hub and shared branding
packages for Nuitka, so installed applications do not need loose source assets.
Run it after changing branding. Release builds generate the Windows executable
ICO from the symbol; Linux release packaging copies it as the desktop icon.
CMake embeds the same symbol into native game intro pixels with its proportions
preserved. Qt/SVG is only needed when generating those pixels, not by the game.

## Editor window layout

The editor uses the standard native window title bar and caption controls on
Windows and other platforms. The themed menu remains in QMainWindow's normal
menu layout, using the shared theme's original padding and text size. There is
no detached caption overlay, custom frame, or additional client-area spacer.
