/**
 * @file encode.c
 *
 * @author Mathis Rosenhauer, Deutsches Klimarechenzentrum
 * @author Moritz Hanke, Deutsches Klimarechenzentrum
 * @author Joerg Behrens, Deutsches Klimarechenzentrum
 * @author Luis Kornblueh, Max-Planck-Institut fuer Meteorologie
 *
 * @section LICENSE
 * Copyright 2012 - 2014
 *
 * Mathis Rosenhauer,                 Luis Kornblueh
 * Moritz Hanke,
 * Joerg Behrens
 *
 * Deutsches Klimarechenzentrum GmbH  Max-Planck-Institut fuer Meteorologie
 * Bundesstr. 45a                     Bundesstr. 53
 * 20146 Hamburg                      20146 Hamburg
 * Germany                            Germany
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials provided
 *    with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * @section DESCRIPTION
 *
 * Adaptive Entropy Encoder
 * Based on CCSDS documents 121.0-B-2 and 120.0-G-3
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libaec.h"
#include "encode.h"
#include "encode_accessors.h"

static int m_get_block(struct aec_stream *strm);

static inline void emit(struct internal_state *state,
                        uint32_t data, int bits)
{
    /**
       Emit sequence of bits.
     */

    if (bits <= state->bits) {
        state->bits -= bits;
        *state->cds += data << state->bits;
    } else {
        bits -= state->bits;
        *state->cds++ += (uint64_t)data >> bits;

        while (bits > 8) {
            bits -= 8;
            *state->cds++ = data >> bits;
        }

        state->bits = 8 - bits;
        *state->cds = data << state->bits;
    }
}

static inline void emitfs(struct internal_state *state, int fs)
{
    /**
       Emits a fundamental sequence.

       fs zero bits followed by one 1 bit.
     */

    for(;;) {
        if (fs < state->bits) {
            state->bits -= fs + 1;
            *state->cds += 1U << state->bits;
            break;
        } else {
            fs -= state->bits;
            *++state->cds = 0;
            state->bits = 8;
        }
    }
}

static inline void copy64(uint8_t *dst, uint64_t src)
{
    dst[0] = src >> 56;
    dst[1] = src >> 48;
    dst[2] = src >> 40;
    dst[3] = src >> 32;
    dst[4] = src >> 24;
    dst[5] = src >> 16;
    dst[6] = src >> 8;
    dst[7] = src;
}

static inline void emitblock_fs(struct aec_stream *strm, int k, int ref)
{
    int i;
    int used; /* used bits in 64 bit accumulator */
    uint64_t acc; /* accumulator */
    struct internal_state *state = strm->state;

    acc = (uint64_t)*state->cds << 56;
    used = 7 - state->bits;

    for (i = ref; i < strm->block_size; i++) {
        used += (state->block[i] >> k) + 1;
        while (used > 63) {
            copy64(state->cds, acc);
            state->cds += 8;
            acc = 0;
            used -= 64;
        }
        acc |= 1ULL << (63 - used);
    }

    copy64(state->cds, acc);
    state->cds += used >> 3;
    state->bits = 7 - (used & 7);
}

static inline void emitblock(struct aec_stream *strm, int k, int ref)
{
    /**
       Emit the k LSB of a whole block of input data.
    */

    uint64_t a;
    struct internal_state *state = strm->state;
    uint32_t *in = state->block + ref;
    uint32_t *in_end = state->block + strm->block_size;
    uint64_t mask = (1ULL << k) - 1;
    uint8_t *o = state->cds;
    int p = state->bits;

    a = *o;

    while(in < in_end) {
        a <<= 56;
        p = (p % 8) + 56;

        while (p > k && in < in_end) {
            p -= k;
            a += ((uint64_t)(*in++) & mask) << p;
        }

        switch (p & ~7) {
        case 0:
            o[0] = a >> 56;
            o[1] = a >> 48;
            o[2] = a >> 40;
            o[3] = a >> 32;
            o[4] = a >> 24;
            o[5] = a >> 16;
            o[6] = a >> 8;
            o += 7;
            break;
        case 8:
            o[0] = a >> 56;
            o[1] = a >> 48;
            o[2] = a >> 40;
            o[3] = a >> 32;
            o[4] = a >> 24;
            o[5] = a >> 16;
            a >>= 8;
            o += 6;
            break;
        case 16:
            o[0] = a >> 56;
            o[1] = a >> 48;
            o[2] = a >> 40;
            o[3] = a >> 32;
            o[4] = a >> 24;
            a >>= 16;
            o += 5;
            break;
        case 24:
            o[0] = a >> 56;
            o[1] = a >> 48;
            o[2] = a >> 40;
            o[3] = a >> 32;
            a >>= 24;
            o += 4;
            break;
        case 32:
            o[0] = a >> 56;
            o[1] = a >> 48;
            o[2] = a >> 40;
            a >>= 32;
            o += 3;
            break;
        case 40:
            o[0] = a >> 56;
            o[1] = a >> 48;
            a >>= 40;
            o += 2;
            break;
        case 48:
            *o++ = a >> 56;
            a >>= 48;
            break;
        default:
            a >>= 56;
            break;
        }
    }

    *o = a;
    state->cds = o;
    state->bits = p % 8;
}

