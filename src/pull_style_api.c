/* 
 * Copyright (c) 2020 lalawue
 * 
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "http_parser.h"
#include "pull_style_api.h"

typedef struct {
   http_parser parser;
   struct http_parser_settings settings;
   char is_field;
   int buf_pos;
   http_data_t *last_data;
   unsigned char buf[HTTP_URL_LENGTH];
} ctx_t;

static inline http_ctx_t*
_http(http_parser *p) {
   return (http_ctx_t *)p->data;
}

static inline ctx_t*
_ctx(http_ctx_t *h) {
   return (ctx_t *)h->opaque;
}

/* 
 */
static int
_msgbegin(http_parser* p) {
   http_ctx_t *h = _http(p);
   h->process_state = PROCESS_STATE_BEGIN;
   return 0;
}

static int
_status(http_parser* p, const char *at, size_t length) {
   return 0;
}

static char*
_copy_header_kv(ctx_t *ctx) {
   if (ctx->buf_pos > 0) {
      char *buf = (char *)calloc(1, ctx->buf_pos + 1);
      strncpy(buf, (char *)ctx->buf, ctx->buf_pos);
      ctx->buf_pos = 0;
      return buf;
   }
   return NULL;
}

static int
_headerscomplete(http_parser* p) {
   http_ctx_t *h = _http(p);
   ctx_t *ctx = _ctx(h);
   h->process_state = PROCESS_STATE_HEAD;
   h->method = http_method_str(p->method);
   h->status_code = p->status_code;
   if (p->content_length < ULLONG_MAX) {
      h->content_length = (unsigned int)p->content_length;
   } else {
      h->content_length = 0;
   }
   if (ctx->buf_pos > 0) {
      http_head_kv_t *kv = h->head_kv;
      kv->head_value = _copy_header_kv(ctx);
   }
   return 0;
}

static int
_msgcomplete(http_parser* p) {
   http_ctx_t *h = _http(p);
   h->process_state = PROCESS_STATE_FINISH;
   return 0;
}

static int
_url(http_parser* p, const char *at, size_t length) {
   http_ctx_t *h = _http(p);
   memset(h->url, 0, HTTP_URL_LENGTH);
   strncpy(h->url, at, length);
   return 0;
}

static int
_headerfield(http_parser* p, const char *at, size_t length) {
   http_ctx_t *h = _http(p);
   ctx_t *ctx = _ctx(h);
   if (!ctx->is_field) {
      ctx->is_field = 1;
      if (ctx->buf_pos > 0) {
         http_head_kv_t *kv = h->head_kv;
         kv->head_value = _copy_header_kv(ctx);
      }
   }
   memcpy(&ctx->buf[ctx->buf_pos], at, length);
   ctx->buf_pos += length;
   return 0;
}

static int
_headervalue(http_parser* p, const char *at, size_t length) {
   http_ctx_t *h = _http(p);
   ctx_t *ctx = _ctx(h);
   if (ctx->is_field) {
      ctx->is_field = 0;
      if (ctx->buf_pos > 0) {
         http_head_kv_t *kv = (http_head_kv_t *)calloc(1, sizeof(http_head_kv_t));
         kv->next = h->head_kv;
         h->head_kv = kv;
         kv->head_field = _copy_header_kv(ctx);
      }
   }
   memcpy(&ctx->buf[ctx->buf_pos], at, length);
   ctx->buf_pos += length;
   return 0;
}

static int
_chunkheader(http_parser* p) {
   http_ctx_t *h = _http(p);
   h->content_length = 0;
   return 0;
}

static int
_chunkcomplete(http_parser* p) {
   return 0;
}

static inline http_data_t*
_next_data(http_ctx_t *h) {
   ctx_t *ctx= _ctx(h);      
   http_data_t *data = h->content;
   if (!data) {
      data = (http_data_t *)calloc(1, sizeof(http_data_t));
      memset(data, 0, sizeof(http_data_t));
      h->content = data;
      ctx->last_data = data;
   } else if (data->data_pos >= HTTP_URL_LENGTH) {
      data = (http_data_t *)calloc(1, sizeof(http_data_t));
      memset(data, 0, sizeof(http_data_t));
      ctx->last_data->next = data;
      ctx->last_data = data;
   }
   return data;
}

static inline int
_copy_length(http_data_t *data, size_t input_length) {
   int left_size = HTTP_URL_LENGTH - data->data_pos;
   return left_size < input_length ? left_size : input_length;
}

