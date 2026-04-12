// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Tim Dettmar <beanfacts@protonmail.com>

#include "client_callback.h"
#include "fabric.h"
#include "nfr_log.h"
#include "nfr_mem.h"
#include "nfr_protocol.h"

#include "modules/fabric/fabric.h"

void nfrClientProcessInternalTx(struct NFRFabricContext * ctx)
{
  ASSERT_CONTEXT_VALID(ctx);
  struct NFRHeader * hdr = (struct NFRHeader *) ctx->slot->data;
  NFR_LOG_DEBUG("Processing txctx %p -> type %d", ctx, hdr->type);
  assert(ctx->state == CTX_STATE_WAITING || ctx->state == CTX_STATE_ACK_ONLY);
  NFR_CAST_UDATA(struct LGMPFabricClientChannel *, ch, ctx, NFR_CLIENT_TX_CB_CHANNEL);
  if (!ch)
  {
    assert(!"Invalid arguments passed to nfrClientProcessInternalTx");
    return;
  }
  ++ch->res->txCredits;
  NFR_RESET_CONTEXT(ctx);
}

void nfrClientProcessInternalRx(struct NFRFabricContext * ctx)
{
  NFR_LOG_DEBUG("Processing rxctx %p", ctx);
  ASSERT_CONTEXT_VALID(ctx);

  NFR_CAST_UDATA(struct LGMPFabricClientChannel *, chan, ctx, NFR_CLIENT_RX_CB_CHANNEL);

  struct LGMPFabricClient * client = chan->parent;
  assert(client);
  assert(chan);

  struct NFRHeader * hdr = (struct NFRHeader *) ctx->slot->data;
  if (memcmp(hdr->magic, NETFR_MAGIC, 8) != 0 || hdr->version != NETFR_VERSION)
  {
    NFR_LOG_ERROR("Invalid message header from host");
    goto disconnect_peer;
  }

  switch (hdr->type)
  {
    case NFR_MSG_BUFFER_UPDATE:
    {
      /* New data available in a memory region, process it */
      struct NFRMsgBufferUpdate * update = (struct NFRMsgBufferUpdate *) hdr;
      if (update->bufferIndex >= NETFR_MAX_MEM_REGIONS)
      {
        NFR_LOG_ERROR("Host sent invalid buffer index %d", update->bufferIndex);
        goto disconnect_peer;
      }
      struct NFRMemory * mem = chan->res->memRegions + update->bufferIndex;
      if (update->payloadOffset + update->payloadSize > mem->size)
      {
        NFR_LOG_ERROR("Host sent buffer update exceeding region bounds "
                      "(offset %u + size %u > %lu)",
                      update->payloadOffset, update->payloadSize, mem->size);
        goto disconnect_peer;
      }
      mem->state         = MEM_STATE_HAS_DATA;
      mem->payloadOffset = update->payloadOffset;
      mem->payloadLength = update->payloadSize;
      mem->writeSerial   = update->writeSerial;
      mem->channelSerial = update->channelSerial;
      mem->udata         = update->udata;
      NFR_RESET_CONTEXT(ctx);
      return;
    }
    case NFR_MSG_HOST_DATA:
    {
      /* Host data is implemented but not used (LGMP has no mechanism for it,
         only HostQueuePost) which is covered by NFR_MSG_BUFFER_UPDATE */
      struct NFRMsgHostData * msg = (struct NFRMsgHostData *) hdr;
      if (msg->length > NETFR_MESSAGE_MAX_PAYLOAD_SIZE || msg->length == 0)
      {
        NFR_LOG_ERROR("Host sent invalid data message length %u", msg->length);
        goto disconnect_peer;
      }
      ctx->state               = CTX_STATE_HAS_DATA;
      ctx->slot->msgSerial     = msg->msgSerial;
      ctx->slot->channelSerial = msg->channelSerial;
      return;
    }
    case NFR_MSG_CLIENT_DATA_ACK:
    {
      /* Free up a transfer credit */
      NFR_RESET_CONTEXT(ctx);
      ++chan->res->txCredits;
      return;
    }
    case NFR_MSG_HOST_BUFFER_STATE:
    {
      /* For each host region, ensure we have a matching client region
         to allow for RDMA writes to it */
      struct NFRMsgHostBufferState * state =
          (struct NFRMsgHostBufferState *) hdr;

      struct NFRResource * chanRes = chan->res;
      if (!chanRes)
      {
        NFR_RESET_CONTEXT(ctx);
        return;
      }

      /* Get existing memory regions and count reported */

      uint64_t totalAlloc = 0;
      int localCount = 0;
      for (int i = 0; i < NETFR_MAX_MEM_REGIONS; ++i)
      {
        if (chanRes->memRegions[i].state >= MEM_STATE_AVAILABLE_UNSYNCED &&
            chanRes->memRegions[i].memType != NFR_MEM_TYPE_INTERNAL)
        {
          totalAlloc += chanRes->memRegions[i].size;
          ++localCount;
        }
      }

      int hostCount = 0;
      for (int i = 0; i < NETFR_MAX_MEM_REGIONS && state->size[i]; ++i)
        ++hostCount;

      /* Allocate memory regions, taking into account constraints */
      int required = hostCount - localCount;
      for (int i = 0; i < required; ++i)
      {
        uint64_t sz = state->size[localCount + i];
        if (!sz)
          sz = state->size[0];

        if (sz > client->maxRegionAlloc)
          continue;
        if (totalAlloc + sz > client->maxTotalAlloc)
          continue;

        bool useDMABUF = chan->parent->useDMABUF;
        
        uint64_t acs = FI_READ | FI_WRITE | FI_REMOTE_WRITE;
        PNFRMemory mem = NULL;
        if (useDMABUF)
          mem = nfrRdmaAllocDMABUF(chanRes, sz, acs);
        else
          mem = nfrRdmaAttach(chanRes, 0, sz, 0, acs, NFR_MEM_TYPE_SYSTEM_MANAGED,
                              MEM_STATE_AVAILABLE_UNSYNCED);
        if (mem)
          totalAlloc += sz;
        else
          NFR_LOG_ERROR("Failed to allocate memory region of size %lu for host "
                        "buffer (useDMABUF=%d)", sz, useDMABUF);
      }

      NFR_RESET_CONTEXT(ctx);
      return;
    }
    case NFR_MSG_HOST_INIT_DATA:
    {
      struct NFRMsgHostInitData * initMsg = (struct NFRMsgHostInitData *) hdr;
      if (initMsg->udataSize > sizeof(client->udata))
      {
        NFR_LOG_ERROR("Host sent oversized init data (%u bytes)",
                      initMsg->udataSize);
        goto disconnect_peer;
      }
      client->clientID  = initMsg->clientID;
      client->sessionID = initMsg->sessionID;
      client->udataSize = initMsg->udataSize;
      if (initMsg->udataSize > 0)
        memcpy(client->udata, initMsg->udata, initMsg->udataSize);
      client->initDataReceived = true;
      NFR_LOG_DEBUG("Received init data: clientID=%u sessionID=%u udataSize=%u",
                    initMsg->clientID, initMsg->sessionID, initMsg->udataSize);
      NFR_RESET_CONTEXT(ctx);
      return;
    }
    default:
    {
      NFR_LOG_ERROR("Host sent unknown message type %d", hdr->type);
      goto disconnect_peer;
    }
  }

disconnect_peer:
  NFR_RESET_CONTEXT(ctx);
  nfrDisconnectPeer(chan->res);
}
