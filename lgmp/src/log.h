// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Tim Dettmar <beanfacts@protonmail.com>

#ifndef LGMP_PRIVATE_LOG_H
#define LGMP_PRIVATE_LOG_H

#include "lgmp/lgmp.h"

extern int lgmpLogLevel;

unsigned long getTimestamp(void);

void lgmpLog(int level, const char * func, const char * file, int line,
             const char * fmt, ...);

#define LGMP_LOG_TRACE(fmt, ...)                                               \
  lgmpLog(LGMP_LOG_LEVEL_TRACE, __func__, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LGMP_LOG_DEBUG(fmt, ...)                                               \
  lgmpLog(LGMP_LOG_LEVEL_DEBUG, __func__, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LGMP_LOG_INFO(fmt, ...)                                                \
  lgmpLog(LGMP_LOG_LEVEL_INFO, __func__, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LGMP_LOG_WARNING(fmt, ...)                                             \
  lgmpLog(LGMP_LOG_LEVEL_WARNING, __func__, __FILE__, __LINE__, fmt,           \
          ##__VA_ARGS__)

#define LGMP_LOG_ERROR(fmt, ...)                                               \
  lgmpLog(LGMP_LOG_LEVEL_ERROR, __func__, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define LGMP_LOG_FATAL(fmt, ...)                                               \
  lgmpLog(LGMP_LOG_LEVEL_FATAL, __func__, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#endif
