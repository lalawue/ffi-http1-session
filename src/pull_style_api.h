/* 
 * Copyright (c) 2020 lalawue
 * 
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 */

#ifndef _PULL_STYLE_API
#define _PULL_STYLE_API

#define HTTP_URL_LENGTH 8192

/* parsing state */
typedef enum {
   PROCESS_STATE_INVALID = 0,   /* not begin, or encouter error */
   PROCESS_STATE_BEGIN = 1,     /* begin message */
   PROCESS_STATE_HEAD = 2,      /* header data comlete */
   PROCESS_STATE_BODY = 3,      /* begin body data */
   PROCESS_STATE_FINISH = 4,    /* message finished */
} http_process_state_t;

/* http header field */
typedef struct s_http_head_kv {
   char *head_field;            /* field name */
   char *head_value;            /* value string */
   struct s_http_head_kv *next;      /* next field */
} http_head_kv_t;

/* http contents */
typedef struct s_http_data {
   unsigned char data[HTTP_URL_LENGTH]; /* content */
   int data_pos;                        /* partial  */
   struct s_http_data *next;                 /* next block */
} http_data_t;

typedef struct {
   http_process_state_t process_state; /* parsing state */
   const char *method;            /* 'GET', 'POST', ... */
   char url[HTTP_URL_LENGTH];     /* URL */
   uint16_t status_code;          /* HTTP response */
   http_head_kv_t *head_kv;            /* http header */
   http_data_t *content;               /* http content */
   unsigned int content_length;   /* chunked data cause it 0 */
   unsigned int readed_length;    /* all readed bytes */
   const char *err_msg;           /* error message */
   void *opaque;                  /* reserved for internal use */
} http_ctx_t;

typedef struct {
   unsigned major;
   unsigned minor;
   unsigned patch;
} http_version_t;

/* 0:request 1:response 2:both */
http_ctx_t* mhttp_parser_create(int parser_type);
void mhttp_parser_destroy(http_ctx_t *h);

/* return byte processed, -1 means error */
int mhttp_parser_process(http_ctx_t *h, char *data, int length);

/* in BODY process_state, you can consume data blocks, 
 * minimize the memory usage, and last block may be a 
 * partial one
 */
void mhttp_parser_consume_data(http_ctx_t *h, int count);

/* reset http parser */
void mhttp_parser_reset(http_ctx_t *h);

/* get http version */
void mhttp_parser_version(http_version_t *v);

#endif
