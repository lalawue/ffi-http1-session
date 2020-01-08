/* 
 * Copyright (c) 2020 lalawue
 * 
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 */

#ifndef _PULL_STYLE_API
#define _PULL_STYLE_API

#define HTTP_URL_LENGTH 8192

typedef enum {
   PROCESS_STATE_INVALID = 0,
   PROCESS_STATE_HEAD = 1,
   PROCESS_STATE_BODY = 2,
   PROCESS_STATE_FINISH = 3
} process_state_t;

typedef struct s_head_kv {
   char *head_field;
   char *head_value;
   struct s_head_kv *next;
} head_kv_t;

typedef struct s_data {
   unsigned char *data;
   int data_len;
   struct s_data *next;
} data_t;

typedef struct s_http {
   process_state_t state;
   char url[HTTP_URL_LENGTH];
   int status_code;
   head_kv_t *head_kv;
   data_t *body;
   int data_length;
   void *opaque;
} http_t;

/* 0:request 1:response 2:both */
http_t* mhttp_parser_create(int parser_type);
void mhttp_parser_destroy(http_t *h);

/* return byte processed, -1 means error */
int mhttp_parser_process(http_t *h, char *data, int length);

#endif
