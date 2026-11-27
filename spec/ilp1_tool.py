#!/usr/bin/env python3
"""
Minimal zero-dependency Python 3 reference parser & inspector for the open
SilexNib ILP1 (*.ilp) and Silex Notebook (*.snb/) handwriting formats.

License: CC0 1.0 Universal (Public Domain) — free to copy, modify, and embed
in any open-source or commercial application for format interoperability.
"""

from __future__ import annotations

import dataclasses
import json
import struct
import sys
from pathlib import Path
from typing import List, Tuple

PAPER_STYLES = {0: "Grid", 1: "Ruled", 2: "Dotted", 3: "Blank"}
HEADER_FMT = "<4sIffIIIIII"
HEADER_SIZE = struct.calcsize(HEADER_FMT)  # 40 bytes
STROKE_HDR_FMT = "<QQQIfIffQI"
STROKE_HDR_SIZE = struct.calcsize(STROKE_HDR_FMT)  # 56 bytes
POINT_FMT = "<fff"
POINT_SIZE = struct.calcsize(POINT_FMT)  # 12 bytes
TEXT_HDR_FMT = "<QQQffffII"
TEXT_HDR_SIZE = struct.calcsize(TEXT_HDR_FMT)  # 48 bytes
TOMB_FMT = "<QQQ"
TOMB_SIZE = struct.calcsize(TOMB_FMT)  # 24 bytes


@dataclasses.dataclass
class InkStroke:
    id_high: int
    id_low: int
    created_hlc_ms: int
    argb_color: int
    base_width_pt: float
    flags: int
    offset_x: float
    offset_y: float
    offset_updated_hlc_ms: int
    points: List[Tuple[float, float, float]]


@dataclasses.dataclass
class InkTextBlock:
    id_high: int
    id_low: int
    updated_hlc_ms: int
    x: float
    y: float
    width: float
    font_size_pt: float
    argb_color: int
    text: str


@dataclasses.dataclass
class InkTombstone:
    target_id_high: int
    target_id_low: int
    deleted_hlc_ms: int


@dataclasses.dataclass
class InkLeafPage:
    page_width_pt: float = 420.0
    page_height_pt: float = 595.0
    paper_style: int = 0
    bg_argb: int = 0xFFFAF8F3
    strokes: List[InkStroke] = dataclasses.field(default_factory=list)
    text_blocks: List[InkTextBlock] = dataclasses.field(default_factory=list)
    tombstones: List[InkTombstone] = dataclasses.field(default_factory=list)

    @classmethod
    def from_bytes(cls, data: bytes) -> "InkLeafPage":
        if len(data) < HEADER_SIZE:
            raise ValueError("Data shorter than 40-byte ILP1 header")
        magic, version, w, h, style, bg, s_cnt, t_cnt, m_cnt, _ = struct.unpack_from(HEADER_FMT, data, 0)
        if magic != b"ILP1" or version != 1:
            raise ValueError(f"Unsupported ILP header: magic={magic!r}, version={version}")
        pos = HEADER_SIZE

        strokes: List[InkStroke] = []
        for _ in range(s_cnt):
            id_hi, id_lo, hlc, col, bw, flags, ox, oy, o_hlc, p_cnt = struct.unpack_from(STROKE_HDR_FMT, data, pos)
            pos += STROKE_HDR_SIZE
            pts = [struct.unpack_from(POINT_FMT, data, pos + i * POINT_SIZE) for i in range(p_cnt)]
            pos += p_cnt * POINT_SIZE
            strokes.append(InkStroke(id_hi, id_lo, hlc, col, bw, flags, ox, oy, o_hlc, pts))

        text_blocks: List[InkTextBlock] = []
        for _ in range(t_cnt):
            id_hi, id_lo, hlc, x, y, width, fsize, col, utf8_len = struct.unpack_from(TEXT_HDR_FMT, data, pos)
            pos += TEXT_HDR_SIZE
            text = data[pos : pos + utf8_len].decode("utf-8", errors="replace")
            pos += utf8_len
            text_blocks.append(InkTextBlock(id_hi, id_lo, hlc, x, y, width, fsize, col, text))

        tombstones: List[InkTombstone] = []
        for _ in range(m_cnt):
            t_hi, t_lo, d_hlc = struct.unpack_from(TOMB_FMT, data, pos)
            pos += TOMB_SIZE
            tombstones.append(InkTombstone(t_hi, t_lo, d_hlc))

        return cls(w, h, style, bg, strokes, text_blocks, tombstones)

    def to_bytes(self) -> bytes:
        chunks = [
            struct.pack(
                HEADER_FMT,
                b"ILP1",
                1,
                self.page_width_pt,
                self.page_height_pt,
                self.paper_style,
                self.bg_argb,
                len(self.strokes),
                len(self.text_blocks),
                len(self.tombstones),
                0,
            )
        ]
        for s in self.strokes:
            chunks.append(
                struct.pack(
                    STROKE_HDR_FMT,
                    s.id_high,
                    s.id_low,
                    s.created_hlc_ms,
                    s.argb_color,
                    s.base_width_pt,
                    s.flags,
                    s.offset_x,
                    s.offset_y,
                    s.offset_updated_hlc_ms,
                    len(s.points),
                )
            )
            for x, y, p in s.points:
                chunks.append(struct.pack(POINT_FMT, x, y, p))
        for tb in self.text_blocks:
            raw = tb.text.encode("utf-8")
            chunks.append(
                struct.pack(
                    TEXT_HDR_FMT,
                    tb.id_high,
                    tb.id_low,
                    tb.updated_hlc_ms,
                    tb.x,
                    tb.y,
                    tb.width,
                    tb.font_size_pt,
                    tb.argb_color,
                    len(raw),
                )
            )
            chunks.append(raw)
        for tm in self.tombstones:
            chunks.append(struct.pack(TOMB_FMT, tm.target_id_high, tm.target_id_low, tm.deleted_hlc_ms))
        return b"".join(chunks)


