/* 
 * Copyright (c) 2020 lalawue
 * 
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "http_parser.h"
#include "pull_style_api.h"

typedef struct {
   http_parser parser;
   struct http_parser_settings settings;   
   unsigned char buf[HTTP_URL_LENGTH];
   int buf_idx;
} ctx_t;

static inline http_t*
_http(http_parser *p) {
   return (http_t *)p->data;
}

static inline ctx_t*
_ctx(http_t *h) {
   return (ctx_t *)h->opaque;
}

/* 
 */
static int
_msgbegin(http_parser* p) {
   http_t *h = _http(p);
   h->process_state = PROCESS_STATE_HEAD;
   return 0;
}

static int
_status(http_parser* p, const char *at, size_t length) {
   return 0;
}

static int
_headerscomplete(http_parser* p) {
   http_t *h = _http(p);
   h->process_state = PROCESS_STATE_BODY;
   h->status_code = p->status_code;   
   h->content_length = p->content_length;
   return 0;
}

static int
_msgcomplete(http_parser* p) {
   http_t *h = _http(p);
   h->process_state = PROCESS_STATE_FINISH;
   return 0;
}

static int
_url(http_parser* p, const char *at, size_t length) {
   http_t *h = _http(p);
   memset(h->url, 0, HTTP_URL_LENGTH);
   strncpy(h->url, at, length);
   return 0;
}

static int
_headerfield(http_parser* p, const char *at, size_t length) {
   http_t *h = _http(p);
   head_kv_t *kv = (head_kv_t *)calloc(1, sizeof(head_kv_t));
   kv->next = h->head_kv;
   h->head_kv = kv;
   kv->head_field = (char *)calloc(1, length + 1);
   strncpy(kv->head_field, at, length);
   return 0;
}

static int
_headervalue(http_parser* p, const char *at, size_t length) {
   http_t *h = _http(p);
   head_kv_t *kv = h->head_kv;
   kv->head_value = (char *)calloc(1, length + 1);
   strncpy(kv->head_value, at, length);
   return 0;
}

static int
_chunkheader(http_parser* p) {
   return 0;
}

static int
_chunkcomplete(http_parser* p) {
   return 0;
}

static inline data_t*
_next_data(http_t *h) {
   data_t *data = h->content;
   if (!data) {
      data = (data_t *)malloc(sizeof(data_t));
      memset(data, 0, sizeof(data_t));
      h->content = data;
   } else if (data->data_pos >= HTTP_URL_LENGTH) {
      data = (data_t *)malloc(sizeof(data_t));
      memset(data, 0, sizeof(data_t));
      data->next = h->content;
      h->content = data;
   }
   return data;
}

static inline int
_copy_length(data_t *data, size_t input_length) {
   int left_size = HTTP_URL_LENGTH - data->data_pos;
   return left_size < input_length ? left_size : input_length;
}

static int
_body(http_parser* p, const char *at, size_t length) {
   http_t *h = _http(p);
   int input_pos = 0;
   unsigned char *input = (unsigned char *)at;
   do {
      data_t *data = _next_data(h);
      int copy_length = _copy_length(data, length - input_pos);
      memcpy(&data->data[data->data_pos], &input[input_pos], copy_length);
      data->data_pos += copy_length;
      input_pos += copy_length;
   } while (input_pos < length);
   return 0;
}

/* return byte processed, -1 means error */
int
mhttp_parser_process(http_t *h, char *data, int length) {
   if (h && data && length > 0) {
      ctx_t *ctx= _ctx(h);
      int nparsed = http_parser_execute(&ctx->parser, &ctx->settings, data, length);
      if (ctx->parser.http_errno == 0) {
         h->readed_length += nparsed;
         return nparsed;
      } else {
         h->err_msg = http_errno_name(ctx->parser.http_errno);
      }
   }
   return -1;
}

http_t*
mhttp_parser_create(int parser_type) {
   http_t *h = (http_t *)calloc(1, sizeof(*h));
   ctx_t *ctx = (ctx_t *)calloc(1, sizeof(*ctx));
   /* initialize */
   h->opaque = ctx;
   /* parser */
   http_parser_init(&ctx->parser, (enum http_parser_type)parser_type);
   ctx->parser.data = h;
   /* settings */
   http_parser_settings_init(&ctx->settings);
   ctx->settings.on_message_begin = _msgbegin;
   ctx->settings.on_url = _url;
   ctx->settings.on_header_field = _headerfield;
   ctx->settings.on_header_value = _headervalue;
   ctx->settings.on_headers_complete = _headerscomplete;
   ctx->settings.on_body = _body;
   ctx->settings.on_message_complete = _msgcomplete;
   ctx->settings.on_status = _status;   
   ctx->settings.on_chunk_header = _chunkheader;
   ctx->settings.on_chunk_complete = _chunkcomplete;
   return h;
}

void
mhttp_parser_destroy(http_t *h) {
   if (h) {
      head_kv_t *kv = h->head_kv;
      while (kv) {
         if (kv->head_field) {
            free(kv->head_field);
         }
         if (kv->head_value) {
            free(kv->head_value);
         }
         kv = kv->next;
      }
      data_t *data = h->content;
      while (data) {
         data_t *next = data->next;
         free(data);
         data = next;
      }
      free(h->opaque);
      free(h);
   }
}

#ifdef TEST
int main(int argc, char *argv[]) {
   if (argc < 3) {
      return 0;
   }
   int style = atoi(argv[1]);
   char *data = argv[2];
   printf("--%d --\n", (int)strlen(data));
   http_t *h = mhttp_parser_create(style);
   if (mhttp_parser_process(h, data, strlen(data)-1) > 0) {
      printf("state: %d, %d\n", h->process_state, h->readed_length);
      printf("url: %s\n", h->url);
      printf("status: %d\n", h->status_code);
      printf("---- header fields ---\n");
      for (head_kv_t *kv=h->head_kv; kv; kv=kv->next) {
         printf("%s : %s\n", kv->head_field, kv->head_value);
      }
      printf("---- data:%d ---\n", h->content_length);
      for (data_t *data=h->content; data; data=data->next) {
         printf("%s", data->data);
      }
   } else {
      printf("http_errno: %s\n", h->err_msg);
   }
   mhttp_parser_destroy(h);
   return 0;
}
#endif