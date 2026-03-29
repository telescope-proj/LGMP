// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Tim Dettmar <beanfacts@protonmail.com>

#include "log.h"
#include "lgmp.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

int lgmpLogLevel = LGMP_LOG_LEVEL_OFF;

void lgmpLog(int level, const char * func, const char * file, int line,
             const char * fmt, ...)
{
  if (level < lgmpLogLevel)
    return;
  const char * levelStr;
  switch (level)
  {
    case LGMP_LOG_LEVEL_TRACE: levelStr = "T "; break;
    case LGMP_LOG_LEVEL_DEBUG: levelStr = "D "; break;
    case LGMP_LOG_LEVEL_INFO: levelStr = "I "; break;
    case LGMP_LOG_LEVEL_WARNING: levelStr = "!W"; break;
    case LGMP_LOG_LEVEL_ERROR: levelStr = "!E"; break;
    case LGMP_LOG_LEVEL_FATAL: levelStr = "!F"; break;
    default: levelStr = "Unknown"; break;
  }
  const char * filename = strrchr(file, '/');
  if (!filename)
    filename = file;
  else
    ++filename;

  fprintf(stderr, "%s; %" PRIu64 "; %s:%d ; %s ; ", 
          levelStr, lgmpGetClockMS(), filename, line, func);
  va_list args;
  va_start(args, fmt);
  vfprintf(stderr, fmt, args);
  va_end(args);
  fprintf(stderr, "\n");
}

void lgmpSetLogLevel(int level) { lgmpLogLevel = level; }
