/*
 * Copyright (c) 2017 lalawue
 *
 * This library is free software; you can redistribute it and/or modify it
 * under the terms of the MIT license. See LICENSE for details.
 */

/* PRNG from 'http://xoroshiro.di.unimi.it/xoroshiro128plus.c'
 */

#include "m_prng.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

int prng_init(prng_t *rng)
{
    if (rng == NULL)
    {
        return 0;
    }

    srand(time(NULL));
    rng->seed[0] = rand();
    rng->seed[1] = rand();

    return 1;
}

static inline uint64_t
__rotl(const uint64_t x, int k)
{
    return (x << k) | (x >> (64 - k));
}

uint64_t
prng_next(prng_t *rng)
{
    if (rng == NULL)
    {
        return 0;
    }

    const uint64_t s0 = rng->seed[0];
    uint64_t s1 = rng->seed[1];
    const uint64_t result = s0 + s1;

    s1 ^= s0;
    rng->seed[0] = __rotl(s0, 55) ^ s1 ^ (s1 << 14); // a, b
    rng->seed[1] = __rotl(s1, 36);                   // c

    return result;
}