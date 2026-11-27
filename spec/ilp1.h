/*
 * ilp1.h — Single-header pure C99 codec & CRDT merger for the open SilexNib ILP1 (*.ilp) format.
 *
 * License: CC0 1.0 Universal (Public Domain) — free to copy, embed, modify, and use
 *          in any open-source or commercial application with zero restrictions.
 * Format:  https://github.com/keithxc/silexnib/blob/main/spec/FORMAT.md
 *
 * Usage (stb single-header style):
 *   In ONE .c/.cpp file:      #define ILP1_IMPLEMENTATION
 *                             #include "ilp1.h"
 *   In all other files:       #include "ilp1.h"
 *
 * Optional macros before #include:
 *   ILP1_STATIC | ILP1_MALLOC(sz) | ILP1_CALLOC(n,sz) | ILP1_FREE(ptr)
 */

#ifndef ILP1_H
#define ILP1_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef ILP1_STATIC
#define ILP1_API static
#else
#define ILP1_API extern
#endif

#define ILP1_VERSION                1u
#define ILP1_HEADER_SIZE            40u
#define ILP1_STROKE_HEADER_SIZE     56u
#define ILP1_POINT_SIZE             12u
#define ILP1_TEXT_BLOCK_HEADER_SIZE 48u
#define ILP1_TOMBSTONE_SIZE         24u

#define ILP1_MAX_STROKES            500000u
#define ILP1_MAX_POINTS             500000u
#define ILP1_MAX_TEXT_BLOCKS        100000u
#define ILP1_MAX_TEXT_BYTES         (4u * 1024u * 1024u)
#define ILP1_MAX_TOMBSTONES         500000u
#define ILP1_STROKE_FLAG_HIGHLIGHTER 1u

typedef enum ilp1_paper_style {
    ILP1_PAPER_GRID = 0, ILP1_PAPER_RULED = 1, ILP1_PAPER_DOTTED = 2, ILP1_PAPER_BLANK = 3
} ilp1_paper_style;

typedef enum ilp1_status {
    ILP1_OK = 0,
    ILP1_ERR_INVALID_ARG = -1,
    ILP1_ERR_TRUNCATED = -2,
    ILP1_ERR_BAD_MAGIC = -3,
    ILP1_ERR_UNSUPPORTED_VERSION = -4,
    ILP1_ERR_LIMIT_EXCEEDED = -5,
    ILP1_ERR_NOMEM = -6,
    ILP1_ERR_OVERFLOW = -7,
    ILP1_ERR_BUFFER_TOO_SMALL = -8
} ilp1_status;

typedef struct ilp1_point { float x, y, pressure; } ilp1_point;

typedef struct ilp1_stroke {
    uint64_t    id_high, id_low, created_hlc_ms;
    uint32_t    argb_color;
    float       base_width_pt;
    uint32_t    flags;                 /* bit 0: ILP1_STROKE_FLAG_HIGHLIGHTER */
    float       offset_x, offset_y;
    uint64_t    offset_updated_hlc_ms;
    ilp1_point *points;
    uint32_t    point_count;
} ilp1_stroke;

typedef struct ilp1_text_block {
    uint64_t id_high, id_low, updated_hlc_ms;
    float    x, y, width, font_size_pt;
    uint32_t argb_color;
    char    *text;                     /* UTF-8 bytes (NUL-terminated when decoded/merged) */
    uint32_t text_len;                 /* exact UTF-8 byte length excluding trailing NUL */
} ilp1_text_block;

typedef struct ilp1_tombstone {
    uint64_t target_id_high, target_id_low, deleted_hlc_ms;
} ilp1_tombstone;

typedef struct ilp1_page {
    float            page_width_pt, page_height_pt;
    uint32_t         paper_style, bg_argb;
    ilp1_stroke     *strokes;
    uint32_t         stroke_count;
    ilp1_text_block *text_blocks;
    uint32_t         text_block_count;
    ilp1_tombstone  *tombstones;
    uint32_t         tombstone_count;
} ilp1_page;

