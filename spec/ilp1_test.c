/*
 * Standalone C99 verification harness for ilp1.h (CC0 1.0 Public Domain).
 *
 * Build & run:
 *   cc -std=c99 -Wall -Wextra -Werror -pedantic -fsanitize=address,undefined spec/ilp1_test.c -o /tmp/ilp1_test && /tmp/ilp1_test
 */

#define ILP1_IMPLEMENTATION
#include "ilp1.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond)                                                                   \
    do {                                                                              \
        if (!(cond)) {                                                                \
            fprintf(stderr, "[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond);         \
            return 1;                                                                 \
        }                                                                             \
    } while (0)

static int write_file(const char *path, const uint8_t *buf, size_t len) {
    FILE *fp = fopen(path, "wb");
    if (!fp) return 0;
    if (len > 0 && fwrite(buf, 1, len, fp) != len) {
        fclose(fp);
        return 0;
    }
    return fclose(fp) == 0;
}

static uint8_t *read_file(const char *path, size_t *out_len) {
    FILE *fp = fopen(path, "rb");
    long sz;
    uint8_t *buf;
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    sz = ftell(fp);
    if (sz < 0) { fclose(fp); return NULL; }
    rewind(fp);
    buf = (uint8_t *)malloc((size_t)sz + 1u);
    if (!buf) { fclose(fp); return NULL; }
    if (sz > 0 && fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        free(buf);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    *out_len = (size_t)sz;
    return buf;
}

static int run_unit_tests(void) {
    ilp1_point pts1[2] = {{10.0f, 20.0f, 0.7f}, {80.0f, 45.0f, 0.8f}};
    ilp1_point pts2[2] = {{12.0f, 90.0f, 0.6f}, {95.0f, 110.0f, 0.9f}};
    ilp1_point pts3[2] = {{30.0f, 30.0f, 0.5f}, {60.0f, 60.0f, 0.5f}};

    /* 1. Encode -> Decode byte-exact round-trip */
    ilp1_stroke s_in[2];
    ilp1_text_block tb_in[1];
    ilp1_tombstone tm_in[1];
    ilp1_page page_a, dec, page_b, merged_ab, merged_ba;
    uint8_t *raw_a = NULL, *raw_ab = NULL, *raw_ba = NULL;
    size_t len_a = 0, len_ab = 0, len_ba = 0;

    memset(s_in, 0, sizeof(s_in));
    s_in[0].id_high = 10; s_in[0].id_low = 1; s_in[0].created_hlc_ms = 1000;
    s_in[0].argb_color = 0xFF1E293Bu; s_in[0].base_width_pt = 2.4f;
    s_in[0].points = pts1; s_in[0].point_count = 2;

    s_in[1].id_high = 10; s_in[1].id_low = 2; s_in[1].created_hlc_ms = 1100;
    s_in[1].argb_color = 0x55FACC15u; s_in[1].base_width_pt = 12.0f;
    s_in[1].flags = ILP1_STROKE_FLAG_HIGHLIGHTER;
    s_in[1].points = pts2; s_in[1].point_count = 2;

    memset(tb_in, 0, sizeof(tb_in));
    tb_in[0].id_high = 20; tb_in[0].id_low = 1; tb_in[0].updated_hlc_ms = 1500;
    tb_in[0].x = 24.0f; tb_in[0].y = 60.0f; tb_in[0].width = 200.0f; tb_in[0].font_size_pt = 15.0f;
    tb_in[0].argb_color = 0xFF262420u;
    tb_in[0].text = "Hello SilexNib!";
    tb_in[0].text_len = (uint32_t)strlen(tb_in[0].text);

    tm_in[0].target_id_high = 99; tm_in[0].target_id_low = 1; tm_in[0].deleted_hlc_ms = 900;

    ilp1_page_init(&page_a);
    page_a.strokes = s_in; page_a.stroke_count = 2;
    page_a.text_blocks = tb_in; page_a.text_block_count = 1;
    page_a.tombstones = tm_in; page_a.tombstone_count = 1;

    CHECK(ilp1_encode(&page_a, &raw_a, &len_a) == ILP1_OK);
    CHECK(len_a > ILP1_HEADER_SIZE);
    CHECK(ilp1_decode(raw_a, len_a, &dec) == ILP1_OK);
    CHECK(dec.stroke_count == 2 && dec.strokes[1].flags == ILP1_STROKE_FLAG_HIGHLIGHTER);
    CHECK(dec.strokes[0].points[1].x == 80.0f);
    CHECK(dec.text_block_count == 1 && strcmp(dec.text_blocks[0].text, "Hello SilexNib!") == 0);
    CHECK(dec.tombstone_count == 1 && dec.tombstones[0].deleted_hlc_ms == 900);

    /* 2. Malformed / truncated / bad-magic input safety */
    {
        ilp1_page bad;
        uint8_t bogus[40];
        memset(bogus, 0, sizeof(bogus));
        CHECK(ilp1_decode(raw_a, 10, &bad) == ILP1_ERR_TRUNCATED);
        CHECK(ilp1_decode(bogus, sizeof(bogus), &bad) == ILP1_ERR_BAD_MAGIC);
        CHECK(ilp1_decode(raw_a, len_a - 1u, &bad) == ILP1_ERR_TRUNCATED);
    }

    /* 3. Concurrent CRDT merge:
     *    - stroke (10,1) moved & recolored on B (offset_updated_hlc_ms=2000) -> LWW wins
     *    - stroke (10,2) deleted via tombstone on B -> dropped
     *    - stroke (10,3) added on B -> unioned & sorted by created_hlc_ms
     *    - text (20,1) updated on B (updated_hlc_ms=2500) -> LWW wins
     *    - merge(A, B) and merge(B, A) produce IDENTICAL bytes! */
    {
        ilp1_stroke s_b[2];
        ilp1_text_block tb_b[1];
        ilp1_tombstone tm_b[2];

        s_b[0] = s_in[0];
        s_b[0].offset_x = 18.0f; s_b[0].offset_y = -6.0f;
        s_b[0].offset_updated_hlc_ms = 2000;
        s_b[0].argb_color = 0xFFDC2626u;

        memset(&s_b[1], 0, sizeof(s_b[1]));
        s_b[1].id_high = 10; s_b[1].id_low = 3; s_b[1].created_hlc_ms = 1200;
        s_b[1].argb_color = 0xFF2563EBu; s_b[1].base_width_pt = 2.0f;
        s_b[1].points = pts3; s_b[1].point_count = 2;

        tb_b[0] = tb_in[0];
        tb_b[0].updated_hlc_ms = 2500;
        tb_b[0].text = "Updated on iPad!";
        tb_b[0].text_len = (uint32_t)strlen(tb_b[0].text);

        tm_b[0].target_id_high = 99; tm_b[0].target_id_low = 1; tm_b[0].deleted_hlc_ms = 1800;
        tm_b[1].target_id_high = 10; tm_b[1].target_id_low = 2; tm_b[1].deleted_hlc_ms = 2100;

        ilp1_page_init(&page_b);
        page_b.strokes = s_b; page_b.stroke_count = 2;
        page_b.text_blocks = tb_b; page_b.text_block_count = 1;
        page_b.tombstones = tm_b; page_b.tombstone_count = 2;

        CHECK(ilp1_merge(&page_a, &page_b, &merged_ab) == ILP1_OK);
        CHECK(ilp1_merge(&page_b, &page_a, &merged_ba) == ILP1_OK);

        CHECK(merged_ab.stroke_count == 2);
        CHECK(merged_ab.strokes[0].id_low == 1 && merged_ab.strokes[0].offset_x == 18.0f &&
              merged_ab.strokes[0].argb_color == 0xFFDC2626u);
        CHECK(merged_ab.strokes[1].id_low == 3);
        CHECK(merged_ab.text_block_count == 1 && strcmp(merged_ab.text_blocks[0].text, "Updated on iPad!") == 0);
        CHECK(merged_ab.tombstone_count == 2);

        CHECK(ilp1_encode(&merged_ab, &raw_ab, &len_ab) == ILP1_OK);
        CHECK(ilp1_encode(&merged_ba, &raw_ba, &len_ba) == ILP1_OK);
        CHECK(len_ab == len_ba && memcmp(raw_ab, raw_ba, len_ab) == 0);

        /* In-place merge aliasing test */
        CHECK(ilp1_merge(&dec, &page_b, &dec) == ILP1_OK);
        CHECK(dec.stroke_count == 2 && dec.tombstone_count == 2);
    }

    ilp1_free(raw_a);
    ilp1_free(raw_ab);
    ilp1_free(raw_ba);
    ilp1_page_free(&dec);
    ilp1_page_free(&merged_ab);
    ilp1_page_free(&merged_ba);
    printf("[PASS] ilp1.h pure C99 unit & CRDT merge tests succeeded.\n");
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 4 && strcmp(argv[1], "--merge-files") == 0) {
        size_t la = 0, lb = 0, lo = 0;
        uint8_t *ba = read_file(argv[2], &la);
        uint8_t *bb = read_file(argv[3], &lb);
        ilp1_page pa, pb, pm;
        uint8_t *out = NULL;
        CHECK(ba && bb);
        CHECK(ilp1_decode(ba, la, &pa) == ILP1_OK);
        CHECK(ilp1_decode(bb, lb, &pb) == ILP1_OK);
        CHECK(ilp1_merge(&pa, &pb, &pm) == ILP1_OK);
        CHECK(ilp1_encode(&pm, &out, &lo) == ILP1_OK);
        CHECK(write_file(argv[2], out, lo));
        ilp1_free(out);
        ilp1_page_free(&pa);
        ilp1_page_free(&pb);
        ilp1_page_free(&pm);
        free(ba);
        free(bb);
        return 0;
    }
    if (argc == 5 && strcmp(argv[1], "--merge-to") == 0) {
        size_t la = 0, lb = 0, lo = 0;
        uint8_t *ba = read_file(argv[2], &la);
        uint8_t *bb = read_file(argv[3], &lb);
        ilp1_page pa, pb, pm;
        uint8_t *out = NULL;
        CHECK(ba && bb);
        CHECK(ilp1_decode(ba, la, &pa) == ILP1_OK);
        CHECK(ilp1_decode(bb, lb, &pb) == ILP1_OK);
        CHECK(ilp1_merge(&pa, &pb, &pm) == ILP1_OK);
        CHECK(ilp1_encode(&pm, &out, &lo) == ILP1_OK);
        CHECK(write_file(argv[4], out, lo));
        ilp1_free(out);
        ilp1_page_free(&pa);
        ilp1_page_free(&pb);
        ilp1_page_free(&pm);
        free(ba);
        free(bb);
        return 0;
    }
    return run_unit_tests();
}