static void preprocess_unsigned(struct aec_stream *strm)
{
    /**
       Preprocess RSI of unsigned samples.

       Combining preprocessing and converting to uint32_t in one loop
       is slower due to the data dependance on x_i-1.
    */

    uint32_t D;
    struct internal_state *state = strm->state;
    const uint32_t *restrict x = state->data_raw;
    uint32_t *restrict d = state->data_pp;
    uint32_t xmax = state->xmax;
    uint32_t rsi = strm->rsi * strm->block_size - 1;
    int i;

    d[0] = x[0];
    for (i = 0; i < rsi; i++) {
        if (x[i + 1] >= x[i]) {
            D = x[i + 1] - x[i];
            if (D <= x[i])
                d[i + 1] = 2 * D;
            else
                d[i + 1] = x[i + 1];
        } else {
            D = x[i] - x[i + 1];
            if (D <= xmax - x[i])
                d[i + 1] = 2 * D - 1;
            else
                d[i + 1] = xmax - x[i + 1];
        }
    }
    state->ref = 1;
    state->uncomp_len = (strm->block_size - 1) * strm->bits_per_sample;
}

static void preprocess_signed(struct aec_stream *strm)
{
    /**
       Preprocess RSI of signed samples.
    */

    int64_t D;
    struct internal_state *state = strm->state;
    uint32_t *restrict d = state->data_pp;
    int32_t *restrict x = (int32_t *)state->data_raw;
    uint64_t m = 1ULL << (strm->bits_per_sample - 1);
    int64_t xmax = state->xmax;
    int64_t xmin = state->xmin;
    uint32_t rsi = strm->rsi * strm->block_size - 1;
    int i;

    d[0] = (uint32_t)x[0];
    x[0] = (x[0] ^ m) - m;

    for (i = 0; i < rsi; i++) {
        x[i + 1] = (x[i + 1] ^ m) - m;
        if (x[i + 1] < x[i]) {
            D = (int64_t)x[i] - x[i + 1];
            if (D <= xmax - x[i])
                d[i + 1] = 2 * D - 1;
            else
                d[i + 1] = xmax - x[i + 1];
        } else {
            D = (int64_t)x[i + 1] - x[i];
            if (D <= x[i] - xmin)
                d[i + 1] = 2 * D;
            else
                d[i + 1] = x[i + 1] - xmin;
        }
    }
    state->ref = 1;
    state->uncomp_len = (strm->block_size - 1) * strm->bits_per_sample;
}

static inline uint64_t block_fs(struct aec_stream *strm, int k)
{
    /**
       Sum FS of all samples in block for given splitting position.
    */

    int i;
    uint64_t fs = 0;
    struct internal_state *state = strm->state;

    for (i = 0; i < strm->block_size; i++)
        fs += (uint64_t)(state->block[i] >> k);

    if (state->ref)
        fs -= (uint64_t)(state->block[0] >> k);

    return fs;
}

