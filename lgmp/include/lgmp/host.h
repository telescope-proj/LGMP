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

/**
 * @brief Initialize the shared memory transport backend.
 * 
 * @param mem 
 * @param size 
 * @param result 
 * @param udataSize 
 * @param udata 
 * @return LGMP_STATUS 
 */
LGMP_STATUS lgmpShmHostInit(void *mem, const uint32_t size, PLGMPHost * result,
    uint32_t udataSize, uint8_t * udata);

/**
 * @brief Initialize the fabric transport backend.
 * 
 * @param uri       URI containing transport configuration.
 *
 * The URI follows the format ``transport://addr:port?opt=value&opt2=value2``
 *
 * The transport may either be **tcp** or **rdma**. In RDMA mode, the network
 * address MUST always be that of an RDMA-enabled network adapter in order for
 * the system to bind to the hardware resources of that NIC. Addresses such as
 * ``0.0.0.0`` or `127.0.0.1`` are invalid!
 *
 * NetFR allocates up to ``LGMP_MAX_QUEUES + 1`` ports, starting from the port
 * number specified in the URI. For example, with 5 queues, using the URI
 * rdma://10.1.2.3:9000 will allocate port 9000 for the metadata queue, and
 * 9001-9005 for each LGMP queue. Please ensure that this block of ports is
 * free before initializing this backend.
 *
 * @param result    Output host pointer
 * @param udataSize Size of user data
 * @param udata     Arbitrary user data
 * @return          LGMP_STATUS 
 */
LGMP_STATUS lgmpFabricHostInit(const char * uri,
    PLGMPHost * result, uint32_t udataSize, uint8_t * udata);

void        lgmpHostFree   (PLGMPHost * host);
LGMP_STATUS lgmpHostProcess(PLGMPHost host);

struct LGMPQueueConfig
{
  uint32_t queueID;     // application defined queue ID
  uint32_t numMessages; // number of messages in the queue
  uint32_t subTimeout;  // length of time in ms to wait before removing a subscriber
};

LGMP_STATUS lgmpHostQueueNew    (PLGMPHost host,
    const struct LGMPQueueConfig config, PLGMPHostQueue * result);
bool        lgmpHostQueueHasSubs(PLGMPHostQueue queue);
uint32_t    lgmpHostQueueNewSubs(PLGMPHostQueue queue);
uint32_t    lgmpHostQueuePending(PLGMPHostQueue queue);
LGMP_STATUS lgmpHostQueuePost   (PLGMPHostQueue queue, uint32_t udata,
    PLGMPMemory payload);
LGMP_STATUS lgmpHostQueuePostSized (PLGMPHostQueue queue, uint32_t udata,
    PLGMPMemory payload, int64_t payloadSize);
LGMP_STATUS lgmpHostReadData(PLGMPHostQueue queue, void * data, size_t * size);
LGMP_STATUS lgmpHostAckData(PLGMPHostQueue queue);
LGMP_STATUS lgmpHostGetClientIDs(PLGMPHostQueue queue, uint32_t clientIDs[32],
    unsigned int * count);


/**
 * @brief Get the amount of memory available for allocations.
 * 
 * @warning The fabric backend returns a large amount of available memory, but
 *          unlike the shared memory backend, this memory is not actually
 *          allocated yet. Therefore, you should not allocate all of the 
 *          memory returned from this call.
 *
 * @param queue Queue to query free memory from. 
 *              The queue is irrelevant for the shared memory backend, and
 *              currently irrelevant for the fabric backend as well.
 *
 * @return size_t The amount of memory available for allocations.
 */
size_t      lgmpHostMemAvail       (PLGMPHostQueue queue);

/**
 * @brief Allocates some RAM for application use from the shared memory
 *
 * @warning These allocations are permanent with the shared memory backend.
 *          With the fabric backend, the allocations are recoverable.
 *          See #lgmpHostMemFree.
 *
 * @param queue     LGMP queue to allocate memory from. 
 *                  For the shared memory backend, the queue is irrelevant.
 *                  For the fabric backend, the memory returned from this call 
 *                  may only be used on the queue used to allocate it.
 * @param size      Size of the memory to allocate
 * @param result    The allocated memory region
 */
LGMP_STATUS lgmpHostMemAlloc       (PLGMPHostQueue queue, uint32_t size,
    PLGMPMemory * result);

/**
 * @brief Like #lgmpHostMemAlloc, with an additional alignment specification.
 *
 * @param queue     LGMP queue to allocate memory from
 * @param size      Size of the memory to allocate
 * @param alignment Alignment of the memory to allocate
 * @param result    The allocated memory region
 */
LGMP_STATUS lgmpHostMemAllocAligned(PLGMPHostQueue queue, uint32_t size,
    uint32_t alignment, PLGMPMemory * result);

/**
 * @brief Allocate a DMABUF-backed memory block for RDMA operations.
 *
 * @note Only supported by the fabric backend. Returns
 *       #LGMP_ERR_NOT_SUPPORTED on the SHM backend.
 *
 * @param queue  LGMP queue to allocate memory from
 * @param size   Size of the memory to allocate
 * @param result The allocated memory region
 */
LGMP_STATUS lgmpHostMemAllocDMABUF(PLGMPHostQueue queue, uint32_t size,
    PLGMPMemory * result);

/**
 * @brief Get the DMABUF file descriptor for a DMABUF-backed memory block.
 *
 * @param mem Memory block allocated with #lgmpHostMemAllocDMABUF.
 * @return    The DMABUF file descriptor, or -1 if @p mem is not DMABUF-backed.
 */
int lgmpHostDMAFD(PLGMPMemory mem);

/**
 * @brief Free a memory block.
 *
 * @warning Calling this function only frees the #LGMPMemory structure, but
 *          does not recover the shared memory for later use. However, when 
 *          using the fabric transport, these allocations are recoverable.
 * 
 * @param mem Memory block to free.
 */
void        lgmpHostMemFree        (PLGMPMemory * mem);

/**
 * @brief Get a pointer to the block of memory referred to by the #PLGMPMemory.
 *
 * @param mem Memory block to get a pointer to.
 *
 * @return void* Pointer to the memory block.
 */
void *      lgmpHostMemPtr         (PLGMPMemory mem);

#ifdef __cplusplus
}
#endif

#endif
