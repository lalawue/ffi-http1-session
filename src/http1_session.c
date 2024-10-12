/*
 * Copyright (c) 2024 lalawue
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "http_parser.h"
#include "m_prng.h"
#include "http1_session.h"
#include "WjCryptLib_Sha1.h"

#if defined(__linux__) || defined(__FreeBSD__) || defined(__APPLE__)
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

/* For Mingw build */
#if defined(_WIN32) || defined(_WIN64)
#include <winsock2.h>
#endif

const uint32_t _Z_DATA_LEN = 4 * 1024;

enum _opcode
{
    _WS_CONTINUATION_FRAME = 0x0u,
    _WS_TEXT_FRAME = 0x1u,
    _WS_BINARY_FRAME = 0x2u,
    _WS_CONNECTION_CLOSE = 0x8u,
    _WS_PING = 0x9u,
    _WS_PONG = 0xau
};

typedef struct
{
    struct
    {
        uint8_t fin : 1;
        uint8_t rsv1 : 1;
        uint8_t rsv2 : 1;
        uint8_t rsv3 : 1;

        uint8_t opcode : 4;
    } h1;

    struct
    {
        uint8_t mask : 1;
        uint8_t plen : 7;
    } h2;

    union
    {
        uint64_t plen64;
        uint16_t plen16;
    } u;

    uint8_t masking_key[4];
    uint64_t fr_pread; // frame payload readed
    int fr_stage;      // frame reading state, 0: head, 1: payload
} ws_t;

typedef enum
{
    SESSION_STAGE_INIT = 0,
    SESSION_STAGE_HTTP = 1,
    SESSION_STAGE_WS = 2
} session_stage_t;

typedef struct
{
    int server;
    prng_t rng;
    session_stage_t stage;
    http_parser hp;
    struct http_parser_settings hp_settings;
    ws_t ws;
    mssn_header_t *header_rlast; // last header for read, for append header value
    mssn_frame_t *frame_rlast;   // last frame for read, conjoin continuation frames
    mssn_data_t *data_read;      // data for read, last data node
    mssn_data_t *data_send;      // data for send
} session_t;

static inline uint64_t
_fr_plen(ws_t *wh)
{
    switch (wh->h2.plen)
    {
    case 127:
        return wh->u.plen64;
    case 126:
        return wh->u.plen16;
    default:
        return wh->h2.plen;
    }
}

static inline session_t *
_sctx(mssn_t *mctx)
{
    return (mctx == NULL) ? NULL : (session_t *)mctx->opaque;
}

static inline mssn_t *
_mctx(http_parser *hp)
{
    return (mssn_t *)hp->data;
}

#ifdef _HTTP_1_SESSION_DEBUG_MEM_USAGE_
static unsigned int z_count = 0;

static void *
_zalloc(unsigned items, unsigned size)
{
    void *p = calloc(1, items * size + sizeof(int));
    unsigned int *u = (unsigned int *)p;
    z_count += items * size;
    *u = items * size;
    return (void *)(u + 1);
}

static void
_zfree(void *address)
{
    if (address)
    {
        unsigned int *u = ((unsigned int *)address - 1);
        z_count -= *u;
        free((void *)u);
    }
}

#define _Z_REPORT(TAG)                                   \
    do                                                   \
    {                                                    \
        printf("[MSSN_MEM] %s left %u\n", TAG, z_count); \
    } while (0)