static uint32_t assess_splitting_option(struct aec_stream *strm)
{
    /**
       Length of CDS encoded with splitting option and optimal k.

       In Rice coding each sample in a block of samples is split at
       the same position into k LSB and bits_per_sample - k MSB. The
       LSB part is left binary and the MSB part is coded as a
       fundamental sequence a.k.a. unary (see CCSDS 121.0-B-2). The
       function of the length of the Coded Data Set (CDS) depending on
       k has exactly one minimum (see A. Kiely, IPN Progress Report
       42-159).

       To find that minimum with only a few costly evaluations of the
       CDS length, we start with the k of the previous CDS. K is
       increased and the CDS length evaluated. If the CDS length gets
       smaller, then we are moving towards the minimum. If the length
       increases, then the minimum will be found with smaller k.

       For increasing k we know that we will gain block_size bits in
       length through the larger binary part. If the FS lenth is less
       than the block size then a reduced FS part can't compensate the
       larger binary part. So we know that the CDS for k+1 will be
       larger than for k without actually computing the length. An
       analogue check can be done for decreasing k.
     */

    int k;
    int k_min;
    int this_bs; /* Block size of current block */
    int no_turn; /* 1 if we shouldn't reverse */
    int dir; /* Direction, 1 means increasing k, 0 decreasing k */
    uint64_t len; /* CDS length for current k */
    uint64_t len_min; /* CDS length minimum so far */
    uint64_t fs_len; /* Length of FS part (not including 1s) */

    struct internal_state *state = strm->state;

    this_bs = strm->block_size - state->ref;
    len_min = UINT64_MAX;
    k = k_min = state->k;
    no_turn = k == 0;
    dir = 1;

    for (;;) {
        fs_len = block_fs(strm, k);
        len = fs_len + this_bs * (k + 1);

        if (len < len_min) {
            if (len_min < UINT64_MAX)
                no_turn = 1;

            len_min = len;
            k_min = k;

            if (dir) {
                if (fs_len < this_bs || k >= state->kmax) {
                    if (no_turn)
                        break;
                    k = state->k - 1;
                    dir = 0;
                    no_turn = 1;
                } else {
                    k++;
                }
            } else {
                if (fs_len >= this_bs || k == 0)
                    break;
                k--;
            }
        } else {
            if (no_turn)
                break;
            k = state->k - 1;
            dir = 0;
            no_turn = 1;
        }
    }
    state->k = k_min;

    return len_min;
}

static uint32_t assess_se_option(struct aec_stream *strm)
{
    /**
       Length of CDS encoded with Second Extension option.

       If length is above limit just return UINT32_MAX.
    */

    int i;
    uint64_t d;
    uint32_t len;
    struct internal_state *state = strm->state;

    len = 1;

    for (i = 0; i < strm->block_size; i+= 2) {
        d = (uint64_t)state->block[i]
            + (uint64_t)state->block[i + 1];
        /* we have to worry about overflow here */
        if (d > state->uncomp_len) {
            len = UINT32_MAX;
            break;
        } else {
            len += d * (d + 1) / 2 + state->block[i + 1] + 1;
        }
    }
    return len;
}

static void init_output(struct aec_stream *strm)
{
    /**
       Direct output to next_out if next_out can hold a Coded Data
       Set, use internal buffer otherwise.
    */

    struct internal_state *state = strm->state;

    if (strm->avail_out > CDSLEN) {
        if (!state->direct_out) {
            state->direct_out = 1;
            *strm->next_out = *state->cds;
            state->cds = strm->next_out;
        }
    } else {
        if (state->zero_blocks == 0 || state->direct_out) {
            /* copy leftover from last block */
            *state->cds_buf = *state->cds;
            state->cds = state->cds_buf;
        }
        state->direct_out = 0;
    }
}

/*
 *
 * FSM functions
 *
 */

static int m_flush_block_resumable(struct aec_stream *strm)
{
    /**
       Slow and restartable flushing
    */
    struct internal_state *state = strm->state;

    int n = MIN(state->cds - state->cds_buf - state->i, strm->avail_out);
    memcpy(strm->next_out, state->cds_buf + state->i, n);
    strm->next_out += n;
    strm->avail_out -= n;
    state->i += n;

    if (strm->avail_out == 0) {
        return M_EXIT;
    } else {
        state->mode = m_get_block;
        return M_CONTINUE;
    }
}