ILP1_API void        ilp1_page_init(ilp1_page *page);
ILP1_API void        ilp1_page_free(ilp1_page *page);
ILP1_API void        ilp1_free(void *ptr);
ILP1_API ilp1_status ilp1_decode(const uint8_t *data, size_t len, ilp1_page *out_page);
ILP1_API ilp1_status ilp1_encoded_size(const ilp1_page *page, size_t *out_size);
ILP1_API ilp1_status ilp1_encode_to(const ilp1_page *page, uint8_t *buf, size_t buf_cap, size_t *out_written);
ILP1_API ilp1_status ilp1_encode(const ilp1_page *page, uint8_t **out_buf, size_t *out_len);
/* Deterministic lossless CRDT merge (metadata from `a`; safe when `out_merged` aliases `a` or `b`). */
ILP1_API ilp1_status ilp1_merge(const ilp1_page *a, const ilp1_page *b, ilp1_page *out_merged);

#ifdef __cplusplus
}
#endif
#endif /* ILP1_H */

#ifdef ILP1_IMPLEMENTATION

#include <stdlib.h>
#include <string.h>

#ifndef ILP1_MALLOC
#define ILP1_MALLOC(sz)    malloc(sz)
#endif
#ifndef ILP1_CALLOC
#define ILP1_CALLOC(n, sz) calloc((n), (sz))
#endif
#ifndef ILP1_FREE
#define ILP1_FREE(ptr)     free(ptr)
#endif

static bool ilp1__add_sz(size_t a, size_t b, size_t *out) {
    if (b > ((size_t)-1) - a) return false;
    *out = a + b;
    return true;
}

static bool ilp1__mul_sz(size_t a, size_t b, size_t *out) {
    if (a != 0 && b > ((size_t)-1) / a) return false;
    *out = a * b;
    return true;
}

static uint32_t ilp1__u32(const uint8_t *p) {
    return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t ilp1__u64(const uint8_t *p) {
    return ((uint64_t)ilp1__u32(p)) | ((uint64_t)ilp1__u32(p + 4) << 32);
}
static float ilp1__f32(const uint8_t *p) {
    uint32_t bits = ilp1__u32(p);
    float f = 0.0f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}
static void ilp1__put_u32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);         p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu); p[3] = (uint8_t)((v >> 24) & 0xFFu);
}
static void ilp1__put_u64(uint8_t *p, uint64_t v) {
    ilp1__put_u32(p, (uint32_t)(v & 0xFFFFFFFFull));
    ilp1__put_u32(p + 4, (uint32_t)((v >> 32) & 0xFFFFFFFFull));
}
static void ilp1__put_f32(uint8_t *p, float f) {
    uint32_t bits = 0;
    memcpy(&bits, &f, sizeof(bits));
    ilp1__put_u32(p, bits);
}
static int ilp1__cmp_u64(uint64_t a, uint64_t b) { return (a < b) ? -1 : ((a > b) ? 1 : 0); }
static int ilp1__cmp_id(uint64_t a_hi, uint64_t a_lo, uint64_t b_hi, uint64_t b_lo) {
    int c = ilp1__cmp_u64(a_hi, b_hi);
    return (c != 0) ? c : ilp1__cmp_u64(a_lo, b_lo);
}

ILP1_API void ilp1_page_init(ilp1_page *page) {
    if (!page) return;
    memset(page, 0, sizeof(*page));
    page->page_width_pt = 420.0f;
    page->page_height_pt = 595.0f;
    page->paper_style = (uint32_t)ILP1_PAPER_GRID;
    page->bg_argb = 0xFFFAF8F3u;
}

ILP1_API void ilp1_page_free(ilp1_page *page) {
    uint32_t i;
    if (!page) return;
    if (page->strokes) {
        for (i = 0; i < page->stroke_count; ++i) ILP1_FREE(page->strokes[i].points);
        ILP1_FREE(page->strokes);
    }
    if (page->text_blocks) {
        for (i = 0; i < page->text_block_count; ++i) ILP1_FREE(page->text_blocks[i].text);
        ILP1_FREE(page->text_blocks);
    }
    ILP1_FREE(page->tombstones);
    page->strokes = NULL;      page->stroke_count = 0;
    page->text_blocks = NULL;  page->text_block_count = 0;
    page->tombstones = NULL;   page->tombstone_count = 0;
}

