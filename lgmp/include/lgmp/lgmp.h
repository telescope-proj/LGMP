/***
 * LGMP - Looking Glass Memory Protocol
 * Copyright © 2020-2025 Geoffrey McRae <geoff@hostfission.com>
 * https://github.com/gnif/LGMP
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef LGMP_LGMP_H
#define LGMP_LGMP_H

/** 
 * @brief Maximum size of a client-to-host data message in bytes. 
 *
 * @warning This must match the size defined in ``src/modules/shm/headers.h``.
 */
#define LGMP_MSGS_SIZE 64

#define LGMP_MAX_QUEUES  5
#define LGMP_MAX_CLIENTS 8

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LGMPHost        * PLGMPHost;
typedef struct LGMPClient      * PLGMPClient;
typedef struct LGMPHostQueue   * PLGMPHostQueue;
typedef struct LGMPClientQueue * PLGMPClientQueue;
typedef struct LGMPMemory      * PLGMPMemory;

enum
{
  LGMP_LOG_LEVEL_TRACE,
  LGMP_LOG_LEVEL_DEBUG,
  LGMP_LOG_LEVEL_INFO,
  LGMP_LOG_LEVEL_WARNING,
  LGMP_LOG_LEVEL_ERROR,
  LGMP_LOG_LEVEL_FATAL,
  LGMP_LOG_LEVEL_OFF
};

void lgmpSetLogLevel(int level);

#ifdef __cplusplus
}
#endif

#endif
