#ifdef __x86_64__

#include <stdio.h>
#include <stdint.h>
#include <x86intrin.h>

int utf8_naive(const unsigned char *data, int len);

struct previous_input {
    __m128i input;
    __m128i follow_bytes;
};

static const int8_t _follow_tbl[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 00 ~ BF */
    1, 1,                               /* C0 ~ DF */
    2,                                  /* E0 ~ EF */
    3,                                  /* F0 ~ FF */
};

static const int8_t _follow_mask_tbl[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, /* 00 ~ BF */
    8, 8,                               /* C0 ~ DF */
    8,                                  /* E0 ~ EF */
    8,                                  /* F0 ~ FF */
};

static const int8_t _range_min_tbl[] = {
    /* 0,    1,    2,    3,    4,    5,    6,    7,    8 */
    0x00, 0x80, 0x80, 0x80, 0xA0, 0x80, 0x90, 0x80, 0xC2,
    /* Must be invalid */
    0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F, 0x7F,
};

static const int8_t _range_max_tbl[] = {
    /* 0,    1,    2,    3,    4,    5,    6,    7,    8 */
    0x7F, 0xBF, 0xBF, 0xBF, 0xBF, 0x9F, 0xBF, 0x8F, 0xF4,
    /* Must be invalid */
    0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80,
};

/* Get number of followup bytes to take care per high nibble */
static inline __m128i get_followup_bytes(
        const __m128i input, const __m128i follow_table,
        __m128i *mask, const __m128i mask_table)
{
    /* Why no _mm_srli_epi8 ? */
    const __m128i high_nibbles =
        _mm_and_si128(_mm_srli_epi16(input, 4), _mm_set1_epi8(0x0F));

    *mask = _mm_shuffle_epi8(mask_table, high_nibbles);
    return _mm_shuffle_epi8(follow_table, high_nibbles);
}

static inline __m128i validate(const unsigned char *data, __m128i error,
       struct previous_input *prev, const __m128i tables[])
{
    __m128i range1, range2;

    const __m128i input1 = _mm_lddqu_si128((const __m128i *)data);
    const __m128i input2 = _mm_lddqu_si128((const __m128i *)(data+16));

    /* range is 8 if input=0xC0~0xFF, overlap will lead to 9, 10, 11 */
    __m128i follow_bytes1 =
        get_followup_bytes(input1, tables[0], &range1, tables[1]);
    __m128i follow_bytes2 =
        get_followup_bytes(input2, tables[0], &range2, tables[1]);

    /* 2nd byte */
    /* range = (follow_bytes, prev.follow_bytes) << 1 byte */
    range1 = _mm_or_si128(
            range1, _mm_alignr_epi8(follow_bytes1, prev->follow_bytes, 15));
    range2 = _mm_or_si128(
            range2, _mm_alignr_epi8(follow_bytes2, follow_bytes1, 15));

    /* 3rd bytes */
    __m128i subp, sub1, sub2;
    /* saturate sub 1 */
    subp = _mm_subs_epu8(prev->follow_bytes, _mm_set1_epi8(1));
    sub1 = _mm_subs_epu8(follow_bytes1, _mm_set1_epi8(1));
    sub2 = _mm_subs_epu8(follow_bytes2, _mm_set1_epi8(1));
    /* range1 |= (sub1, subp) << 2 bytes */
    range1 = _mm_or_si128(range1, _mm_alignr_epi8(sub1, subp, 14));
    /* range2 |= (sub2, sub1) << 2 bytes */
    range2 = _mm_or_si128(range2, _mm_alignr_epi8(sub2, sub1, 14));

    /* 4th bytes */
    /* saturate sub 2 */
    subp = _mm_subs_epu8(prev->follow_bytes, _mm_set1_epi8(2));
    sub1 = _mm_subs_epu8(follow_bytes1, _mm_set1_epi8(2));
    sub2 = _mm_subs_epu8(follow_bytes2, _mm_set1_epi8(2));
    /* range1 |= (sub1, subp) << 3 bytes */
    range1 = _mm_or_si128(range1, _mm_alignr_epi8(sub1, subp, 13));
    /* range2 |= (sub2, sub1) << 3 bytes */
    range2 = _mm_or_si128(range2, _mm_alignr_epi8(sub2, sub1, 13));

    /*
     * Check special cases (not 80..BF)
     * +------------+---------------------+-------------------+
     * | First Byte | Special Second Byte | range table index |
     * +------------+---------------------+-------------------+
     * | E0         | A0..BF              | 4                 |
     * | ED         | 80..9F              | 5                 |
     * | F0         | 90..BF              | 6                 |
     * | F4         | 80..8F              | 7                 |
     * +------------+---------------------+-------------------+
     */
    __m128i shift1, pos;

