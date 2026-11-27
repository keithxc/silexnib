# SilexNib Open Handwriting Format Specification (`ILP1` & `*.snb/`)

**Status**: Stable v1 (2026-11-27)  
**License**: [CC0 1.0 Universal (Public Domain)](../LICENSE-SPEC) — **third-party note-taking, handwriting, and sync apps are warmly welcome to read, write, convert, and interoperate with `ILP1` and `*.snb/` with zero licensing restrictions.**

---

## 1. Design Philosophy

1. **Physical Page-by-Page Notebooks (Not an Infinite Canvas)**  
   Every page has an explicit physical paper size (`A5`, `B5`, `A4`, `iPad 3:4`, or custom `widthPt × heightPt`) and paper ruling (`Grid`, `Ruled`, `Dotted`, `Blank`), matching real notebooks and printing cleanly to vector PDF.
2. **Small Per-Page Files for Fast, Conflict-Free Sync**  
   A notebook is a plain directory (`MyNotebook.snb/`). Each page is a compact standalone `*.ilp` binary file (typically `15–50 KB`). Editing page 18 only writes and syncs page 18 — never rewriting the whole notebook.
3. **Zero Vendor Lock-In**  
   No proprietary encryption, no opaque platform-locked blobs (such as Apple `PKDrawing`), and full support for deterministic Hybrid Logical Clock (HLC) + Tombstone merging plus standard exports to **multi-page vector PDF (`.pdf`)** and **Xournal++ (`.xopp`)**.

---

## 2. Multi-Page Notebook Container: `*.snb/`

On disk, a notebook is a regular folder ending with `.snb`:

```text
2026_Planner.snb/
├── notebook.json              # Human-readable UTF-8 JSON manifest & page order
└── pages/
    ├── p_019ab3f0a1.ilp       # Page 1 (ILP1 little-endian binary)
    ├── p_019ab3f0b2.ilp       # Page 2
    └── ...
```