static int m_flush_block(struct aec_stream *strm)
{
    /**
       Flush block in direct_out mode by updating counters.

       Fall back to slow flushing if in buffered mode.
    */
    int n;
    struct internal_state *state = strm->state;

#ifdef ENABLE_RSI_PADDING
    if (state->blocks_avail == 0
        && strm->flags & AEC_PAD_RSI
        && state->block_nonzero == 0
        )
        emit(state, 0, state->bits % 8);
#endif

    if (state->direct_out) {
        n = state->cds - strm->next_out;
        strm->next_out += n;
        strm->avail_out -= n;
        state->mode = m_get_block;
        return M_CONTINUE;
    }

    state->i = 0;
    state->mode = m_flush_block_resumable;
    return M_CONTINUE;
}

static int m_encode_splitting(struct aec_stream *strm)
{
    struct internal_state *state = strm->state;
    int k = state->k;

    emit(state, k + 1, state->id_len);

    if (state->ref)
        emit(state, state->block[0], strm->bits_per_sample);

    emitblock_fs(strm, k, state->ref);
    if (k)
        emitblock(strm, k, state->ref);

    return m_flush_block(strm);
}

static int m_encode_uncomp(struct aec_stream *strm)
{
    struct internal_state *state = strm->state;

    emit(state, (1U << state->id_len) - 1, state->id_len);
    emitblock(strm, strm->bits_per_sample, 0);

    return m_flush_block(strm);
}

static int m_encode_se(struct aec_stream *strm)
{
    int i;
    uint32_t d;
    struct internal_state *state = strm->state;

    emit(state, 1, state->id_len + 1);
    if (state->ref)
        emit(state, state->block[0], strm->bits_per_sample);

    for (i = 0; i < strm->block_size; i+= 2) {
        d = state->block[i] + state->block[i + 1];
        emitfs(state, d * (d + 1) / 2 + state->block[i + 1]);
    }

    return m_flush_block(strm);
}

static int m_encode_zero(struct aec_stream *strm)
{
    struct internal_state *state = strm->state;

    emit(state, 0, state->id_len + 1);

    if (state->zero_ref)
        emit(state, state->zero_ref_sample, strm->bits_per_sample);

    if (state->zero_blocks == ROS)
        emitfs(state, 4);
    else if (state->zero_blocks >= 5)
        emitfs(state, state->zero_blocks);
    else
        emitfs(state, state->zero_blocks - 1);

    state->zero_blocks = 0;
    return m_flush_block(strm);
}

static int m_select_code_option(struct aec_stream *strm)
{
    /**
       Decide which code option to use.
    */

    uint32_t split_len;
    uint32_t se_len;
    struct internal_state *state = strm->state;

    if (state->id_len > 1)
        split_len = assess_splitting_option(strm);
    else
        split_len = UINT32_MAX;
    se_len = assess_se_option(strm);

    if (split_len < state->uncomp_len) {
        if (split_len < se_len)
            return m_encode_splitting(strm);
        else
            return m_encode_se(strm);
    } else {
        if (state->uncomp_len <= se_len)
            return m_encode_uncomp(strm);
        else
            return m_encode_se(strm);
    }
}

static int m_check_zero_block(struct aec_stream *strm)
{
    /**
       Check if input block is all zero.

       Aggregate consecutive zero blocks until we find !0 or reach the
       end of a segment or RSI.
    */

    int i;
    struct internal_state *state = strm->state;
    uint32_t *p = state->block;

    for (i = state->ref; i < strm->block_size; i++)
        if (p[i] != 0)
            break;

    if (i < strm->block_size) {
        if (state->zero_blocks) {
            /* The current block isn't zero but we have to emit a
             * previous zero block first. The current block will be
             * flagged and handled later.
             */
            state->block_nonzero = 1;
            state->mode = m_encode_zero;
            return M_CONTINUE;
        }
        state->mode = m_select_code_option;
        return M_CONTINUE;
    } else {
        state->zero_blocks++;
        if (state->zero_blocks == 1) {
            state->zero_ref = state->ref;
            state->zero_ref_sample = state->block[0];
        }
        if (state->blocks_avail == 0
            || (strm->rsi - state->blocks_avail) % 64 == 0) {
            if (state->zero_blocks > 4)
                state->zero_blocks = ROS;
            state->mode = m_encode_zero;
            return M_CONTINUE;
        }
        state->mode = m_get_block;
        return M_CONTINUE;
    }
}