ILP1_API void ilp1_free(void *ptr) { ILP1_FREE(ptr); }

ILP1_API ilp1_status ilp1_decode(const uint8_t *data, size_t len, ilp1_page *out_page) {
    ilp1_page tmp;
    ilp1_status err = ILP1_OK;
    size_t pos = ILP1_HEADER_SIZE, need = 0;
    uint32_t style, s_cnt, t_cnt, m_cnt, i, k;

    if (!data || !out_page) return ILP1_ERR_INVALID_ARG;
    if (len < ILP1_HEADER_SIZE) return ILP1_ERR_TRUNCATED;
    if (data[0] != 'I' || data[1] != 'L' || data[2] != 'P' || data[3] != '1') return ILP1_ERR_BAD_MAGIC;
    if (ilp1__u32(data + 4) != ILP1_VERSION) return ILP1_ERR_UNSUPPORTED_VERSION;

    memset(&tmp, 0, sizeof(tmp));
    tmp.page_width_pt  = ilp1__f32(data + 8);
    tmp.page_height_pt = ilp1__f32(data + 12);
    style              = ilp1__u32(data + 16);
    tmp.paper_style    = (style > (uint32_t)ILP1_PAPER_BLANK) ? (uint32_t)ILP1_PAPER_BLANK : style;
    tmp.bg_argb        = ilp1__u32(data + 20);
    s_cnt = ilp1__u32(data + 24);
    t_cnt = ilp1__u32(data + 28);
    m_cnt = ilp1__u32(data + 32);
    if (s_cnt > ILP1_MAX_STROKES || t_cnt > ILP1_MAX_TEXT_BLOCKS || m_cnt > ILP1_MAX_TOMBSTONES) {
        return ILP1_ERR_LIMIT_EXCEEDED;
    }

    if (s_cnt > 0) {
        if (!ilp1__mul_sz((size_t)s_cnt, ILP1_STROKE_HEADER_SIZE, &need) || need > len - pos) {
            err = ILP1_ERR_TRUNCATED; goto fail;
        }
        tmp.strokes = (ilp1_stroke *)ILP1_CALLOC((size_t)s_cnt, sizeof(ilp1_stroke));
        if (!tmp.strokes) { err = ILP1_ERR_NOMEM; goto fail; }
        tmp.stroke_count = s_cnt;
        for (i = 0; i < s_cnt; ++i) {
            ilp1_stroke *s = &tmp.strokes[i];
            uint32_t p_cnt;
            if (len - pos < ILP1_STROKE_HEADER_SIZE) { err = ILP1_ERR_TRUNCATED; goto fail; }
            s->id_high = ilp1__u64(data + pos);
            s->id_low = ilp1__u64(data + pos + 8);
            s->created_hlc_ms = ilp1__u64(data + pos + 16);
            s->argb_color = ilp1__u32(data + pos + 24);
            s->base_width_pt = ilp1__f32(data + pos + 28);
            s->flags = ilp1__u32(data + pos + 32);
            s->offset_x = ilp1__f32(data + pos + 36);
            s->offset_y = ilp1__f32(data + pos + 40);
            s->offset_updated_hlc_ms = ilp1__u64(data + pos + 44);
            p_cnt = ilp1__u32(data + pos + 52);
            pos += ILP1_STROKE_HEADER_SIZE;
            if (p_cnt > ILP1_MAX_POINTS) { err = ILP1_ERR_LIMIT_EXCEEDED; goto fail; }
            if (!ilp1__mul_sz((size_t)p_cnt, ILP1_POINT_SIZE, &need) || need > len - pos) {
                err = ILP1_ERR_TRUNCATED; goto fail;
            }
            if (p_cnt > 0) {
                s->points = (ilp1_point *)ILP1_MALLOC((size_t)p_cnt * sizeof(ilp1_point));
                if (!s->points) { err = ILP1_ERR_NOMEM; goto fail; }
                s->point_count = p_cnt;
                for (k = 0; k < p_cnt; ++k, pos += ILP1_POINT_SIZE) {
                    s->points[k].x = ilp1__f32(data + pos);
                    s->points[k].y = ilp1__f32(data + pos + 4);
                    s->points[k].pressure = ilp1__f32(data + pos + 8);
                }
            }
        }
    }

    if (t_cnt > 0) {
        if (!ilp1__mul_sz((size_t)t_cnt, ILP1_TEXT_BLOCK_HEADER_SIZE, &need) || need > len - pos) {
            err = ILP1_ERR_TRUNCATED; goto fail;
        }
        tmp.text_blocks = (ilp1_text_block *)ILP1_CALLOC((size_t)t_cnt, sizeof(ilp1_text_block));
        if (!tmp.text_blocks) { err = ILP1_ERR_NOMEM; goto fail; }
        tmp.text_block_count = t_cnt;
        for (i = 0; i < t_cnt; ++i) {
            ilp1_text_block *tb = &tmp.text_blocks[i];
            uint32_t ulen;
            if (len - pos < ILP1_TEXT_BLOCK_HEADER_SIZE) { err = ILP1_ERR_TRUNCATED; goto fail; }
            tb->id_high = ilp1__u64(data + pos);
            tb->id_low = ilp1__u64(data + pos + 8);
            tb->updated_hlc_ms = ilp1__u64(data + pos + 16);
            tb->x = ilp1__f32(data + pos + 24);
            tb->y = ilp1__f32(data + pos + 28);
            tb->width = ilp1__f32(data + pos + 32);
            tb->font_size_pt = ilp1__f32(data + pos + 36);
            tb->argb_color = ilp1__u32(data + pos + 40);
            ulen = ilp1__u32(data + pos + 44);
            pos += ILP1_TEXT_BLOCK_HEADER_SIZE;
            if (ulen > ILP1_MAX_TEXT_BYTES) { err = ILP1_ERR_LIMIT_EXCEEDED; goto fail; }
            if ((size_t)ulen > len - pos) { err = ILP1_ERR_TRUNCATED; goto fail; }
            tb->text = (char *)ILP1_MALLOC((size_t)ulen + 1u);
            if (!tb->text) { err = ILP1_ERR_NOMEM; goto fail; }
            if (ulen > 0) { memcpy(tb->text, data + pos, (size_t)ulen); pos += (size_t)ulen; }
            tb->text[ulen] = '\0';
            tb->text_len = ulen;
        }
    }

    if (m_cnt > 0) {
        if (!ilp1__mul_sz((size_t)m_cnt, ILP1_TOMBSTONE_SIZE, &need) || need > len - pos) {
            err = ILP1_ERR_TRUNCATED; goto fail;
        }
        tmp.tombstones = (ilp1_tombstone *)ILP1_MALLOC((size_t)m_cnt * sizeof(ilp1_tombstone));
        if (!tmp.tombstones) { err = ILP1_ERR_NOMEM; goto fail; }
        tmp.tombstone_count = m_cnt;
        for (i = 0; i < m_cnt; ++i, pos += ILP1_TOMBSTONE_SIZE) {
            tmp.tombstones[i].target_id_high = ilp1__u64(data + pos);
            tmp.tombstones[i].target_id_low  = ilp1__u64(data + pos + 8);
            tmp.tombstones[i].deleted_hlc_ms = ilp1__u64(data + pos + 16);
        }
    }

    *out_page = tmp;
    return ILP1_OK;
fail:
    ilp1_page_free(&tmp);
    return err;
}

