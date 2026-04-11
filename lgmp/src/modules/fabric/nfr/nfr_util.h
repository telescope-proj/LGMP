// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Tim Dettmar <beanfacts@protonmail.com>

#ifndef NETFR_PRIVATE_UTIL_H
#define NETFR_PRIVATE_UTIL_H

#include <stddef.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <windows.h>
  #include <bcrypt.h>
  #ifdef _MSC_VER
    #pragma comment(lib, "bcrypt")
  #endif
#elif defined(__linux__)
  #include <sys/random.h>
#else
  #include <stdlib.h>
#endif

static inline void nfrGetCryptoRandom(void * data, size_t size)
{
#ifdef _WIN32
  BCryptGenRandom(NULL, (PUCHAR)data, (ULONG)size,
                  BCRYPT_USE_SYSTEM_PREFERRED_RNG);
#elif defined(__linux__)
  while (size > 0)
  {
    ssize_t n = getrandom(data, size, 0);
    if (n < 0)
      continue;
    data  = (char *)data + n;
    size -= n;
  }
#else
  arc4random_buf(data, size);
#endif
}

static inline uint32_t nfrGetRandomUint32(void)
{
  uint32_t val;
  nfrGetCryptoRandom(&val, sizeof(val));
  return val;
}

static inline uint64_t nfrGetRandomUint64(void)
{
  uint64_t val;
  nfrGetCryptoRandom(&val, sizeof(val));
  return val;
}

#endif