    /* shift1 = (input1, prev.input) << 1 byte */
    shift1 = _mm_alignr_epi8(input1, prev->input, 15);
    pos = _mm_cmpeq_epi8(shift1, _mm_set1_epi8(0xE0));
    range1 = _mm_add_epi8(range1, _mm_and_si128(pos, _mm_set1_epi8(2)));/*2+2*/
    pos = _mm_cmpeq_epi8(shift1, _mm_set1_epi8(0xED));
    range1 = _mm_add_epi8(range1, _mm_and_si128(pos, _mm_set1_epi8(3)));/*2+3*/
    pos = _mm_cmpeq_epi8(shift1, _mm_set1_epi8(0xF0));
    range1 = _mm_add_epi8(range1, _mm_and_si128(pos, _mm_set1_epi8(3)));/*3+3*/
    pos = _mm_cmpeq_epi8(shift1, _mm_set1_epi8(0xF4));
    range1 = _mm_add_epi8(range1, _mm_and_si128(pos, _mm_set1_epi8(4)));/*3+4*/

    /* shift1 = (input2, input1) << 1 byte */
    shift1 = _mm_alignr_epi8(input2, input1, 15);
    pos = _mm_cmpeq_epi8(shift1, _mm_set1_epi8(0xE0));
    range2 = _mm_add_epi8(range2, _mm_and_si128(pos, _mm_set1_epi8(2)));/*2+2*/
    pos = _mm_cmpeq_epi8(shift1, _mm_set1_epi8(0xED));
    range2 = _mm_add_epi8(range2, _mm_and_si128(pos, _mm_set1_epi8(3)));/*2+3*/
    pos = _mm_cmpeq_epi8(shift1, _mm_set1_epi8(0xF0));
    range2 = _mm_add_epi8(range2, _mm_and_si128(pos, _mm_set1_epi8(3)));/*3+3*/
    pos = _mm_cmpeq_epi8(shift1, _mm_set1_epi8(0xF4));
    range2 = _mm_add_epi8(range2, _mm_and_si128(pos, _mm_set1_epi8(4)));/*3+4*/

    /* Check value range */
    __m128i minv1 = _mm_shuffle_epi8(tables[2], range1);
    __m128i maxv1 = _mm_shuffle_epi8(tables[3], range1);
    __m128i minv2 = _mm_shuffle_epi8(tables[2], range2);
    __m128i maxv2 = _mm_shuffle_epi8(tables[3], range2);

    /* error |= ((input < min) | (input > max)) */
    error = _mm_or_si128(error, _mm_cmplt_epi8(input1, minv1));
    error = _mm_or_si128(error, _mm_cmpgt_epi8(input1, maxv1));
    error = _mm_or_si128(error, _mm_cmplt_epi8(input2, minv2));
    error = _mm_or_si128(error, _mm_cmpgt_epi8(input2, maxv2));

    prev->input = input2;
    prev->follow_bytes = follow_bytes2;

    return error;
}

int utf8_range2(const unsigned char *data, int len)
{
    if (len >= 32) {
        struct previous_input previous_input;

        previous_input.input = _mm_set1_epi8(0);
        previous_input.follow_bytes = _mm_set1_epi8(0);

        /* Cached constant tables */
        __m128i tables[4];

        tables[0] = _mm_lddqu_si128((const __m128i *)_follow_tbl);
        tables[1] = _mm_lddqu_si128((const __m128i *)_follow_mask_tbl);
        tables[2] = _mm_lddqu_si128((const __m128i *)_range_min_tbl);
        tables[3] = _mm_lddqu_si128((const __m128i *)_range_max_tbl);

        __m128i error = _mm_set1_epi8(0);

        while (len >= 32) {
            error = validate(data, error, &previous_input, tables);

            data += 32;
            len -= 32;
        }

        /* Delay error check till loop ends */
        /* Reduce error vector, error_reduced = 0xFFFF if error == 0 */
        int error_reduced =
            _mm_movemask_epi8(_mm_cmpeq_epi8(error, _mm_set1_epi8(0)));
        if (error_reduced != 0xFFFF)
            return 0;

        /* Find previous token (not 80~BF) */
        int32_t token4 = _mm_extract_epi32(previous_input.input, 3);

        const int8_t *token = (const int8_t *)&token4;
        int lookahead = 0;
        if (token[3] > (int8_t)0xBF)
            lookahead = 1;
        else if (token[2] > (int8_t)0xBF)
            lookahead = 2;
        else if (token[1] > (int8_t)0xBF)
            lookahead = 3;
        data -= lookahead;
        len += lookahead;
    }

    /* Check remaining bytes with naive method */
    return utf8_naive(data, len);
}

#endif