ILP1_API ilp1_status ilp1_encoded_size(const ilp1_page *page, size_t *out_size) {
    size_t total = ILP1_HEADER_SIZE, part = 0;
    uint32_t i;
    if (!page || !out_size) return ILP1_ERR_INVALID_ARG;
    if ((page->stroke_count && !page->strokes) || (page->text_block_count && !page->text_blocks) ||
        (page->tombstone_count && !page->tombstones)) return ILP1_ERR_INVALID_ARG;

    for (i = 0; i < page->stroke_count; ++i) {
        const ilp1_stroke *s = &page->strokes[i];
        if (s->point_count && !s->points) return ILP1_ERR_INVALID_ARG;
        if (!ilp1__add_sz(total, ILP1_STROKE_HEADER_SIZE, &total) ||
            !ilp1__mul_sz((size_t)s->point_count, ILP1_POINT_SIZE, &part) ||
            !ilp1__add_sz(total, part, &total)) return ILP1_ERR_OVERFLOW;
    }
    for (i = 0; i < page->text_block_count; ++i) {
        const ilp1_text_block *tb = &page->text_blocks[i];
        if (tb->text_len && !tb->text) return ILP1_ERR_INVALID_ARG;
        if (!ilp1__add_sz(total, ILP1_TEXT_BLOCK_HEADER_SIZE, &total) ||
            !ilp1__add_sz(total, (size_t)tb->text_len, &total)) return ILP1_ERR_OVERFLOW;
    }
    if (!ilp1__mul_sz((size_t)page->tombstone_count, ILP1_TOMBSTONE_SIZE, &part) ||
        !ilp1__add_sz(total, part, &total)) return ILP1_ERR_OVERFLOW;
    *out_size = total;
    return ILP1_OK;
}