static int m_get_rsi_resumable(struct aec_stream *strm)
{
    /**
       Get RSI while input buffer is short.

       Let user provide more input. Once we got all input pad buffer
       to full RSI.
    */

    struct internal_state *state = strm->state;

    do {
        if (strm->avail_in >= state->bytes_per_sample) {
            state->data_raw[state->i] = state->get_sample(strm);
        } else {
            if (state->flush == AEC_FLUSH) {
                if (state->i > 0) {
                    state->blocks_avail = state->i / strm->block_size - 1;
                    if (state->i % strm->block_size)
                        state->blocks_avail++;
                    do
                        state->data_raw[state->i] =
                            state->data_raw[state->i - 1];
                    while(++state->i < strm->rsi * strm->block_size);
                } else {
                    /* Finish encoding by padding the last byte with
                     * zero bits. */
                    emit(state, 0, state->bits);
                    if (strm->avail_out > 0) {
                        if (!state->direct_out)
                            *strm->next_out++ = *state->cds;
                        strm->avail_out--;
                        state->flushed = 1;
                    }
                    return M_EXIT;
                }
            } else {
                return M_EXIT;
            }
        }
    } while (++state->i < strm->rsi * strm->block_size);

    if (strm->flags & AEC_DATA_PREPROCESS)
        state->preprocess(strm);

    return m_check_zero_block(strm);
}

static int m_get_block(struct aec_stream *strm)
{
    /**
       Provide the next block of preprocessed input data.

       Pull in a whole Reference Sample Interval (RSI) of data if
       block buffer is empty.
    */

    struct internal_state *state = strm->state;

    init_output(strm);

    if (state->block_nonzero) {
        state->block_nonzero = 0;
        state->mode = m_select_code_option;
        return M_CONTINUE;
    }

    if (state->blocks_avail == 0) {
        state->blocks_avail = strm->rsi - 1;
        state->block = state->data_pp;

        if (strm->avail_in >= state->rsi_len) {
            state->get_rsi(strm);
            if (strm->flags & AEC_DATA_PREPROCESS)
                state->preprocess(strm);

            return m_check_zero_block(strm);
        } else {
            state->i = 0;
            state->mode = m_get_rsi_resumable;
        }
    } else {
        if (state->ref) {
            state->ref = 0;
            state->uncomp_len = strm->block_size * strm->bits_per_sample;
        }
        state->block += strm->block_size;
        state->blocks_avail--;
        return m_check_zero_block(strm);
    }
    return M_CONTINUE;
}

static void cleanup(struct aec_stream *strm)
{
    struct internal_state *state = strm->state;

    if (strm->flags & AEC_DATA_PREPROCESS && state->data_raw)
        free(state->data_raw);
    if (state->data_pp)
        free(state->data_pp);
    free(state);
}

/*
 *
 * API functions
 *
 */

