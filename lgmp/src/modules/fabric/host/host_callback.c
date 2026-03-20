// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Tim Dettmar <beanfacts@protonmail.com>

/* Internal callbacks */

#include "host_callback.h"
#include "fabric.h"

#include "nfr_log.h"
#include "nfr_protocol.h"
#include "nfr_resource.h"

void nfrHostProcessInternalTx(struct NFRFabricContext * ctx)
{
  NFR_LOG_DEBUG("Processing txctx %p", ctx);
  ASSERT_CONTEXT_VALID(ctx);
  NFR_CAST_UDATA(struct LGMPFabricHostChannel *, ch, ctx, NFR_HOST_TX_CB_CHANNEL);
  if (!ch)
  {
    assert(!"Invalid arguments passed to nfrHostProcessInternalTx");
    return;
  }
  ++ch->res->txCredits;
  NFR_RESET_CONTEXT(ctx);
}

void nfrHostProcessInternalRx(struct NFRFabricContext * ctx)
{
  NFR_LOG_DEBUG("Processing rxctx %p", ctx);
  if (!ctx)
  {
    assert(!"Null context passed to nfrHostProcessInternalRx");
    return;
  }
  ASSERT_CONTEXT_VALID(ctx);

  NFR_CAST_UDATA(struct LGMPFabricHostChannel *, chan, ctx, NFR_HOST_RX_CB_CHANNEL);
  struct LGMPFabricHost * host = chan->parent;

  if (!chan || !host)
  {
    assert(!"Invalid arguments passed to nfrHostProcessInternalRx");
    return;
  }

  if (chan->res->connState != NFR_CONN_STATE_CONNECTED)
  {
    NFR_LOG_ERROR("Invalid connection state %d", chan->res->connState);
    return;
  }

  if (ctx->state != CTX_STATE_WAITING)
  {
    NFR_LOG_ERROR("Invalid buffer state %d", ctx->state);
    return;
  }

  struct NFRHeader * hdr = (struct NFRHeader *) ctx->slot->data;
  if (memcmp(hdr->magic, NETFR_MAGIC, 8) != 0 || hdr->version != NETFR_VERSION)
  {
    NFR_LOG_ERROR("Invalid message header from client");
    goto disconnect_peer;
  }

  switch (hdr->type)
  {
    case NFR_MSG_CLIENT_BUFFER_STATE:
    {
      /* Save each client region for RDMA writes */
      struct NFRMsgClientBufferState * state = (struct NFRMsgClientBufferState *) hdr;
      if (state->index >= NETFR_MAX_MEM_REGIONS)
      {
        NFR_LOG_ERROR("Client sent invalid memory region index %d", state->index);
        goto disconnect_peer;
      }

      struct NFRRemoteMemory * rmem = chan->clientRegions + state->index;

      if (!state->size)
      {
        memset(rmem, 0, sizeof(*rmem));
        rmem->state = NFR_RMEM_NONE;
        goto release_mbuf;
      }

      /* Reject regions that exceed the maximum buffer size */
      if (state->size > NETFR_MAX_BUFFER_SIZE)
      {
        NFR_LOG_ERROR("Client region %d size %lu exceeds max %d",
                      state->index, (unsigned long) state->size,
                      NETFR_MAX_BUFFER_SIZE);
        goto disconnect_peer;
      }

      /* Enforce the allocation cap */
      uint64_t totalRemote = 0;
      for (int j = 0; j < NETFR_MAX_MEM_REGIONS; ++j)
      {
        if (j != state->index &&
            chan->clientRegions[j].state != NFR_RMEM_NONE)
          totalRemote += chan->clientRegions[j].size;
      }
      if (totalRemote + state->size > host->maxTotalAlloc)
      {
        NFR_LOG_ERROR("Client total remote allocation would exceed cap "
                      "(%lu + %lu > %lu)",
                      (unsigned long) totalRemote,
                      (unsigned long) state->size,
                      (unsigned long) host->maxTotalAlloc);
        goto release_mbuf;
      }

      NFR_LOG_DEBUG("Got buf index %d / %p len %lu key %d st %d -> %d",
                    state->index, (uintptr_t) state->addr, state->size,
                    state->rkey, rmem->state, NFR_RMEM_AVAILABLE);
      rmem->addr          = state->addr;
      rmem->size          = state->size;
      rmem->rkey          = state->rkey;
      rmem->align         = state->pageSize;
      rmem->index         = state->index;
      rmem->state         = NFR_RMEM_AVAILABLE;
      rmem->activeContext = 0;
      break;
    }
    case NFR_MSG_CLIENT_DATA:
    {
      /* From a lgmpClientSendData call */
      struct NFRMsgClientData * msg = (struct NFRMsgClientData *) hdr;
      if (msg->length > NETFR_MESSAGE_MAX_PAYLOAD_SIZE)
      {
        NFR_LOG_ERROR("Client sent oversized data message (%u bytes)",
                      msg->length);
        goto disconnect_peer;
      }
      ctx->state               = CTX_STATE_HAS_DATA;
      ctx->slot->msgSerial     = msg->msgSerial;
      ctx->slot->channelSerial = msg->channelSerial;
      return;
    }
    case NFR_MSG_HOST_DATA_ACK: 
    {
      /* Free up a transfer credit. Currently unused because lgmpHostSendData
       * does not exist, but we'll keep the functionality because it's already
       * implemented on the client side and isn't hard to maintain. */
      ++chan->res->txCredits;
      break;
    }
    case NFR_MSG_CLIENT_SUBSCRIBE:
    {
      /* Client subscribed to a queue, track it */
      NFR_LOG_DEBUG("Client subscribed to queue %u",
          ((struct NFRMsgClientSubscribe *) hdr)->queueID);
      chan->subscribed = true;
      ++chan->newSubs;
      break;
    }
    case NFR_MSG_CLIENT_UNSUBSCRIBE:
    {
      /* Client unsubscribed from a queue, track it */
      NFR_LOG_DEBUG("Client unsubscribed from queue %u",
          ((struct NFRMsgClientUnsubscribe *) hdr)->queueID);
      chan->subscribed = false;
      break;
    }
    case NFR_MSG_CLIENT_HELLO:
    {
      NFR_LOG_ERROR("Client sent hello message on data channel");
      goto disconnect_peer;
    }
    case NFR_MSG_HOST_DATA:
    case NFR_MSG_HOST_BUFFER_STATE:
    {
      NFR_LOG_ERROR("Client sent server-side message type %d", hdr->type);
      goto disconnect_peer;
    }
    default:
    {
      NFR_LOG_ERROR("Client sent unknown message type %d", hdr->type);
      goto disconnect_peer;
    }
  }

release_mbuf:
  NFR_RESET_CONTEXT(ctx);
  return;

disconnect_peer:
  NFR_RESET_CONTEXT(ctx);
  nfrDisconnectPeer(chan->res);
}

