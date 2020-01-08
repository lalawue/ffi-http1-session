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

static inline ctx_t*
_ctx(http_t *h) {
   return (ctx_t *)h->opaque;
}

/* 
 */
static int
_msgbegin(http_parser* p) {
   return 0;
}

static int
_status(http_parser* p, const char *at, size_t length) {
   for (int i=0; i<length; i++) {
      printf("%c", at[i]);
   }
   printf("\n");
   return 0;
}

static int
_headerscomplete(http_parser* p) {
   return 0;
}

static int
_msgcomplete(http_parser* p) {
   return 0;
}

static int
_url(http_parser* p, const char *at, size_t length) {
   for (int i=0; i<length; i++) {
      printf("%c", at[i]);
   }
   printf("\n");   
   return 0;
}

static int
_headerfield(http_parser* p, const char *at, size_t length) {
   for (int i=0; i<length; i++) {
      printf("%c", at[i]);
   }
   printf("\n");   
   return 0;
}

static int
_headervalue(http_parser* p, const char *at, size_t length) {
   for (int i=0; i<length; i++) {
      printf("%c", at[i]);
   }
   printf("\n");   
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

static int
_body(http_parser* p, const char *at, size_t length) {
   for (int i=0; i<length; i++) {
      printf("%c", at[i]);
   }
   return 0;
}

/* return byte processed, -1 means error */
int
mhttp_parser_process(http_t *h, char *data, int length) {
   if (h && data && length > 0) {
      ctx_t *ctx= _ctx(h);
      int nparsed = http_parser_execute(&ctx->parser, &ctx->settings, data, length);
      if (ctx->parser.http_errno == 0) {
         return nparsed;
      }
   }
   return -1;
}

http_t*
mhttp_parser_create(int parser_type) {
   http_t *h = (http_t *)malloc(sizeof(*h));
   ctx_t *ctx = (ctx_t *)malloc(sizeof(*ctx));
   memset(h, 0, sizeof(*h));
   memset(ctx, 0, sizeof(*ctx));
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
      data_t *body = h->body;
      while (body) {
         if (body->data) {
            free(body->data);
         }
         body = body->next;
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
   printf("--%d\n\n", (int)strlen(data));
   printf("%s", data);
   printf("\n--\n\n");
   http_t *h = mhttp_parser_create(style);
   if (mhttp_parser_process(h, data, strlen(data)-1) > 0) {
   }
   mhttp_parser_destroy(h);
   return 0;
}
#endif
