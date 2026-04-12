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

#include "lgmp/client.h"

#include "client_internal.h"

#include <assert.h>

void lgmpClientFree(PLGMPClient * client)
{
  assert(client);
  if (!*client)
    return;
  (*client)->iface->free(client);
}

LGMP_STATUS lgmpClientSessionInit(PLGMPClient client, uint32_t * udataSize,
    uint8_t ** udata, uint32_t * clientID)
{
  assert(client);
  return client->iface->sessionInit(client, udataSize, udata, clientID);
}

bool lgmpClientSessionValid(PLGMPClient client)
{
  assert(client);
  return client->iface->sessionValid(client);
}

LGMP_STATUS lgmpClientSubscribe(PLGMPClient client, uint32_t queueID,
    PLGMPClientQueue * result)
{
  assert(client);
  return client->iface->subscribe(client, queueID, result);
}

LGMP_STATUS lgmpClientUnsubscribe(PLGMPClientQueue * result)
{
  assert(result);
  if (!*result)
    return LGMP_OK;
  return (*result)->client->iface->unsubscribe(result);
}

LGMP_STATUS lgmpClientAdvanceToLast(PLGMPClientQueue queue)
{
  assert(queue);
  return queue->ops->advanceToLast(queue);
}

LGMP_STATUS lgmpClientProcess(PLGMPClientQueue queue, PLGMPMessage result)
{
  assert(queue);
  return queue->ops->process(queue, result);
}

LGMP_STATUS lgmpClientMessageDone(PLGMPClientQueue queue)
{
  assert(queue);
  return queue->ops->messageDone(queue);
}

LGMP_STATUS lgmpClientSendData(PLGMPClientQueue queue,
    const void * restrict data, uint32_t size, uint32_t * serial)
{
  assert(queue);
  return queue->ops->sendData(queue, data, size, serial);
}

LGMP_STATUS lgmpClientGetSerial(PLGMPClientQueue queue, uint32_t * serial)
{
  assert(queue);
  return queue->ops->getSerial(queue, serial);
}