int aec_encode_init(struct aec_stream *strm)
{
    struct internal_state *state;

    if (strm->bits_per_sample > 32 || strm->bits_per_sample == 0)
        return AEC_CONF_ERROR;

    if (strm->block_size != 8
        && strm->block_size != 16
        && strm->block_size != 32
        && strm->block_size != 64)
        return AEC_CONF_ERROR;

    if (strm->rsi > 4096)
        return AEC_CONF_ERROR;

    state = malloc(sizeof(struct internal_state));
    if (state == NULL)
        return AEC_MEM_ERROR;

    memset(state, 0, sizeof(struct internal_state));
    strm->state = state;

    if (strm->bits_per_sample > 16) {
        /* 24/32 input bit settings */
        state->id_len = 5;

        if (strm->bits_per_sample <= 24
            && strm->flags & AEC_DATA_3BYTE) {
            state->bytes_per_sample = 3;
            if (strm->flags & AEC_DATA_MSB) {
                state->get_sample = aec_get_msb_24;
                state->get_rsi = aec_get_rsi_msb_24;
            } else {
                state->get_sample = aec_get_lsb_24;
                state->get_rsi = aec_get_rsi_lsb_24;
            }
        } else {
            state->bytes_per_sample = 4;
            if (strm->flags & AEC_DATA_MSB) {
                state->get_sample = aec_get_msb_32;
                state->get_rsi = aec_get_rsi_msb_32;
            } else {
                state->get_sample = aec_get_lsb_32;
                state->get_rsi = aec_get_rsi_lsb_32;
            }
        }
    }
    else if (strm->bits_per_sample > 8) {
        /* 16 bit settings */
        state->id_len = 4;
        state->bytes_per_sample = 2;

        if (strm->flags & AEC_DATA_MSB) {
            state->get_sample = aec_get_msb_16;
            state->get_rsi = aec_get_rsi_msb_16;
        } else {
            state->get_sample = aec_get_lsb_16;
            state->get_rsi = aec_get_rsi_lsb_16;
        }
    } else {
        /* 8 bit settings */
        if (strm->flags & AEC_RESTRICTED) {
            if (strm->bits_per_sample <= 4) {
                if (strm->bits_per_sample <= 2)
                    state->id_len = 1;
                else
                    state->id_len = 2;
            } else {
                return AEC_CONF_ERROR;
            }
        } else {
            state->id_len = 3;
        }
        state->bytes_per_sample = 1;

        state->get_sample = aec_get_8;
        state->get_rsi = aec_get_rsi_8;
    }
    state->rsi_len = strm->rsi * strm->block_size * state->bytes_per_sample;

    if (strm->flags & AEC_DATA_SIGNED) {
        state->xmin = -(1ULL << (strm->bits_per_sample - 1));
        state->xmax = (1ULL << (strm->bits_per_sample - 1)) - 1;
        state->preprocess = preprocess_signed;
    } else {
        state->xmin = 0;
        state->xmax = (1ULL << strm->bits_per_sample) - 1;
        state->preprocess = preprocess_unsigned;
    }

    state->kmax = (1U << state->id_len) - 3;

    state->data_pp = malloc(strm->rsi
                            * strm->block_size
                            * sizeof(uint32_t));
    if (state->data_pp == NULL) {
        cleanup(strm);
        return AEC_MEM_ERROR;
    }

    if (strm->flags & AEC_DATA_PREPROCESS) {
        state->data_raw = malloc(strm->rsi
                                 * strm->block_size
                                 * sizeof(uint32_t));
        if (state->data_raw == NULL) {
            cleanup(strm);
            return AEC_MEM_ERROR;
        }
    } else {
        state->data_raw = state->data_pp;
    }

    state->block = state->data_pp;

    strm->total_in = 0;
    strm->total_out = 0;
    state->flushed = 0;

    state->cds = state->cds_buf;
    *state->cds = 0;
    state->bits = 8;
    state->mode = m_get_block;

    return AEC_OK;
}

int aec_encode(struct aec_stream *strm, int flush)
{
    /**
       Finite-state machine implementation of the adaptive entropy
       encoder.
    */
    int n;
    struct internal_state *state = strm->state;

    state->flush = flush;
    strm->total_in += strm->avail_in;
    strm->total_out += strm->avail_out;

    while (state->mode(strm) == M_CONTINUE);

    if (state->direct_out) {
        n = state->cds - strm->next_out;
        strm->next_out += n;
        strm->avail_out -= n;

        *state->cds_buf = *state->cds;
        state->cds = state->cds_buf;
        state->direct_out = 0;
    }
    strm->total_in -= strm->avail_in;
    strm->total_out -= strm->avail_out;
    return AEC_OK;
}

int aec_encode_end(struct aec_stream *strm)
{
    struct internal_state *state = strm->state;
    int status;

    status = AEC_OK;
    if (state->flush == AEC_FLUSH && state->flushed == 0)
        status = AEC_STREAM_ERROR;
    cleanup(strm);
    return status;
}

int aec_buffer_encode(struct aec_stream *strm)
{
    int status;

    status = aec_encode_init(strm);
    if (status != AEC_OK)
        return status;
    status = aec_encode(strm, AEC_FLUSH);
    if (status != AEC_OK) {
        cleanup(strm);
        return status;
    }
    return aec_encode_end(strm);
}