#define _Z_DEBUG(FMT, ...)                                   \
    do                                                       \
    {                                                        \
        printf("[MSSN_DEBUG] (%s:%d) ", __FILE__, __LINE__); \
        printf(FMT, ##__VA_ARGS__);                          \
        printf("\n");                                        \
    } while (0)
#else
static inline void *
_zalloc(unsigned items, unsigned size)
{
    return calloc(items, size);
}

static inline void
_zfree(void *address)
{
    if (address)
    {
        free(address);
    }
}

#define _Z_REPORT(TAG)
#define _Z_DEBUG(FMT, ARGS...)
#endif // _HTTP_1_SESSION_DEBUG_MEM_

static inline size_t
_zmin(size_t x, size_t y)
{
    return (x < y) ? x : y;
}

static inline uint64_t
_zswap64(uint64_t x)
{
    uint64_t u = ntohl(x & 0xffffffffllu);
    uint64_t l = ntohl((uint32_t)(x >> 32));
    return (u << 32) | l;
}

static mssn_data_t *
_zdata_alloc(const uint8_t *data, int data_len)
{
    if (data_len <= 0)
    {
        return NULL;
    }

    mssn_data_t *dt = (mssn_data_t *)_zalloc(1, sizeof(mssn_data_t) + data_len);
    if (dt == NULL)
    {
        return NULL;
    }

    dt->length = data_len;
    dt->data = ((uint8_t *)dt) + sizeof(mssn_data_t);

    //_Z_DEBUG("data alloc %p, %p", dt, dt->data);

    if (data != NULL)
    {
        memcpy(dt->data, data, data_len);
    }

    return dt;
}

static void
_zdata_free(mssn_data_t *dt)
{
    while (dt != NULL)
    {
        mssn_data_t *tmp = dt->next;
        _zfree(dt);
        dt = tmp;
    }
}

static void
_zframe_free(mssn_frame_t *fr)
{
    while (fr != NULL)
    {
        mssn_frame_t *tmp = fr->next;
        _zdata_free(fr->data_head);
        _zfree(fr);
        fr = tmp;
    }
}

static void _hp_init(mssn_t *);
static void _hp_fini(mssn_t *);
static void _ws_init(mssn_t *);
static void _ws_fini(mssn_t *);
static int _ws_opcode(int);
static int _ws_ftype(int);
static void _ws_genmask(session_t *sctx, uint8_t *buf);
// static void _dt_dump(mssn_data_t *dt);

// MARK: - Public

mssn_t *
mssn_create(int server)
{
    mssn_t *mctx = (mssn_t *)_zalloc(1, sizeof(mssn_t));
    session_t *sctx = (session_t *)_zalloc(1, sizeof(session_t));
    sctx->server = server;
    if (!server)
    {
        prng_init(&sctx->rng);
    }
    sctx->stage = SESSION_STAGE_INIT;
    mctx->opaque = sctx;
    _hp_init(mctx);
    return mctx;
}

void mssn_close(mssn_t *mctx)
{
    session_t *sctx = _sctx(mctx);
    if (sctx != NULL && sctx->stage != SESSION_STAGE_INIT)
    {
        sctx->stage = SESSION_STAGE_INIT;
        mssn_reclaim(mctx, NULL);
        _hp_fini(mctx);
        _ws_fini(mctx);
        _zfree(mctx->opaque);
        mctx->opaque = NULL;
        _zfree(mctx);
    }
    _Z_REPORT("mssn_close");
}

/** Web Socket Header
 0               1               2               3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-------+-+-------------+-------------------------------+
|F|R|R|R| opcode|M| Payload len |    Extended payload length    |
|I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
|N|V|V|V|       |S|             |   (if payload len==126/127)   |
| |1|2|3|       |K|             |                               |
+-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
|     Extended payload length continued, if payload len == 127  |
+ - - - - - - - - - - - - - - - +-------------------------------+
|                               |Masking-key, if MASK set to 1  |
+-------------------------------+-------------------------------+
| Masking-key (continued)       |          Payload Data         |
+-------------------------------- - - - - - - - - - - - - - - - +
:                     Payload Data continued ...                :
+ - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +
|                     Payload Data continued ...                |
+---------------------------------------------------------------+
*/
int mssn_process(mssn_t *mctx, const uint8_t *buf, int buf_len)
{
    //_Z_DEBUG("enter params %p, %p, %d", mctx, buf, buf_len);

    session_t *sctx = _sctx(mctx);
    if ((sctx == NULL) || (buf == NULL) || (buf_len <= 0))
    {
        _Z_DEBUG("invalid param");
        return -1;
    }

    int nread = 0;

    // http stage

    if (sctx->stage == SESSION_STAGE_INIT || sctx->stage == SESSION_STAGE_HTTP)
    {
        nread = http_parser_execute(&sctx->hp, &sctx->hp_settings, (const char *)buf, buf_len);
        if ((mctx->error_msg == NULL) && (sctx->hp.http_errno == 0))
        {
            _Z_DEBUG("http nread %d", nread);
            return nread;
        }
        else
        {
            if (mctx->error_msg == NULL)
            {
                mctx->error_msg = http_errno_name(sctx->hp.http_errno);
            }
            _Z_DEBUG("http err msg: %s", mctx->error_msg);
            return -1;
        }
    }

    // web socket stage

    ws_t *ws = &sctx->ws;

    // read whole frame header at once
    if (ws->fr_stage == 0)
    {
        int hlen = 2;
        if (buf_len < hlen)
        {
            _Z_DEBUG("buf_len < 2");
            return 0;
        }

        _Z_DEBUG("head hex 0x%02x 0x%02x", buf[0], buf[1]);

        ws->h1.fin = buf[0] >> 7;
        ws->h1.rsv1 = (0x40 & buf[0]) >> 6;
        ws->h1.rsv2 = (0x20 & buf[0]) >> 5;
        ws->h1.rsv3 = (0x10 & buf[0]) >> 4;
        ws->h1.opcode = 0xF & buf[0];

        ws->h2.mask = (0x80 & buf[1]) >> 7;
        ws->h2.plen = 0x7F & buf[1];

        if (ws->h2.mask == 1)
        {
            hlen += 4;
        }

        if (ws->h2.plen == 127)
        {
            hlen += 8;
        }
        else if (ws->h2.plen == 126)
        {
            hlen += 2;
        }

        // read whole header at once
        if (buf_len < hlen)
        {
            _Z_DEBUG("buf_len:%d < hlen:%d", buf_len, hlen);
            return 0;
        }

        if (ws->h2.mask && sctx->server)
        {
            memcpy(ws->masking_key, buf + hlen - 4, 4);
            _Z_DEBUG("masking 4 bytes 0x%02x 0x%02x 0x%02x 0x%02x", ws->masking_key[0], ws->masking_key[1], ws->masking_key[2], ws->masking_key[3]);
        }
        else if (ws->h2.mask != !!sctx->server)
        {
            mctx->error_msg = "masking-key not match";
            _Z_DEBUG("masking-key not match");
            return -1;
        }

        if (ws->h2.plen == 127)
        {
            ws->u.plen64 = _zswap64(*((uint64_t *)(buf + 2)));
        }
        else if (ws->h2.plen == 126)
        {
            ws->u.plen64 = ntohs(*((uint16_t *)(buf + 2)));
        }

        ws->fr_pread = 0;
        ws->fr_stage = 1;

        nread += hlen;
        buf_len -= hlen;

        _Z_DEBUG("hlen %d, opcode:%x, fin:%d, plen:%ld", hlen, ws->h1.opcode, ws->h1.fin, _fr_plen(ws));
    }

    // read frame payload
    while (buf_len > 0 || ws->h1.fin)
    {
        if (sctx->frame_rlast == NULL)
        {
            mssn_frame_t *fr = _zalloc(1, sizeof(mssn_frame_t));
            fr->ftype = _ws_ftype(ws->h1.opcode);
            fr->data_head = _zdata_alloc(NULL, _Z_DATA_LEN);
            fr->data_head->length = 0;

            sctx->frame_rlast = fr;
            sctx->data_read = fr->data_head;
        }

        mssn_data_t *dt = sctx->data_read;
        if (dt->length == _Z_DATA_LEN)
        {
            dt = _zdata_alloc(NULL, _Z_DATA_LEN);
            dt->length = 0;
            sctx->data_read->next = dt;
            sctx->data_read = dt;
        }

        if (buf_len > 0)
        {

            size_t mlen = _zmin(_zmin(_Z_DATA_LEN - dt->length, buf_len),
                                _fr_plen(ws) - ws->fr_pread);

            _Z_DEBUG("before mem copying, mlen %ld, buf_len %d", mlen, buf_len);

            if (ws->h2.mask)
            {
                _Z_DEBUG("masking mem copying");
                for (int i = 0; i < mlen; i++)
                    dt->data[dt->length + i] = buf[nread + i] ^ ws->masking_key[(ws->fr_pread + i) % 4];
            }
            else
            {
                _Z_DEBUG("no masking mem copying");
                memcpy(dt->data + dt->length, buf + nread, mlen);
            }

            nread += mlen;
            dt->length += mlen;
            ws->fr_pread += mlen;
            buf_len -= mlen;
        }

        // if reach playload end
        if (_fr_plen(ws) == ws->fr_pread)
        {
            ws->fr_stage = 0;
            if (ws->h1.fin)
            {
                //_dt_dump(sctx->frame_rlast->data_head);
                _zframe_free(mctx->frames);
                mctx->frames = sctx->frame_rlast;
                sctx->frame_rlast = NULL;
                sctx->data_read = NULL;
                break;
            }
        }
        else if (ws->h1.fin)
        {
            // should never reach here
            break;
        }
    }

    return nread;
}

mssn_data_t *
mssn_build(mssn_t *mctx,
           mssn_frame_type ftype,
           int rsv_bits,
           size_t frame_size,
           const uint8_t *buf,
           size_t buf_len)
{
    session_t *sctx = _sctx(mctx);

    // invalid params
    if ((sctx == NULL) || (frame_size <= 0))
    {
        mctx->error_msg = "invalid params";
        return NULL;
    }

    // invalid frame type
    if ((ftype < WS_FRAME_PING) || (ftype > WS_FRAME_BINARY))
    {
        mctx->error_msg = "invalid frame type";
        return NULL;
    }

    if ((ftype == WS_FRAME_TEXT || ftype == WS_FRAME_BINARY) && (buf == NULL || buf_len <= 0))
    {
        mctx->error_msg = "invalid params";
        return NULL;
    }

    // only support one control frame at one call, with payload size <= 125
    if ((ftype >= WS_FRAME_PING) && ((ftype <= WS_FRAME_CLOSE) && (buf_len > 125)))
    {
        mctx->error_msg = "control frame require buf_len <= 125";
        return NULL;
    }

    int masking = !sctx->server;
    int hlen = 2 + (masking ? 4 : 0);

    if (_zmin(buf_len, (frame_size - hlen)) <= 125)
    {
        // 7 bits payload
    }
    else if (_zmin(buf_len, (frame_size - hlen - 2)) <= (1 << 16))
    {
        hlen += 2; // 16 bits payload
    }
    else if (_zmin(buf_len, (frame_size - hlen - 8)) < (1ull << 63))
    {
        hlen += 8; // 64 bits payload
    }
    else
    {
        mctx->error_msg = "invalid payload length";
        return NULL; // invalid payload length
    }

    _Z_DEBUG("build ftype:%d, hlen: %d, masking %d", ftype, hlen, masking);

    mssn_data_t *head = NULL;
    mssn_data_t *last = NULL;
    int is_ctrl = (ftype == WS_FRAME_PING) || (ftype == WS_FRAME_PONG) || (ftype == WS_FRAME_CLOSE);

    for (int bi = 0; (buf_len > 0) || (is_ctrl); bi++)
    {
        const size_t plen = _zmin(buf_len, frame_size - hlen);

        mssn_data_t *dt = _zdata_alloc(NULL, hlen + plen);
        if (head == NULL)
        {
            head = dt;
            last = dt;
        }
        else
        {
            last->next = dt;
            last = dt;
        }

        const int fin = plen <= buf_len;

        dt->data[0] |= (fin << 7) & 0x80;
        dt->data[0] |= (rsv_bits & 0x7) << 4;
        if ((bi == 0) || fin)
        {
            dt->data[0] |= _ws_opcode(ftype) & 0xF;
        }
        else
        {
            dt->data[0] |= _WS_CONTINUATION_FRAME;
        }

        dt->data[1] |= (masking << 7) & 0x80;
        if (plen <= 125)
        {
            dt->data[1] |= plen & 0x7F;
        }
        else if (plen < (1 << 16))
        {
            dt->data[1] |= 126;
            uint16_t tmp_len = htons((uint16_t)plen);
            memcpy(dt->data + 2, &tmp_len, 2);
        }
        else
        {
            dt->data[1] |= 127;
            uint64_t tmp_len = _zswap64(plen);
            memcpy(dt->data + 2, &tmp_len, 8);
        }

        if (buf_len <= 0)
        {
            break;
        }

        _Z_DEBUG("build index %d: plen %ld, buf_len %ld", bi, plen, buf_len);

        if (masking)
        {
            uint8_t maskey[4];
            _ws_genmask(sctx, maskey);
            memcpy(dt->data + hlen - 4, maskey, 4);
            for (int i = 0; i < plen; i++)
            {
                dt->data[hlen + i] = buf[i] ^ maskey[i % 4];
            }
        }
        else
        {
            memcpy(dt->data + hlen, buf, plen);
        }

        buf += plen;
        buf_len -= plen;
    }

    return head;
}

void mssn_reclaim(mssn_t *mctx, mssn_data_t *data_build)
{
    session_t *sctx = _sctx(mctx);
    mctx->opaque = sctx;
    mctx->error_msg = NULL;

    if (data_build)
    {
        _zdata_free(data_build);
        return;
    }

    // clear frames
    _zframe_free(mctx->frames);
    _zframe_free(sctx->frame_rlast);
    mctx->frames = NULL;
    sctx->frame_rlast = NULL;

    _zdata_free(sctx->data_send);
    sctx->data_send = NULL;
    sctx->data_read = NULL; // free in sctx->frame_rlast

    if (sctx->stage == SESSION_STAGE_WS)
    {
        // return for WebSocket connection
        _Z_REPORT("mssn_reclaim ws");
        return;
    }

    mctx->state = MSSN_STATE_INIT;
    mctx->method = NULL;

    // clear path
    _zfree((void *)mctx->path);
    mctx->path = NULL;
    mctx->status = 0;

    // clear headers
    mssn_header_t *n = mctx->headers;
    while (n != NULL)
    {
        mssn_header_t *tmp = n->next;
        _zfree((void *)n->key);
        _zfree((void *)n->value);
        _zfree(n);
        n = tmp;
    }
    mctx->headers = NULL;
    sctx->header_rlast = NULL;
    _Z_REPORT("mssn_reclaim http");
}

void mssn_sha1(const uint8_t *data, int data_len, uint8_t *digest)
{
    SHA1_HASH hash;
    Sha1Calculate(data, data_len, &hash);
    memcpy(digest, hash.bytes, 20);
}

// MARK: - HTTP Session

static int
_hp_msg_begin(http_parser *p)
{
    mssn_t *mctx = _mctx(p);
    mctx->state = MSSN_STATE_BEGIN;
    _sctx(mctx)->stage = SESSION_STAGE_HTTP;
    return 0;
}

static int
_hp_status(http_parser *p, const char *at, size_t length)
{
    return 0;
}

static int
_hp_headers_complete(http_parser *p)
{
    mssn_t *mctx = _mctx(p);

    if (p->method == 0xff)
    {
        mctx->method = NULL;
    }
    else
    {
        mctx->method = http_method_str(p->method);
    }

    mctx->status = p->status_code;
    mctx->state = MSSN_STATE_HEADER;

    if (!p->upgrade || (mctx->headers == NULL))
    {
        return 0;
    }

    int has_version = 0;
    mssn_header_t *h = mctx->headers;
    do
    {
        mssn_header_t *n = h->next;
        if ((strncmp(h->key, "Sec-WebSocket-Version", 21) == 0) &&
            (strncmp(h->value, "13", 2) == 0))
        {
            has_version = 1;
            break;
        }
        h = n;
    } while (h);

    if (has_version)
    {
        mctx->upgrade = 1;
        mctx->state = MSSN_STATE_BODY;
        _ws_init(mctx);
    }
    else
    {
        mctx->error_msg = "Invalid websocket version !";
    }
    return 0;
}

static int
_hp_msg_complete(http_parser *p)
{
    mssn_t *mctx = _mctx(p);
    mctx->state = MSSN_STATE_FINISH;
    return 0;
}

static int
_hp_url(http_parser *p, const char *at, size_t length)
{
    mssn_t *mctx = _mctx(p);
    if (mctx->path)
    {
        _zfree((void *)mctx->path);
    }
    mctx->path = _zalloc(1, length + 1);
    memcpy((char *)mctx->path, at, length);
    return 0;
}

static int
_hp_header_field(http_parser *p, const char *at, size_t length)
{
    mssn_header_t *h = _zalloc(1, sizeof(mssn_header_t));
    h->key = _zalloc(1, length + 1);
    memcpy((char *)h->key, at, length);

    mssn_t *mctx = _mctx(p);
    if (mctx->headers == NULL)
    {
        mctx->headers = h;
    }

    session_t *sctx = _sctx(mctx);
    if (sctx->header_rlast != NULL)
    {
        sctx->header_rlast->next = h;
    }
    sctx->header_rlast = h;

    return 0;
}

static int
_hp_header_value(http_parser *p, const char *at, size_t length)
{
    mssn_t *mctx = _mctx(p);
    mssn_header_t *h = _sctx(mctx)->header_rlast;
    if (h != NULL)
    {
        h->value = _zalloc(1, length + 1);
        memcpy((char *)h->value, at, length);
    }
    return 0;
}

static int
_hp_chunk_header(http_parser *p)
{
    return 0;
}

static int
_hp_body(http_parser *p, const char *at, size_t length)
{
    mssn_t *mctx = _mctx(p);
    session_t *sctx = _sctx(mctx);

    mssn_frame_t *fr = mctx->frames;
    if (fr == NULL)
    {
        fr = _zalloc(1, sizeof(mssn_frame_t));
        fr->ftype = HTTP_FRAME_BODY;
        mctx->frames = fr;
        sctx->frame_rlast = NULL;
    }

    if (fr->data_head == NULL)
    {
        fr->data_head = _zdata_alloc((uint8_t *)at, length);
        fr->data_last = fr->data_head;
    }
    else
    {
        fr->data_last->next = _zdata_alloc((uint8_t *)at, length);
        fr->data_last = fr->data_last->next;
    }

    return 0;
}

static int
_hp_chunk_complete(http_parser *p)
{
    return 0;
}

static void
_hp_init(mssn_t *mctx)
{
    session_t *sctx = _sctx(mctx);
    // http parser
    http_parser_init(&sctx->hp, HTTP_BOTH);
    sctx->hp.data = mctx;
    sctx->hp.method = 0xFF;
    sctx->hp.status_code = 0xFFFF;
    sctx->hp.content_length = 0;
    // http settins
    http_parser_settings_init(&sctx->hp_settings);
    sctx->hp_settings.on_message_begin = _hp_msg_begin;
    sctx->hp_settings.on_url = _hp_url;
    sctx->hp_settings.on_header_field = _hp_header_field;
    sctx->hp_settings.on_header_value = _hp_header_value;
    sctx->hp_settings.on_headers_complete = _hp_headers_complete;
    sctx->hp_settings.on_body = _hp_body;
    sctx->hp_settings.on_message_complete = _hp_msg_complete;
    sctx->hp_settings.on_status = _hp_status;
    sctx->hp_settings.on_chunk_header = _hp_chunk_header;
    sctx->hp_settings.on_chunk_complete = _hp_chunk_complete;
}

static void
_hp_fini(mssn_t *mctx)
{
}

// MARK: - WebSocket Session

static void
_ws_genmask(session_t *sctx, uint8_t *buf)
{
    uint64_t n = prng_next(&sctx->rng);
    memcpy(buf, &n, 4);
}

static void
_ws_init(mssn_t *mctx)
{
    session_t *sctx = _sctx(mctx);
    if (sctx->stage >= SESSION_STAGE_WS)
    {
        return;
    }
    sctx->stage = SESSION_STAGE_WS;
}

static void
_ws_fini(mssn_t *mctx)
{
    // session_t *sctx = _sctx(mctx);
    // if ((sctx->stage >= SESSION_STAGE_WS) && sctx->ws_ptr)
    // {
    //     wslay_frame_context_free(sctx->ws_ptr);
    //     sctx->ws_ptr = NULL;
    // }
}

static int
_ws_ftype(int opcode)
{
    switch (opcode)
    {
    case _WS_CONNECTION_CLOSE:
        return WS_FRAME_CLOSE;
    case _WS_TEXT_FRAME:
        return WS_FRAME_TEXT;
    case _WS_BINARY_FRAME:
        return WS_FRAME_BINARY;
    case _WS_PING:
        return WS_FRAME_PING;
    case _WS_PONG:
        return WS_FRAME_PONG;
    }
    return -1;
}

static int
_ws_opcode(int ftype)
{
    switch (ftype)
    {
    case WS_FRAME_CLOSE:
        return _WS_CONNECTION_CLOSE;
    case WS_FRAME_TEXT:
        return _WS_TEXT_FRAME;
    case WS_FRAME_BINARY:
        return _WS_BINARY_FRAME;
    case WS_FRAME_PING:
        return _WS_PING;
    case WS_FRAME_PONG:
        return _WS_PONG;
    }
    return -1;
}

// static void
// _dt_dump(mssn_data_t *dt)
// {
//     if (dt == NULL)
//     {
//         return;
//     }
//     printf("000: ");
//     int l = 1;
//     int ii = 0;
//     do
//     {
//         mssn_data_t *ndt = dt->next;
//         for (int di = 0; di < dt->length; di++, ii++)
//         {
//             if (ii > 0 && (ii % 16) == 0)
//             {
//                 l += 1;
//                 printf("\n%03d: ", l);
//             }
//             printf("%02x ", dt->data[di]);
//         }
//         if (ii > 0 && (ii % 15) != 0)
//         {
//             printf("\n");
//         }
//         dt = ndt;
//     } while (dt != NULL);
//     if (ii % 15 != 1)
//     {
//         printf("\n");
//     }
// }

#undef _Z_EREPORT
#undef _Z_DEBUG