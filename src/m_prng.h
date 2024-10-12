/*
 * Copyright (c) 2017 lalawue
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 */

#ifndef _PRNG_H
#define _PRNG_H

#include <stdint.h>

typedef struct
{
    uint64_t seed[2];
} prng_t;

int prng_init(prng_t *);

uint64_t prng_next(prng_t *);

#endif