ILP1_API ilp1_status ilp1_encode_to(const ilp1_page *page, uint8_t *buf, size_t buf_cap, size_t *out_written) {
    size_t need = 0, pos = ILP1_HEADER_SIZE;
    uint32_t i, k;
    ilp1_status st = ilp1_encoded_size(page, &need);
    if (st != ILP1_OK) return st;
    if (!buf) return ILP1_ERR_INVALID_ARG;
    if (buf_cap < need) return ILP1_ERR_BUFFER_TOO_SMALL;

    buf[0] = 'I'; buf[1] = 'L'; buf[2] = 'P'; buf[3] = '1';
    ilp1__put_u32(buf + 4,  ILP1_VERSION);
    ilp1__put_f32(buf + 8,  page->page_width_pt);
    ilp1__put_f32(buf + 12, page->page_height_pt);
    ilp1__put_u32(buf + 16, page->paper_style);
    ilp1__put_u32(buf + 20, page->bg_argb);
    ilp1__put_u32(buf + 24, page->stroke_count);
    ilp1__put_u32(buf + 28, page->text_block_count);
    ilp1__put_u32(buf + 32, page->tombstone_count);
    ilp1__put_u32(buf + 36, 0u);

    for (i = 0; i < page->stroke_count; ++i) {
        const ilp1_stroke *s = &page->strokes[i];
        ilp1__put_u64(buf + pos, s->id_high);             ilp1__put_u64(buf + pos + 8, s->id_low);
        ilp1__put_u64(buf + pos + 16, s->created_hlc_ms); ilp1__put_u32(buf + pos + 24, s->argb_color);
        ilp1__put_f32(buf + pos + 28, s->base_width_pt);  ilp1__put_u32(buf + pos + 32, s->flags);
        ilp1__put_f32(buf + pos + 36, s->offset_x);       ilp1__put_f32(buf + pos + 40, s->offset_y);
        ilp1__put_u64(buf + pos + 44, s->offset_updated_hlc_ms);
        ilp1__put_u32(buf + pos + 52, s->point_count);
        pos += ILP1_STROKE_HEADER_SIZE;
        for (k = 0; k < s->point_count; ++k, pos += ILP1_POINT_SIZE) {
            ilp1__put_f32(buf + pos, s->points[k].x);
            ilp1__put_f32(buf + pos + 4, s->points[k].y);
            ilp1__put_f32(buf + pos + 8, s->points[k].pressure);
        }
    }
    for (i = 0; i < page->text_block_count; ++i) {
        const ilp1_text_block *tb = &page->text_blocks[i];
        ilp1__put_u64(buf + pos, tb->id_high);            ilp1__put_u64(buf + pos + 8, tb->id_low);
        ilp1__put_u64(buf + pos + 16, tb->updated_hlc_ms);
        ilp1__put_f32(buf + pos + 24, tb->x);             ilp1__put_f32(buf + pos + 28, tb->y);
        ilp1__put_f32(buf + pos + 32, tb->width);         ilp1__put_f32(buf + pos + 36, tb->font_size_pt);
        ilp1__put_u32(buf + pos + 40, tb->argb_color);    ilp1__put_u32(buf + pos + 44, tb->text_len);
        pos += ILP1_TEXT_BLOCK_HEADER_SIZE;
        if (tb->text_len > 0) { memcpy(buf + pos, tb->text, (size_t)tb->text_len); pos += (size_t)tb->text_len; }
    }
    for (i = 0; i < page->tombstone_count; ++i, pos += ILP1_TOMBSTONE_SIZE) {
        ilp1__put_u64(buf + pos, page->tombstones[i].target_id_high);
        ilp1__put_u64(buf + pos + 8, page->tombstones[i].target_id_low);
        ilp1__put_u64(buf + pos + 16, page->tombstones[i].deleted_hlc_ms);
    }
    if (out_written) *out_written = pos;
    return ILP1_OK;
}

