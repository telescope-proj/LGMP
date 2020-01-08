/*
LGMP - Looking Glass Memory Protocol
Copyright (C) 2020 Geoffrey McRae <geoff@hostfission.com>
https://looking-glass.hostfission.com

This program is free software; you can redistribute it and/or modify it under
the terms of the GNU General Public License as published by the Free Software
Foundation; either version 2 of the License, or (at your option) any later
version.

This program is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program; if not, write to the Free Software Foundation, Inc., 59 Temple
Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef LGMP_HOST_H
#define LGMP_HOST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lgmp.h"
#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif

LGMP_STATUS lgmpHostInit(void *mem, const size_t size, PLGMPHost * result);
void lgmpHostFree(PLGMPHost * host);

LGMP_STATUS lgmpHostAddQueue(PLGMPHost host, uint32_t type, uint32_t numMessages, PLGMPHQueue * result);
LGMP_STATUS lgmpHostProcess(PLGMPHost host);

/**
 * Allocates some RAM for application use from the shared memory
 *
 * Note: These allocations are permanant! Calling lgmpHostMemFree only frees
 * the LGMPMemory structure, but does not recover the shared memory for later
 * use.
 */
LGMP_STATUS lgmpHostMemAlloc(PLGMPHost host, uint32_t size, PLGMPMemory * result);
void        lgmpHostMemFree (PLGMPMemory * mem);
void *      lgmpHostMemPtr  (PLGMPMemory mem);

LGMP_STATUS lgmpHostPost(PLGMPHQueue queue, uint32_t type, PLGMPMemory payload);

#ifdef __cplusplus
}
#endif

#endif