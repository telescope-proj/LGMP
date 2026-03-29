// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Tim Dettmar <beanfacts@protonmail.com>

#ifndef NETFR_PRIVATE_LOG_H
#define NETFR_PRIVATE_LOG_H

#include "log.h"

/* Aliases so existing fabric code doesn't need to change */
#define NFR_LOG_TRACE   LGMP_LOG_TRACE
#define NFR_LOG_DEBUG   LGMP_LOG_DEBUG
#define NFR_LOG_INFO    LGMP_LOG_INFO
#define NFR_LOG_WARNING LGMP_LOG_WARNING
#define NFR_LOG_ERROR   LGMP_LOG_ERROR
#define NFR_LOG_FATAL   LGMP_LOG_FATAL

#endif