ILP1_API ilp1_status ilp1_encode(const ilp1_page *page, uint8_t **out_buf, size_t *out_len) {
    size_t need = 0;
    uint8_t *buf;
    ilp1_status st;
    if (!out_buf || !out_len) return ILP1_ERR_INVALID_ARG;
    *out_buf = NULL; *out_len = 0;
    st = ilp1_encoded_size(page, &need);
    if (st != ILP1_OK) return st;
    buf = (uint8_t *)ILP1_MALLOC(need);
    if (!buf) return ILP1_ERR_NOMEM;
    st = ilp1_encode_to(page, buf, need, out_len);
    if (st != ILP1_OK) { ILP1_FREE(buf); *out_len = 0; return st; }
    *out_buf = buf;
    return ILP1_OK;
}

typedef struct ilp1__sref { const ilp1_stroke *s; uint32_t ord; } ilp1__sref;
typedef struct ilp1__tref { const ilp1_text_block *t; uint32_t ord; } ilp1__tref;

static int ilp1__cmp_tomb(const void *pa, const void *pb) {
    const ilp1_tombstone *a = (const ilp1_tombstone *)pa, *b = (const ilp1_tombstone *)pb;
    int c = ilp1__cmp_id(a->target_id_high, a->target_id_low, b->target_id_high, b->target_id_low);
    return c ? c : ilp1__cmp_u64(a->deleted_hlc_ms, b->deleted_hlc_ms);
}
static int ilp1__cmp_sref(const void *pa, const void *pb) {
    const ilp1__sref *a = (const ilp1__sref *)pa, *b = (const ilp1__sref *)pb;
    int c = ilp1__cmp_id(a->s->id_high, a->s->id_low, b->s->id_high, b->s->id_low);
    return c ? c : ((a->ord < b->ord) ? -1 : ((a->ord > b->ord) ? 1 : 0));
}
static int ilp1__cmp_sfinal(const void *pa, const void *pb) {
    const ilp1_stroke *a = (const ilp1_stroke *)pa, *b = (const ilp1_stroke *)pb;
    int c = ilp1__cmp_u64(a->created_hlc_ms, b->created_hlc_ms);
    return c ? c : ilp1__cmp_id(a->id_high, a->id_low, b->id_high, b->id_low);
}
static int ilp1__cmp_tref(const void *pa, const void *pb) {
    const ilp1__tref *a = (const ilp1__tref *)pa, *b = (const ilp1__tref *)pb;
    int c = ilp1__cmp_id(a->t->id_high, a->t->id_low, b->t->id_high, b->t->id_low);
    return c ? c : ((a->ord < b->ord) ? -1 : ((a->ord > b->ord) ? 1 : 0));
}
static const ilp1_tombstone *ilp1__find_tomb(const ilp1_tombstone *tm, uint32_t n, uint64_t hi, uint64_t lo) {
    uint32_t lo_i = 0, hi_i = n;
    while (lo_i < hi_i) {
        uint32_t mid = lo_i + (hi_i - lo_i) / 2u;
        int c = ilp1__cmp_id(tm[mid].target_id_high, tm[mid].target_id_low, hi, lo);
        if (c == 0) return &tm[mid];
        if (c < 0) lo_i = mid + 1u; else hi_i = mid;
    }
    return NULL;
}

