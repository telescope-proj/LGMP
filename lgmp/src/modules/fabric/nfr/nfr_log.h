// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Tim Dettmar <beanfacts@protonmail.com>

#ifndef NETFR_PRIVATE_LOG_H
#define NETFR_PRIVATE_LOG_H

unsigned long getTimestamp(void);

void nfrLog(int level, const char * func, const char * file, int line,
             const char * fmt, ...);

#define NFR_LOG_TRACE(fmt, ...)                                                \
  nfrLog(NFR_LOG_LEVEL_TRACE, __func__, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define NFR_LOG_DEBUG(fmt, ...)                                                \
  nfrLog(NFR_LOG_LEVEL_DEBUG, __func__, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define NFR_LOG_INFO(fmt, ...)                                                 \
  nfrLog(NFR_LOG_LEVEL_INFO, __func__, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define NFR_LOG_WARNING(fmt, ...)                                              \
  nfrLog(NFR_LOG_LEVEL_WARNING, __func__, __FILE__, __LINE__, fmt,            \
          ##__VA_ARGS__)

#define NFR_LOG_ERROR(fmt, ...)                                                \
  nfrLog(NFR_LOG_LEVEL_ERROR, __func__, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#define NFR_LOG_FATAL(fmt, ...)                                                \
  nfrLog(NFR_LOG_LEVEL_FATAL, __func__, __FILE__, __LINE__, fmt, ##__VA_ARGS__)

#endif