void nfrHostProcessInternalWrite(struct NFRFabricContext * ctx)
{
  NFR_LOG_TRACE("Processing wrctx %p", ctx);
  if (!ctx)
  {
    assert(!"Null context passed to nfrHostProcessInternalWrite");
    return;
  }

  NFR_CAST_UDATA(struct LGMPFabricHostChannel *, ch, ctx, NFR_HOST_WRITE_CB_CHANNEL);
  NFR_CAST_UDATA(struct NFRMemory *, lmem, ctx, NFR_HOST_WRITE_CB_LMEM);
  NFR_CAST_UDATA(struct NFRRemoteMemory *, rmem, ctx, NFR_HOST_WRITE_CB_RMEM);
  NFR_CAST_UDATA_NUM(uint64_t, lOffset, ctx, NFR_HOST_WRITE_CB_LOFFSET);
  NFR_CAST_UDATA_NUM(uint64_t, rOffset, ctx, NFR_HOST_WRITE_CB_ROFFSET);
  NFR_CAST_UDATA_NUM(uint64_t, length,  ctx, NFR_HOST_WRITE_CB_LENGTH);
  NFR_CAST_UDATA_UNCHECKED(NFRCallback, userCb, ctx, NFR_HOST_WRITE_CB_USER_CB);

  if (!ch || !lmem || !rmem || !length || 
      lOffset + length > lmem->size ||
      rOffset + length > rmem->size ||
      length > NETFR_MAX_BUFFER_SIZE)
  {
    NFR_LOG_ERROR(
      "Write callback: invalid arguments! ch=%p lmem=%p rmem=%p length=%lu "
      "lOffset=%lu rOffset=%lu",
      ch, lmem, rmem, length, lOffset, rOffset);
    assert(!"Invalid arguments passed to nfrHostProcessInternalWrite");
    return;
  }

  if (ctx->state != CTX_STATE_WAITING)
  {
    NFR_LOG_ERROR("Write callback: invalid buffer state %d", ctx->state);
    return;
  }

  if (rmem->state == NFR_RMEM_BUSY_LOCAL)
    rmem->state = NFR_RMEM_BUSY_REMOTE;
  else if (rmem->state != NFR_RMEM_AVAILABLE)
  {
    NFR_LOG_ERROR("Write callback: unexpected remote memory state %d",
                  rmem->state);
    return;
  }
  /* If available, client's buffer state response was processed before
     this write CQ completion (out-of-order). Recycled already. */

  if (userCb)
  {
    const void ** uudata =
        (const void **) (ctx->cbInfo.uData + NFR_USER_CB_INDEX);
    userCb(uudata);
  }

  NFR_RESET_CONTEXT(ctx);
}