ILP1_API ilp1_status ilp1_merge(const ilp1_page *a, const ilp1_page *b, ilp1_page *out_merged) {
    ilp1_page res;
    ilp1_status err = ILP1_OK;
    ilp1__sref *srefs = NULL;
    ilp1__tref *trefs = NULL;
    size_t n_tm, n_st, n_tb;
    uint32_t i;

    if (!a || !b || !out_merged) return ILP1_ERR_INVALID_ARG;
    memset(&res, 0, sizeof(res));
    res.page_width_pt = a->page_width_pt; res.page_height_pt = a->page_height_pt;
    res.paper_style = a->paper_style;     res.bg_argb = a->bg_argb;

    n_tm = (size_t)a->tombstone_count + (size_t)b->tombstone_count;
    n_st = (size_t)a->stroke_count + (size_t)b->stroke_count;
    n_tb = (size_t)a->text_block_count + (size_t)b->text_block_count;
    if (n_tm > ILP1_MAX_TOMBSTONES || n_st > ILP1_MAX_STROKES || n_tb > ILP1_MAX_TEXT_BLOCKS) {
        return ILP1_ERR_LIMIT_EXCEEDED;
    }

    if (n_tm > 0) {
        uint32_t out_n = 0;
        res.tombstones = (ilp1_tombstone *)ILP1_MALLOC(n_tm * sizeof(ilp1_tombstone));
        if (!res.tombstones) { err = ILP1_ERR_NOMEM; goto fail; }
        if (a->tombstone_count) memcpy(res.tombstones, a->tombstones, (size_t)a->tombstone_count * sizeof(ilp1_tombstone));
        if (b->tombstone_count) memcpy(res.tombstones + a->tombstone_count, b->tombstones, (size_t)b->tombstone_count * sizeof(ilp1_tombstone));
        qsort(res.tombstones, n_tm, sizeof(ilp1_tombstone), ilp1__cmp_tomb);
        for (i = 0; i < (uint32_t)n_tm; ++i) {
            if (out_n && res.tombstones[out_n - 1u].target_id_high == res.tombstones[i].target_id_high &&
                res.tombstones[out_n - 1u].target_id_low == res.tombstones[i].target_id_low) {
                if (res.tombstones[i].deleted_hlc_ms > res.tombstones[out_n - 1u].deleted_hlc_ms)
                    res.tombstones[out_n - 1u].deleted_hlc_ms = res.tombstones[i].deleted_hlc_ms;
            } else {
                res.tombstones[out_n++] = res.tombstones[i];
            }
        }
        res.tombstone_count = out_n;
    }

    if (n_st > 0) {
        uint32_t rn = 0, out_n = 0;
        const ilp1_page *srcs[2] = {a, b};
        int si;
        srefs = (ilp1__sref *)ILP1_MALLOC(n_st * sizeof(ilp1__sref));
        if (!srefs) { err = ILP1_ERR_NOMEM; goto fail; }
        for (si = 0; si < 2; ++si) {
            for (i = 0; i < srcs[si]->stroke_count; ++i) {
                const ilp1_stroke *s = &srcs[si]->strokes[i];
                if (!ilp1__find_tomb(res.tombstones, res.tombstone_count, s->id_high, s->id_low)) {
                    srefs[rn].s = s; srefs[rn].ord = rn; rn++;
                }
            }
        }
        if (rn > 0) {
            qsort(srefs, (size_t)rn, sizeof(ilp1__sref), ilp1__cmp_sref);
            res.strokes = (ilp1_stroke *)ILP1_CALLOC((size_t)rn, sizeof(ilp1_stroke));
            if (!res.strokes) { err = ILP1_ERR_NOMEM; goto fail; }
            for (i = 0; i < rn; ) {
                ilp1_stroke cur = *srefs[i].s;
                const ilp1_point *pts = cur.points;
                uint32_t j = i + 1u;
                while (j < rn && srefs[j].s->id_high == cur.id_high && srefs[j].s->id_low == cur.id_low) {
                    if (srefs[j].s->offset_updated_hlc_ms > cur.offset_updated_hlc_ms) {
                        cur.offset_x = srefs[j].s->offset_x;
                        cur.offset_y = srefs[j].s->offset_y;
                        cur.offset_updated_hlc_ms = srefs[j].s->offset_updated_hlc_ms;
                        cur.argb_color = srefs[j].s->argb_color;
                    }
                    j++;
                }
                cur.points = NULL;
                if (cur.point_count > 0) {
                    cur.points = (ilp1_point *)ILP1_MALLOC((size_t)cur.point_count * sizeof(ilp1_point));
                    if (!cur.points) { res.stroke_count = out_n; err = ILP1_ERR_NOMEM; goto fail; }
                    memcpy(cur.points, pts, (size_t)cur.point_count * sizeof(ilp1_point));
                }
                res.strokes[out_n++] = cur;
                i = j;
            }
            res.stroke_count = out_n;
            qsort(res.strokes, (size_t)out_n, sizeof(ilp1_stroke), ilp1__cmp_sfinal);
        }
        ILP1_FREE(srefs); srefs = NULL;
    }

    if (n_tb > 0) {
        uint32_t rn = 0, out_n = 0;
        const ilp1_page *srcs[2] = {a, b};
        int si;
        trefs = (ilp1__tref *)ILP1_MALLOC(n_tb * sizeof(ilp1__tref));
        if (!trefs) { err = ILP1_ERR_NOMEM; goto fail; }
        for (si = 0; si < 2; ++si) {
            for (i = 0; i < srcs[si]->text_block_count; ++i) {
                const ilp1_text_block *tb = &srcs[si]->text_blocks[i];
                const ilp1_tombstone *tm = ilp1__find_tomb(res.tombstones, res.tombstone_count, tb->id_high, tb->id_low);
                if (!tm || tm->deleted_hlc_ms < tb->updated_hlc_ms) {
                    trefs[rn].t = tb; trefs[rn].ord = rn; rn++;
                }
            }
        }
        if (rn > 0) {
            qsort(trefs, (size_t)rn, sizeof(ilp1__tref), ilp1__cmp_tref);
            res.text_blocks = (ilp1_text_block *)ILP1_CALLOC((size_t)rn, sizeof(ilp1_text_block));
            if (!res.text_blocks) { err = ILP1_ERR_NOMEM; goto fail; }
            for (i = 0; i < rn; ) {
                const ilp1_text_block *best = trefs[i].t;
                uint32_t j = i + 1u;
                ilp1_text_block copy;
                while (j < rn && trefs[j].t->id_high == best->id_high && trefs[j].t->id_low == best->id_low) {
                    if (trefs[j].t->updated_hlc_ms > best->updated_hlc_ms) best = trefs[j].t;
                    j++;
                }
                copy = *best;
                copy.text = (char *)ILP1_MALLOC((size_t)best->text_len + 1u);
                if (!copy.text) { res.text_block_count = out_n; err = ILP1_ERR_NOMEM; goto fail; }
                if (best->text_len > 0) memcpy(copy.text, best->text, (size_t)best->text_len);
                copy.text[best->text_len] = '\0';
                res.text_blocks[out_n++] = copy;
                i = j;
            }
            res.text_block_count = out_n;
        }
        ILP1_FREE(trefs); trefs = NULL;
    }

    if (out_merged == a || out_merged == b) ilp1_page_free(out_merged);
    *out_merged = res;
    return ILP1_OK;
fail:
    ILP1_FREE(srefs);
    ILP1_FREE(trefs);
    ilp1_page_free(&res);
    return err;
}

#endif /* ILP1_IMPLEMENTATION */