Standalone single-page `*.ilp` files (for quick sketches or images embedded inside a note's `./<note_stem>.assets/` directory) are also first-class.

### 2.1 `notebook.json` Schema (v1)

```json
{
  "format": "silex-notebook",
  "version": 1,
  "id": "019ab3f0-7c21-7a4b-9e10-8a1f3c9d0001",
  "title": "2026 Planner",
  "coverColor": "#8C6F52",
  "defaultPreset": "A5",
  "defaultPaperStyle": "grid",
  "defaultBgArgb": "0xFFFAF8F3",
  "updatedHlcMs": 1795675200000,
  "pages": [
    {
      "id": "p_019ab3f0a1",
      "file": "pages/p_019ab3f0a1.ilp",
      "orderKey": "a0",
      "createdHlcMs": 1795675200000,
      "orderUpdatedHlcMs": 1795675200000
    }
  ],
  "deletedPages": [
    {
      "id": "p_019ab3e900",
      "deletedHlcMs": 1795675100000
    }
  ]
}
```

### 2.2 Standard Paper Presets (PDF Points, `72 pt = 1 inch`)

| Preset | Portrait `W × H` (pt) | Physical Size | Typical Use |
| --- | --- | --- | --- |
| `A5` (default) | `420.0 × 595.0` | `148 × 210 mm` | Everyday planner, fits iPad mini portrait |
| `B5` | `499.0 × 709.0` | `176 × 250 mm` | Study notebook / engineering pad |
| `A4` | `595.0 × 842.0` | `210 × 297 mm` | Standard printable document / sheet |
| `iPad3x4` | `768.0 × 1024.0` | `3:4` ratio | Full-screen iPad sketch page |

Landscape pages simply swap width and height; mixed page sizes inside the same `.snb/` bundle are valid because every `.ilp` page stores its own exact `pageWidthPt` and `pageHeightPt`.

### 2.3 Deterministic `notebook.json` Merge Algorithm

When two devices concurrently edit `notebook.json`:
1. Take the union of `deletedPages` keyed by `id`, keeping the highest `deletedHlcMs`.
2. Take the union of `pages` keyed by `id`, excluding entries present in `deletedPages` where `deletedHlcMs >= createdHlcMs`. For matching `id`s, keep the `orderKey` with the higher `orderUpdatedHlcMs`.
3. Sort surviving pages deterministically by `(orderKey, createdHlcMs, id)` and scan `pages/*.ilp` to recover any unreferenced orphan pages.
4. Write atomically via a `*.tmp` temporary file (`write -> fsync -> rename`).

---

## 3. Single-Page Binary Format: `ILP1` (`*.ilp`)

All multi-byte fields are **Little-Endian** (`<`) with IEEE-754 32-bit floats (`float32`).

### 3.1 File Header (`40 Bytes`)

| Offset | Size | Type | Field | Notes |
| --- | --- | --- | --- | --- |
| `0` | 4 B | ASCII | `magic` | Exact bytes `"ILP1"` (`49 4C 50 31`) |
| `4` | 4 B | `uint32` | `version` | `1` |
| `8` | 4 B | `float32` | `pageWidthPt` | Page width in PDF points (`> 0`) |
| `12` | 4 B | `float32` | `pageHeightPt` | Page height in PDF points (`> 0`) |
| `16` | 4 B | `uint32` | `paperStyle` | `0 = Grid`, `1 = Ruled`, `2 = Dotted`, `3 = Blank` |
| `20` | 4 B | `uint32` | `bgArgb` | Background color `0xAARRGGBB` (e.g. `0xFFFAF8F3` warm paper, `0xFFFFFFFF` white) |
| `24` | 4 B | `uint32` | `strokeCount` | Number of `Stroke` records following the header |
| `28` | 4 B | `uint32` | `textBlockCount` | Number of `TextBlock` records following `Strokes` |
| `32` | 4 B | `uint32` | `tombstoneCount` | Number of `Tombstone` records following `TextBlocks` |
| `36` | 4 B | `uint32` | `reserved` | `0` (must be ignored on read by v1 parsers) |

### 3.2 `Stroke` Record (repeated `strokeCount` times)

| Type | Field | Notes |
| --- | --- | --- |
| `uint64` | `idHigh` | High 64 bits of 128-bit unique stroke ID |
| `uint64` | `idLow` | Low 64 bits of 128-bit unique stroke ID |
| `uint64` | `createdHlcMs` | Creation HLC timestamp in milliseconds (defines deterministic z-order) |
| `uint32` | `argbColor` | `0xAARRGGBB` ink color |
| `float32` | `baseWidthPt` | Base stroke width in PDF points |
| `uint32` | `flags` | Bitfield: `bit 0` (`0x1`) = Highlighter (`0` = Pen); higher bits reserved for brush variants |
| `float32` | `offsetX` | Translation offset X in pt (for lasso/select move, default `0.0`) |
| `float32` | `offsetY` | Translation offset Y in pt (default `0.0`) |
| `uint64` | `offsetUpdatedHlcMs` | LWW HLC timestamp for `offsetX` / `offsetY` / `argbColor` updates |
| `uint32` | `pointCount` | Number of points in this stroke |
| `Point[pointCount]` | `points` | Each point is 12 bytes: `float32 x`, `float32 y`, `float32 pressure` (`0.0..1.0+`) |

Final rendered coordinates for a point are `(point.x + offsetX, point.y + offsetY)`.

### 3.3 `TextBlock` Record (repeated `textBlockCount` times)

| Type | Field | Notes |
| --- | --- | --- |
| `uint64` | `idHigh` | High 64 bits of 128-bit unique text block ID |
| `uint64` | `idLow` | Low 64 bits of 128-bit unique text block ID |
| `uint64` | `updatedHlcMs` | Last-Write-Wins HLC timestamp in milliseconds |
| `float32` | `x` | Top-left X in pt |
| `float32` | `y` | Top-left Y in pt |
| `float32` | `width` | Wrapping box width in pt |
| `float32` | `fontSizePt` | Font size in pt (default `15.0`) |
| `uint32` | `argbColor` | Text color `0xAARRGGBB` |
| `uint32` | `utf8Len` | Byte length of UTF-8 text payload |
| `uint8[utf8Len]` | `text` | Unterminated UTF-8 string (indexed for full-text vault search) |

### 3.4 `Tombstone` Record (repeated `tombstoneCount` times, `24 Bytes` each)

| Type | Field | Notes |
| --- | --- | --- |
| `uint64` | `targetIdHigh` | High 64 bits of deleted stroke or text block ID |
| `uint64` | `targetIdLow` | Low 64 bits of deleted stroke or text block ID |
| `uint64` | `deletedHlcMs` | Deletion HLC timestamp in milliseconds |

### 3.5 Lossless Concurrent Page Merge (`ILP1` CRDT / Set-Union)

When merging local page `A` and remote page `B` (for example after concurrent offline edits on two devices):
1. **Tombstones**: Union by `(targetIdHigh, targetIdLow)`, keeping `max(deletedHlcMs)`.
2. **Strokes**: Union by `(idHigh, idLow)`, removing any stroke present in `Tombstones`. For identical IDs, keep the record with the larger `offsetUpdatedHlcMs`. Sort deterministically by `(createdHlcMs, idHigh, idLow)`.
3. **TextBlocks**: Union by `(idHigh, idLow)`, removing any block with a tombstone where `deletedHlcMs >= updatedHlcMs`. For identical IDs, keep the record with the larger `updatedHlcMs`.
4. **Atomic Write**: Write merged binary to `*.tmp`, `fsync`, and atomically `rename` into place.

---

## 4. Interoperability Exports (`.pdf` & `.xopp`)

- **Vector PDF (`.pdf`)**: Exact per-page dimensions (`pageWidthPt × pageHeightPt`), vector ruling lines, variable-width pen strokes, translucent multiply highlighters, and selectable/searchable UTF-8 text layers for `TextBlocks`.
- **Xournal++ (`.xopp`)**: Standard gzip-compressed XML (`fileversion="4"`), mapping `*.snb/` pages to `<page>`, `paperStyle` + `bgArgb` to `<background style="graph|ruled|dotted|plain"/>`, strokes (with `offsetX`/`offsetY` applied) to `<stroke tool="pen|highlighter"/>`, and text blocks to `<text>`.

---

## 5. Reference Implementation

See [`ilp1_tool.py`](./ilp1_tool.py) in this directory for a zero-dependency Python 3 parser, inspector, and sample generator released under CC0 1.0 Public Domain.