static int
_body(http_parser* p, const char *at, size_t length) {
   http_ctx_t *h = _http(p);
   int input_pos = 0;
   unsigned char *input = (unsigned char *)at;
   h->process_state = PROCESS_STATE_BODY;
   do {
      http_data_t *data = _next_data(h);
      int copy_length = _copy_length(data, length - input_pos);
      memcpy(&data->data[data->data_pos], &input[input_pos], copy_length);
      data->data_pos += copy_length;
      input_pos += copy_length;
   } while (input_pos < length);
   return 0;
}

/* return byte processed, -1 means error */
int
mhttp_parser_process(http_ctx_t *h, char *data, int length) {
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

static void
_http_init(http_ctx_t *h, ctx_t *ctx, int parser_type) {
   memset(h, 0, sizeof(*h));
   memset(ctx, 0, sizeof(*ctx));
   /* initialize */
   h->opaque = ctx;
   /* parser */
   http_parser_init(&ctx->parser, (enum http_parser_type)parser_type);
   ctx->parser.data = h;
   ctx->parser.method = 0xFF;
   ctx->parser.status_code = 0xFFFF;
   ctx->parser.content_length = 0;
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
}

http_ctx_t*
mhttp_parser_create(int parser_type) {
   http_ctx_t *h = (http_ctx_t *)calloc(1, sizeof(*h));
   ctx_t *ctx = (ctx_t *)calloc(1, sizeof(*ctx));
   _http_init(h, ctx, parser_type);
   return h;
}

static void
_free_kv(http_head_kv_t *kv) {
   while (kv) {
      if (kv->head_field) {
         free(kv->head_field);
      }
      if (kv->head_value) {
         free(kv->head_value);
      }
      kv = kv->next;
   }
}

static http_data_t*
_free_data(http_data_t *head, int count) {
   http_data_t *next = NULL;
   for (int i=0; head && i<count; i++) {
      next = head->next;
      free(head);
      head = next;
   }
   return head;
}

void
mhttp_parser_destroy(http_ctx_t *h) {
   if (h) {
      _free_kv(h->head_kv);
      _free_data(h->content, INT_MAX);
      free(h->opaque);
      free(h);
   }
}

void
mhttp_parser_consume_data(http_ctx_t *h, int count) {
   if (h && count > 0) {
      ctx_t *ctx = _ctx(h); 
      h->content = _free_data(h->content, count);
      ctx->last_data = h->content ? ctx->last_data : NULL;
   }
}

void
mhttp_parser_reset(http_ctx_t *h) {
   if (h) {
      ctx_t *ctx = _ctx(h);
      _free_kv(h->head_kv);
      _free_data(h->content, INT_MAX);
      _http_init(h, ctx, ctx->parser.type);
   }
}

void
mhttp_parser_version(http_version_t *v) {
   if (v) {
      unsigned long version = http_parser_version();
      v->major = (version >> 16) & 255;
      v->minor = (version >> 8) & 255;
      v->patch = version & 255;
   }
}

#ifdef TEST_PULL_STYLE_API
static char*
get_content(char *path, size_t *size) {
   FILE *fp = fopen(path, "rb");
   fseek(fp, 0, SEEK_END);
   *size = ftell(fp);
   char *data = calloc(1, *size + 1);
   fseek(fp, 0, SEEK_SET);
   fread(data, *size+1, 1, fp);
   fclose(fp);
   return data;
}
int main(int argc, char *argv[]) {
   if (argc < 3) {
      printf("%s STYLE FILE", argv[0]);
      return 0;
   }
   int style = atoi(argv[1]);
   size_t size = 0;
   char *data = get_content(argv[2], &size);
   printf("--%ld --\n", size);
   http_ctx_t *h = mhttp_parser_create(style);
   int nread = mhttp_parser_process(h, data, size);
   if (nread > 0) {
      printf("nread: %d\n", nread);
      printf("method: %s\n", h->method);      
      printf("process-state:%d, content-length:%d, readed:%d\n",
             h->process_state,
             h->content_length,
             h->readed_length);
      printf("url: %s\n", h->url);
      printf("status: %d\n", h->status_code);
      printf("---- header fields ---\n");
      http_head_kv_t *kv = NULL;
      for ( kv=h->head_kv; kv; kv=kv->next) {
         printf("%s : %s\n", kv->head_field, kv->head_value);
      }
      printf("---- data:%d ---\n", h->content_length);
      http_data_t *data = NULL;
      for ( data=h->content; data; data=data->next) {
         printf("%s", data->data);
      }
   } else {
      printf("http_errno: %s\n", h->err_msg);
   }
   mhttp_parser_destroy(h);
   return 0;
}
#endif  /* TEST_PULL_STYLE_API */
