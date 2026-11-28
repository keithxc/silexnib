# Third-Party Open-Source Licenses

Silex / SilexNib (desktop and mobile) uses the following third-party libraries
and frameworks. All application-level GPL dependencies have been excluded from
distributed builds.

---

## 1. Qt 6 (Desktop: macOS & Linux)

- **Components:** `QtCore`, `QtGui`, `QtWidgets`, `QtNetwork`, `QtImageFormats`,
  and `QtWayland` (Linux)
- **License:** GNU Lesser General Public License v3.0 (`LGPL-3.0-only`)
- **Copyright:** The Qt Company Ltd. and contributors
- **Linkage & Compliance Notice:**
  - Silex dynamically links against Qt 6 shared libraries (`Qt*.framework` on
    macOS; `libQt6*.so` on Linux).
  - Users are explicitly permitted to replace the bundled Qt 6 shared libraries
    and plugins with ABI-compatible versions.
  - Official Qt 6 source archives are available from:
    https://download.qt.io/official_releases/qt/
  - For Nix-built official releases, the exact Qt 6 version and patches match
    the pinned `NixOS/nixpkgs` revision recorded in the release build metadata.
  - Full LGPL-3.0 license text: https://www.gnu.org/licenses/lgpl-3.0.txt

---

## 2. PDFium (Desktop: Linux)

- **Component:** `libpdfium` (native PDF rendering, outline, and text search)
- **License:** `Apache-2.0` AND `BSD-3-Clause` AND `MIT`
- **Copyright:** The PDFium Authors / Google Inc.
- **Website:** https://pdfium.googlesource.com/pdfium/

---

## 3. md4c / md4c-html (Desktop: macOS & Linux)

- **Component:** `md4c` Markdown parser
- **License:** `MIT`
- **Copyright:** Martin Mitáš
- **Website:** https://github.com/mity/md4c

---

## 4. OpenSSL (Desktop: macOS & Linux)

- **Component:** `libssl`, `libcrypto` (mTLS 1.3 device-to-device vault sync)
- **License:** `Apache-2.0`
- **Copyright:** The OpenSSL Project Authors
- **Website:** https://www.openssl.org/

---

## 5. zlib (All Platforms)

- **Component:** `libz` (compression for `ILP1` and `.xopp` export)
- **License:** `Zlib`
- **Copyright:** Jean-loup Gailly and Mark Adler
- **Website:** https://zlib.net/

---

## 6. Platform System Frameworks

- **macOS:** Apple `AppKit`, `CoreGraphics`, `Foundation`, `PDFKit`, `QuartzCore`
- **iPadOS / iOS:** Apple `UIKit`, `Metal`, `QuartzCore`, `PDFKit`, `Network`,
  `Security`, `UniformTypeIdentifiers`, `AVFoundation`
- **Linux Runtime Dependencies (dynamically linked, LGPL/MIT/BSD/Apache):**
  standard system libraries (`glibc`, `libstdc++`, `Wayland`, `XKB`, `FreeType`,
  `HarfBuzz`, `Fontconfig`, `ICU`, `libjpeg-turbo`, `libpng`, `OpenJPEG`,
  `Little CMS 2`, `GLib`, `DBus`, `OpenGL/EGL/Mesa`)
