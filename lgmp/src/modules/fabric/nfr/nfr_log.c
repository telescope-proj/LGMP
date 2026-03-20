// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Tim Dettmar <beanfacts@protonmail.com>

#include "nfr_log.h"
#include "lgmp.h"
#include "nfr_constants.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

int nfrLogLevel = NFR_LOG_LEVEL_OFF;

void nfrLog(int level, const char * func, const char * file, int line,
             const char * fmt, ...)
{
  if (level < nfrLogLevel)
    return;
  const char * levelStr;
  switch (level)
  {
    case NFR_LOG_LEVEL_TRACE: levelStr = "T "; break;
    case NFR_LOG_LEVEL_DEBUG: levelStr = "D "; break;
    case NFR_LOG_LEVEL_INFO: levelStr = "I "; break;
    case NFR_LOG_LEVEL_WARNING: levelStr = "!W"; break;
    case NFR_LOG_LEVEL_ERROR: levelStr = "!E"; break;
    case NFR_LOG_LEVEL_FATAL: levelStr = "!F"; break;
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

void nfrSetLogLevel(int level) { nfrLogLevel = level; }