def inspect_target(path: Path) -> None:
    if path.is_dir() and (path / "notebook.json").is_file():
        manifest = json.loads((path / "notebook.json").read_text(encoding="utf-8"))
        print(f"Notebook: {manifest.get('title', path.stem)} (v{manifest.get('version', 1)})")
        pages = manifest.get("pages", [])
        print(f"Pages: {len(pages)}, Deleted: {len(manifest.get('deletedPages', []))}")
        for idx, p in enumerate(pages, 1):
            rel = p.get("file") or f"pages/{p['id']}.ilp"
            ilp = path / rel
            if ilp.is_file():
                page = InkLeafPage.from_bytes(ilp.read_bytes())
                style = PAPER_STYLES.get(page.paper_style, str(page.paper_style))
                print(
                    f"  [{idx}] {rel}: {page.page_width_pt:.0f}x{page.page_height_pt:.0f}pt {style} "
                    f"bg=0x{page.bg_argb:08X} strokes={len(page.strokes)} texts={len(page.text_blocks)} tombstones={len(page.tombstones)}"
                )
        return

    page = InkLeafPage.from_bytes(path.read_bytes())
    style = PAPER_STYLES.get(page.paper_style, str(page.paper_style))
    print(
        f"ILP1 {path.name}: {page.page_width_pt:.0f}x{page.page_height_pt:.0f}pt {style} "
        f"bg=0x{page.bg_argb:08X} strokes={len(page.strokes)} texts={len(page.text_blocks)} tombstones={len(page.tombstones)}"
    )


def _self_test() -> None:
    sample = InkLeafPage(
        strokes=[InkStroke(1, 2, 1795675200000, 0xFF1E293B, 2.2, 0, 0.0, 0.0, 0, [(10.0, 20.0, 0.7), (80.0, 45.0, 0.8)])],
        text_blocks=[InkTextBlock(3, 4, 1795675200001, 24.0, 60.0, 200.0, 15.0, 0xFF262420, "Hello SilexNib!")],
        tombstones=[InkTombstone(9, 10, 1795675200002)],
    )
    decoded = InkLeafPage.from_bytes(sample.to_bytes())
    assert decoded.strokes[0].points[1][0] == 80.0
    assert decoded.text_blocks[0].text == "Hello SilexNib!"
    assert decoded.tombstones[0].target_id_low == 10
    print("[PASS] ILP1 round-trip self-test succeeded.")


if __name__ == "__main__":
    if len(sys.argv) < 2 or sys.argv[1] == "--self-test":
        _self_test()
    else:
        inspect_target(Path(sys.argv[1]))
