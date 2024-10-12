/*
 * Copyright (c) 2024 lalawue
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 */

#ifndef _HTTP_1_SESSION_H_
#define _HTTP_1_SESSION_H_

#include <stddef.h>
#include <stdint.h>

//#define _HTTP_1_SESSION_DEBUG_MEM_USAGE_ 0

typedef struct s_mssn_data
{
    int length;
    struct s_mssn_data *next;
    uint8_t *data;
} mssn_data_t;

typedef struct s_mssn_header
{
    const char *key;
    const char *value;
    struct s_mssn_header *next;
} mssn_header_t;

typedef enum
{
    HTTP_FRAME_BODY = 1, // HTTP body data
    WS_FRAME_PING = 2,   // WS ping frame
    WS_FRAME_PONG = 3,   // WS pong frame
    WS_FRAME_CLOSE = 4,  // WS close frame
    WS_FRAME_TEXT = 5,   // WS text frame
    WS_FRAME_BINARY = 6, // WS binary frame
} mssn_frame_type;

typedef struct s_mssn_frame
{
    mssn_frame_type ftype;     // frame type
    mssn_data_t *data_head;    // data head, unmasking
    mssn_data_t *data_last;    // data last
    struct s_mssn_frame *next; // next frame
} mssn_frame_t;

typedef enum
{
    MSSN_STATE_INIT = 0,
    MSSN_STATE_BEGIN,  // has begin process data
    MSSN_STATE_HEADER, // header complete
    MSSN_STATE_BODY,   // begin body/frame data
    MSSN_STATE_FINISH, // HTTP body complete, WEBSOCKET frame closed
    MSSN_STATE_ERROR,  // parsing error, you should close context
} mssn_state_t;

typedef struct
{
    mssn_state_t state;     // state for last processing
    const char *method;     // method, nil meens HTTP response
    const char *path;       // path, nil meens HTTP response
    int status;             // http response status code
    int upgrade;            // upgrade to websocket
    mssn_header_t *headers; // header data for last process
    mssn_frame_t *frames;   // frames data for last process
    const char *error_msg;  // error message for last process
    void *opaque;           // internal use
} mssn_t;

/// @brief create context
/// @param server non-zero for server
/// @return context
mssn_t *mssn_create(int server);

/// @brief close context
void mssn_close(mssn_t *ctx);

/// @brief output frames in mssn_t, with error in mssn_t's error_msg
/// @param buf raw data
/// @param buf_len data length
/// - return = 0, require more data
/// - return > 0, parsed bytes
/// - return < 0, encounter error
int mssn_process(mssn_t *, const uint8_t *buf, int buf_len);

/// @brief build websocket binary frame data, data will be fragment but control frame
/// @param ctx context
/// @param ftype websocket frame type
/// @param frame_size max frame size including websocket header
/// @param rsv rsv 3 bits
/// @param buf data to be send
/// @param buf_len data length
/// @return every mssn_data_t is a compact websocket frame
mssn_data_t *mssn_build(mssn_t *ctx,
                        mssn_frame_type ftype,
                        int rsv_bits,
                        size_t frame_size,
                        const uint8_t *buf,
                        size_t buf_len);

/// @brief reclaim frames, headers, datas if needed
/// @param ctx context
/// @param data_build data from mssn_build
void mssn_reclaim(mssn_t *ctx, mssn_data_t *data_build);

/// @brief sha1 digest
void mssn_sha1(const uint8_t *data, int data_len, uint8_t *digest);

#endif // _HTTP_1_SESSION_H_
