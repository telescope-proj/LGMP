/**
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

#include "lgmp/host.h"

#include "host_internal.h"
#include "modules/module.h"

#include <assert.h>

LGMP_STATUS lgmpHostInit(void *mem, const uint32_t size, PLGMPHost * result,
    uint32_t udataSize, uint8_t * udata)
{
  return lgmpShmHostInit(mem, size, result, udataSize, udata);
}

void lgmpHostFree(PLGMPHost * host)
{
  assert(host);
  if (!*host)
    return;
  (*host)->iface->free(host);
}

LGMP_STATUS lgmpHostProcess(PLGMPHost host)
{
  assert(host);
  return host->iface->process(host);
}

LGMP_STATUS lgmpHostQueueNew(PLGMPHost host,
    const struct LGMPQueueConfig config, PLGMPHostQueue * result)
{
  assert(host);
  return host->iface->queueNew(host, config, result);
}

bool lgmpHostQueueHasSubs(PLGMPHostQueue queue)
{
  assert(queue);
  return queue->ops->queueHasSubs(queue);
}

uint32_t lgmpHostQueueNewSubs(PLGMPHostQueue queue)
{
  assert(queue);
  return queue->ops->queueNewSubs(queue);
}

uint32_t lgmpHostQueuePending(PLGMPHostQueue queue)
{
  assert(queue);
  return queue->ops->queuePending(queue);
}

LGMP_STATUS lgmpHostQueuePost(PLGMPHostQueue queue, uint32_t udata,
    PLGMPMemory payload)
{
  return lgmpHostQueuePostSized(queue, udata, payload, -1);
}

LGMP_STATUS lgmpHostQueuePostSized(PLGMPHostQueue queue, uint32_t udata,
    PLGMPMemory payload, int64_t payloadSize)
{
  assert(queue);
  assert(payload);
  return queue->ops->QueuePostSized(queue, udata, payload, payloadSize);
}

LGMP_STATUS lgmpHostReadData(PLGMPHostQueue queue, void * data, size_t * size)
{
  assert(queue);
  return queue->ops->readData(queue, data, size);
}

LGMP_STATUS lgmpHostAckData(PLGMPHostQueue queue)
{
  assert(queue);
  return queue->ops->ackData(queue);
}

LGMP_STATUS lgmpHostGetClientIDs(PLGMPHostQueue queue, uint32_t clientIDs[32],
    unsigned int * count)
{
  assert(queue);
  return queue->ops->getClientIDs(queue, clientIDs, count);
}

size_t lgmpHostMemAvail(PLGMPHostQueue queue)
{
  assert(queue);
  return queue->ops->memAvail(queue);
}

LGMP_STATUS lgmpHostMemAlloc(PLGMPHostQueue queue, uint32_t size,
    PLGMPMemory * result)
{
  assert(queue);
  return queue->ops->memAlloc(queue, size, result);
}

LGMP_STATUS lgmpHostMemAllocAligned(PLGMPHostQueue queue, uint32_t size,
    uint32_t alignment, PLGMPMemory * result)
{
  assert(queue);
  return queue->ops->memAllocAligned(queue, size, alignment, result);
}

void lgmpHostMemFree(PLGMPMemory * mem)
{
  assert(mem);
  if (!*mem)
    return;
  (*mem)->queue->ops->memFree(mem);
}

void * lgmpHostMemPtr(PLGMPMemory mem)
{
  assert(mem);
  return mem->queue->ops->memPtr(mem);
}
