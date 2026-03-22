// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Tim Dettmar <beanfacts@protonmail.com>

#include "lgmp/host.h"
#include "lgmp/status.h"

#include "host_internal.h"
#include "modules/module.h"

#include "fabric.h"
#include "nfr_uri.h"
#include "nfr_log.h"
#include "nfr_mem.h"
#include "nfr_protocol.h"
#include "nfr_resource.h"
#include "nfr_util.h"
#include "host_callback.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

// Internal helpers ------------------------------------------------------------

static int lgmpFabric_HostBind(struct NFRResource * res)
{
  assert(res);
  assert(res->info);

  int ret = fi_passive_ep(res->fabric, res->info, &res->pep, res);
  if (ret < 0)
  {
    NFR_LOG_DEBUG("Failed to create PEP: %s (%d)", fi_strerror(-ret), ret);
    return ret;
  }

  ret = fi_pep_bind(res->pep, &res->eq->fid, 0);
  if (ret < 0)
  {
    NFR_LOG_DEBUG("Failed to bind PEP to EQ: %s (%d)", fi_strerror(-ret), ret);
    fi_close(&res->pep->fid);
    res->pep = 0;
    return ret;
  }

  ret = fi_listen(res->pep);
  if (ret < 0)
  {
    NFR_LOG_DEBUG("Failed to listen: %s (%d)", fi_strerror(-ret), ret);
    fi_close(&res->pep->fid);
    res->pep = 0;
    return ret;
  }

  NFR_LOG_DEBUG("Passive endpoint listening");
  return 0;
}

static int lgmpFabric_HostEQProcess(struct NFRResource * res)
{
  struct NFRExtCMEntry entry;
  uint32_t             event;
  int ret = (int) fi_eq_read(res->eq, &event, &entry, sizeof(entry), 0);
  if (ret == 0 || ret == -FI_EAGAIN)
    return 0;

  if (ret == -FI_EAVAIL)
  {
    struct fi_eq_err_entry err;
    ret = (int) fi_eq_readerr(res->eq, &err, 0);
    if (ret < 0)
      return ret;
    NFR_LOG_DEBUG("EQ error: %s (%d)", fi_strerror(err.err), err.err);
    return -err.err;
  }
  if (ret < 0)
    return ret;

  switch (event)
  {
    case FI_CONNREQ:
    {
      if (res->connState == NFR_CONN_STATE_CONNECTED ||
          res->connState == NFR_CONN_STATE_CONNECTING)
      {
        NFR_LOG_DEBUG("Rejecting connection: already connected");
        fi_reject(res->pep, entry.info->handle, 0, 0);
        fi_freeinfo(entry.info);
        return 0;
      }

      /* Validate client hello */
      struct NFRMsgClientHello * hello = (struct NFRMsgClientHello *) entry.data;
      if (memcmp(hello->header.magic, NETFR_MAGIC, 8) != 0 ||
          hello->header.version != NETFR_VERSION ||
          hello->header.type != NFR_MSG_CLIENT_HELLO)
      {
        NFR_LOG_DEBUG("Rejecting connection: bad hello message");
        fi_reject(res->pep, entry.info->handle, 0, 0);
        fi_freeinfo(entry.info);
        return 0;
      }

      /* Create the active endpoint */
      ret = fi_endpoint(res->domain, entry.info, &res->ep, res);
      fi_freeinfo(entry.info);
      if (ret < 0)
      {
        NFR_LOG_DEBUG("Failed to create EP: %s (%d)", fi_strerror(-ret), ret);
        return ret;
      }

      ret = nfrEndpointSetup(res);
      if (ret < 0)
        return ret;

      struct NFRMsgServerHello serverHello;
      nfrSetHeader(&serverHello.header, NFR_MSG_SERVER_HELLO);
      serverHello.status = NFR_MSG_STATUS_OK;

      ret = fi_accept(res->ep, &serverHello, sizeof(serverHello));
      if (ret < 0)
      {
        NFR_LOG_DEBUG("Failed to accept: %s (%d)", fi_strerror(-ret), ret);
        fi_close(&res->ep->fid);
        res->ep = 0;
        return ret;
      }

      res->connState = NFR_CONN_STATE_CONNECTING;
      return 0;
    }
    case FI_CONNECTED:
    {
      res->connState = NFR_CONN_STATE_CONNECTED;
      NFR_LOG_DEBUG("Client connected");
      return 1;
    }
    case FI_SHUTDOWN:
    {
      fi_close(&res->ep->fid);
      res->ep        = 0;
      res->connState = NFR_CONN_STATE_DISCONNECTED;

      /* Drain all CQEs from the disconnected endpoint */
      if (res->cq)
      {
        struct fi_cq_data_entry cqEntry;
        struct fi_cq_err_entry  cqErr;
        for (;;)
        {
          int n = (int) fi_cq_read(res->cq, &cqEntry, 1);
          if (n == 0 || n == -FI_EAGAIN)
            break;
          if (n == -FI_EAVAIL)
          {
            fi_cq_readerr(res->cq, &cqErr, 0);
            continue;
          }
          if (n < 0)
            break;
        }
      }

      /* Reset contexts to clean state */
      if (res->commBuf.ctx)
      {
        uint32_t total = NFR_TOTAL_SLOTS(res->commBuf.info);
        for (uint32_t i = 0; i < total; ++i)
        {
          res->commBuf.ctx[i].state = CTX_STATE_AVAILABLE;
          memset(&res->commBuf.ctx[i].cbInfo, 0,
                 sizeof(res->commBuf.ctx[i].cbInfo));
        }
      }

      /* Reset tx credits */
      res->txCredits = NETFR_CREDIT_COUNT;

      /* Clear client memory regions */
      if (res->parentRole == NFR_ROLE_HOST && res->parent.host)
      {
        struct LGMPFabricHost * fh = res->parent.host;
        for (int i = 0; i < fh->numChannels; ++i)
        {
          if (fh->channels[i].res == res)
          {
            memset(fh->channels[i].clientRegions, 0,
                   sizeof(fh->channels[i].clientRegions));
            fh->channels[i].subscribed = false;
            fh->channels[i].newSubs    = 0;
            break;
          }
        }
      }

      NFR_LOG_DEBUG("Client disconnected");
      return -FI_ECONNRESET;
    }
    default:
      NFR_LOG_DEBUG("Unexpected event: %d", event);
      return -EIO;
  }
}

// LGMP vtable implementations -------------------------------------------------

LGMP_STATUS lgmpFabricHostInit(const char * uri,
    PLGMPHost * result, uint32_t udataSize, uint8_t * udata)
{
  if (!uri || !result || (!udataSize && udata) || (udataSize && !udata))
    return LGMP_ERR_INVALID_ARGUMENT;

  /* Parse the URI into address + transport type */
  struct sockaddr_in baseAddr;
  uint8_t            transport;
  int ret = nfrParseUri(uri, &baseAddr, &transport);
  if (ret < 0)
    return LGMP_ERR_INVALID_ARGUMENT;

  int numChannels = LGMP_MAX_QUEUES + 1; /* +1 for metadata channel */

  /* Build NFRInitOpts from the parsed URI */
  struct NFRInitOpts opts;
  memset(&opts, 0, sizeof(opts));
  opts.apiVersion = FI_VERSION(2, 0);
  opts.flags      = 0;
  opts.maxQueues  = LGMP_MAX_QUEUES;

  uint16_t basePort = ntohs(baseAddr.sin_port);
  for (int i = 0; i < numChannels; ++i)
  {
    opts.addrs[i]          = baseAddr;
    opts.addrs[i].sin_port = htons(basePort + i);
    opts.transportTypes[i] = transport;
  }

  struct NFRResource * res[LGMP_MAX_QUEUES + 1];
  memset(res, 0, sizeof(res));
  ret = nfrResourceOpen(&opts, numChannels, res);
  if (ret < 0)
  {
    NFR_LOG_DEBUG("Failed to open resources: %d", ret);
    return LGMP_ERR_TRANSPORT_INIT_FAILURE;
  }

  PLGMPHost host = calloc(1, sizeof(*host));
  if (!host)
  {
    for (int i = 0; i < numChannels; ++i)
      nfrResourceClose(res[i]);
    return LGMP_ERR_NO_MEM;
  }

  struct LGMPFabricHost * fh = calloc(1, sizeof(*fh));
  if (!fh)
  {
    free(host);
    for (int i = 0; i < numChannels; ++i)
      nfrResourceClose(res[i]);
    return LGMP_ERR_NO_MEM;
  }

  host->iface    = &lgmpFabricHostInterface;
  host->internal = fh;

  fh->numChannels    = numChannels;
  fh->maxRegionAlloc = NETFR_MAX_BUFFER_SIZE;
  fh->maxTotalAlloc  = NETFR_MAX_MEM_USAGE;

  if (udataSize && udata)
  {
    host->udata = malloc(udataSize);
    if (!host->udata)
    {
      free(fh);
      free(host);
      for (int i = 0; i < numChannels; ++i)
        nfrResourceClose(res[i]);
      return LGMP_ERR_NO_MEM;
    }
    memcpy(host->udata, udata, udataSize);
    host->udataSize = udataSize;
  }

  host->sessionID = nfrGetRandomUint32();

  for (int i = 0; i < numChannels; ++i)
  {
    res[i]->parentRole  = NFR_ROLE_HOST;
    res[i]->parent.host = fh;
  }

  for (int i = 0; i < numChannels; ++i)
  {
    fh->channels[i].parent          = fh;
    fh->channels[i].res             = res[i];
    fh->channels[i].res->txCredits  = NETFR_CREDIT_COUNT;

    struct NFRCommBufInfo info       = nfrGetDefaultCommBufInfo();
    ret                              = nfrCommBufOpen(res[i], &info);
    if (ret < 0)
    {
      NFR_LOG_DEBUG("Failed to open communication buffer: %s (%d)",
                    fi_strerror(-ret), ret);
      goto cleanup_init;
    }

    ret = lgmpFabric_HostBind(res[i]);
    if (ret < 0)
    {
      NFR_LOG_DEBUG("Failed to bind: %s (%d)", fi_strerror(-ret), ret);
      goto cleanup_init;
    }
  }

  *result = host;
  return LGMP_OK;

cleanup_init:
  for (int i = 0; i < numChannels; ++i)
    nfrResourceClose(res[i]);
  free(host->udata);
  free(fh);
  free(host);
  return LGMP_ERR_TRANSPORT_INIT_FAILURE;
}

static void lgmpFabricHostFree(PLGMPHost * host)
{
  assert(host);
  if (!*host)
    return;

  struct LGMPFabricHost * fh = (*host)->internal;

  for (int i = 0; i < fh->numChannels; ++i)
  {
    if (fh->channels[i].res)
    {
      nfrCommBufClose(&fh->channels[i].res->commBuf);
      nfrResourceClose(fh->channels[i].res);
    }
  }

  free((*host)->udata);
  free(fh);
  free(*host);
  *host = NULL;
}

/** 
 * Send buffer state to the client on the queue's own channel.
 * The client callback allocates RDMA buffers on the channel that receives this
 * message, so it must be the queue's channel.
 */
static void lgmpFabric_HostSendBufferState(struct LGMPFabricHost * fh,
    struct LGMPFabricHostChannel * ch)
{
  if (!ch->res || ch->res->connState != NFR_CONN_STATE_CONNECTED)
    return;

  struct NFRMsgHostBufferState msg;
  nfrSetHeader(&msg.header, NFR_MSG_HOST_BUFFER_STATE);
  memset(msg.size, 0, sizeof(msg.size));

  int idx = 0;
  for (int i = 0; i < NETFR_MAX_MEM_REGIONS && idx < NETFR_MAX_MEM_REGIONS; ++i)
  {
    struct NFRMemory * m = &ch->res->memRegions[i];
    if (m->state >= MEM_STATE_AVAILABLE_UNSYNCED &&
        m->memType != NFR_MEM_TYPE_INTERNAL)
      msg.size[idx++] = m->size;
  }
  void * uData[NETFR_CALLBACK_USER_DATA_COUNT];
  uData[NFR_HOST_TX_CB_CHANNEL] = ch;
  nfrSendMessage(ch->res, &msg, sizeof(msg), nfrHostProcessInternalTx,
                 uData);
}

static LGMP_STATUS lgmpFabricHostProcess(PLGMPHost host)
{
  assert(host);

  struct LGMPFabricHost * fh = host->internal;

  for (int i = 0; i < fh->numChannels; ++i)
  {
    struct LGMPFabricHostChannel * ch = &fh->channels[i];
    struct NFRResource * res          = ch->res;
    if (!res)
      continue;

    /* Process EQ (connection events) */
    lgmpFabric_HostEQProcess(res);

    /* Process CQ and post receives */
    struct NFR_CallbackInfo cbInfo = {0};
    cbInfo.callback                       = nfrHostProcessInternalRx;
    cbInfo.uData[NFR_HOST_RX_CB_CHANNEL]  = ch;
    nfrChannelPoll(res, &cbInfo);
  }

  return LGMP_OK;
}

static LGMP_STATUS lgmpFabricHostQueueNew(PLGMPHost host,
    const struct LGMPQueueConfig config, PLGMPHostQueue * result)
{
  /* Queues map 1:1 to channels. Queue index N uses channel N+1
     (channel 0 is reserved for metadata). */
  assert(host);
  assert(result);

  struct LGMPFabricHost * fh = host->internal;

  if (host->numQueues == LGMP_MAX_QUEUES)
    return LGMP_ERR_NO_QUEUES;

  unsigned int idx = host->numQueues++;
  int channelIdx   = idx + NFR_QUEUE_CHANNEL_BASE;

  if (channelIdx >= fh->numChannels)
    return LGMP_ERR_NO_QUEUES;

  *result = &host->queues[idx];
  PLGMPHostQueue queue = *result;
  queue->ops      = &lgmpFabricHostQueueOps;
  queue->host     = host;
  queue->index    = idx;
  queue->internal = &fh->channels[channelIdx];

  (void)config;
  return LGMP_OK;
}

static bool lgmpFabricHostQueueHasSubs(PLGMPHostQueue queue)
{
  assert(queue);
  struct LGMPFabricHostChannel * ch = queue->internal;

  if (ch->res &&
      ch->res->connState == NFR_CONN_STATE_CONNECTED &&
      ch->subscribed)
    return true;

  return false;
}

static uint32_t lgmpFabricHostQueueNewSubs(PLGMPHostQueue queue)
{
  assert(queue);
  struct LGMPFabricHostChannel * ch = queue->internal;
  uint32_t total = ch->newSubs;
  ch->newSubs = 0;

  if (total > 0)
  {
    struct LGMPFabricHost * fh = queue->host->internal;
    lgmpFabric_HostSendBufferState(fh, ch);
  }

  return total;
}

static uint32_t lgmpFabricHostQueuePending(PLGMPHostQueue queue)
{
  (void)queue;
  return 0;
}

static LGMP_STATUS lgmpFabricHostQueuePostSized(PLGMPHostQueue queue,
    uint32_t udata, PLGMPMemory payload, int64_t payloadSize);

static LGMP_STATUS lgmpFabricHostQueuePost(PLGMPHostQueue queue,
    uint32_t udata, PLGMPMemory payload)
{
  return lgmpFabricHostQueuePostSized(queue, udata, payload, -1);
}

static LGMP_STATUS lgmpFabricHostQueuePostSized(PLGMPHostQueue queue,
    uint32_t udata, PLGMPMemory payload, int64_t payloadSize)
{
  assert(queue);
  assert(payload);

  if (payloadSize < 0)
    payloadSize = payload->size;
  else if ((uint64_t)payloadSize > payload->size)
    return LGMP_ERR_INVALID_SIZE;

  uint32_t size = (uint32_t)payloadSize;

  struct LGMPFabricHostChannel * ch = queue->internal;
  if (!ch->res || ch->res->connState != NFR_CONN_STATE_CONNECTED)
    return LGMP_ERR_INVALID_SESSION;

  struct NFRResource * res = ch->res;

  LGMPFabricMemory * localMem = payload->internal;
  if (!localMem)
    return LGMP_ERR_INVALID_ARGUMENT;

  /* Find an available remote memory region */
  struct NFRRemoteMemory * rmem = NULL;
  for (int i = 0; i < NETFR_MAX_MEM_REGIONS; ++i)
  {
    if (ch->clientRegions[i].state == NFR_RMEM_AVAILABLE &&
        ch->clientRegions[i].size >= size)
    {
      rmem = &ch->clientRegions[i];
      break;
    }
  }

  if (!rmem)
    return LGMP_ERR_QUEUE_FULL;

  struct NFR_CallbackInfo wCbInfo = {0};
  wCbInfo.callback                          = nfrHostProcessInternalWrite;
  wCbInfo.uData[NFR_HOST_WRITE_CB_CHANNEL]  = ch;
  wCbInfo.uData[NFR_HOST_WRITE_CB_LMEM]     = localMem;
  wCbInfo.uData[NFR_HOST_WRITE_CB_RMEM]     = rmem;
  wCbInfo.uData[NFR_HOST_WRITE_CB_LOFFSET]  = 0;
  wCbInfo.uData[NFR_HOST_WRITE_CB_ROFFSET]  = 0;
  wCbInfo.uData[NFR_HOST_WRITE_CB_LENGTH]   = (void *)(uintptr_t)size;

  struct NFR_CallbackInfo cbInfo = {0};
  cbInfo.callback                        = nfrHostProcessInternalTx;
  cbInfo.uData[NFR_HOST_TX_CB_CHANNEL]   = ch;

  struct NFR_TransferInfo ti = {0};
  ti.opType                 = NFR_OP_WRITE;
  ti.length                 = size;
  ti.udata                  = udata;
  ti.cbInfo                 = &cbInfo;
  ti.writeOpts.localMem     = localMem;
  ti.writeOpts.localOffset  = 0;
  ti.writeOpts.remoteMem    = rmem;
  ti.writeOpts.remoteOffset = 0;
  ti.writeOpts.writeCbInfo  = &wCbInfo;

  ssize_t ret = nfrPostTransfer(res, &ti);
  if (ret < 0)
    return LGMP_ERR_TRANSPORT_IO;

  return LGMP_OK;
}

static LGMP_STATUS lgmpFabricHostReadData(PLGMPHostQueue queue,
    void * data, size_t * size)
{
  assert(queue);
  assert(data);
  assert(size);

  struct LGMPFabricHostChannel * ch = queue->internal;
  if (!ch->res || ch->res->connState != NFR_CONN_STATE_CONNECTED)
    return LGMP_ERR_QUEUE_EMPTY;

  struct NFRFabricContext * ctx = NULL;
  int ret = nfrContextGetOldestMessage(ch->res, &ctx);
  if (ret > 0 && ctx)
  {
    struct NFRMsgClientData * msg = (struct NFRMsgClientData *) ctx->slot->data;
    if (msg->length <= *size)
    {
      memcpy(data, msg->data, msg->length);
      *size = msg->length;
      NFR_RESET_CONTEXT(ctx);
      return LGMP_OK;
    }
  }

  return LGMP_ERR_QUEUE_EMPTY;
}

static LGMP_STATUS lgmpFabricHostAckData(PLGMPHostQueue queue)
{
  assert(queue);

  struct LGMPFabricHostChannel * ch = queue->internal;
  if (!ch->res || ch->res->connState != NFR_CONN_STATE_CONNECTED)
    return LGMP_ERR_INVALID_SESSION;

  struct NFRMsgClientDataAck ack;
  nfrSetHeader(&ack.header, NFR_MSG_CLIENT_DATA_ACK);

  struct NFR_TransferInfo ti = {0};
  ti.opType                  = NFR_OP_INJECT;
  ti.data                    = &ack;
  ti.length                  = sizeof(ack);

  nfrPostTransfer(ch->res, &ti);
  return LGMP_OK;
}

static LGMP_STATUS lgmpFabricHostGetClientIDs(PLGMPHostQueue queue,
    uint32_t clientIDs[32], unsigned int * count)
{
  assert(queue);
  assert(count);
  (void)clientIDs;
  *count = 0;

  struct LGMPFabricHostChannel * ch = queue->internal;

  if (ch->res &&
      ch->res->connState == NFR_CONN_STATE_CONNECTED)
  {
    if (*count < 32)
      clientIDs[(*count)++] = 1;
  }

  return LGMP_OK;
}

static size_t lgmpFabricHostMemAvail(PLGMPHostQueue queue)
{
  /* Not limited by shared memory in fabric mode, report max. */
  (void)queue;
  return NETFR_MAX_BUFFER_SIZE;
}

static LGMP_STATUS lgmpFabricHostMemAllocAligned(PLGMPHostQueue queue,
    uint32_t size, uint32_t alignment, PLGMPMemory * result)
{
  assert(queue);
  assert(result);

  struct LGMPFabricHostChannel * ch = queue->internal;
  if (!ch->res)
    return LGMP_ERR_TRANSPORT_DISCONNECTED;

  PNFRMemory nfrMem = nfrRdmaAttach(
    ch->res, 0, size, 0, FI_READ | FI_WRITE | FI_REMOTE_WRITE, 
    NFR_MEM_TYPE_SYSTEM_MANAGED, MEM_STATE_AVAILABLE_UNSYNCED
  );
  if (!nfrMem)
    return LGMP_ERR_TRANSPORT_MEM_REG;

  *result = calloc(1, sizeof(**result));
  if (!*result)
  {
    nfrFreeMemory(&nfrMem);
    return LGMP_ERR_NO_MEM;
  }

  PLGMPMemory mem = *result;
  mem->queue    = queue;
  mem->offset   = 0;
  mem->size     = size;
  mem->mem      = nfrMem->addr;
  mem->internal = nfrMem;

  struct LGMPFabricHost * fh = queue->host->internal;
  lgmpFabric_HostSendBufferState(fh, ch);

  return LGMP_OK;
}

static LGMP_STATUS lgmpFabricHostMemAlloc(PLGMPHostQueue queue, uint32_t size,
    PLGMPMemory * result)
{
  // RDMA generally performs best with aligned pages, so we let the allocation
  // in NetFR choose the alignment, regardless of whether the user needs
  // page-aligned memory.
  return lgmpFabricHostMemAllocAligned(queue, size, 0, result);
}

static void lgmpFabricHostMemFree(PLGMPMemory * mem)
{
  assert(mem);
  if (!*mem)
    return;

  LGMPFabricMemory * nfrMem = (*mem)->internal;
  if (nfrMem)
    nfrFreeMemory(&nfrMem);

  free(*mem);
  *mem = NULL;
}

static void * lgmpFabricHostMemPtr(PLGMPMemory mem)
{
  assert(mem);
  return mem->mem;
}

const struct LGMPHostInterface lgmpFabricHostInterface =
{
  .type            = LGMP_MODULE_TYPE_FABRIC,
  .free            = lgmpFabricHostFree,
  .process         = lgmpFabricHostProcess,
  .queueNew        = lgmpFabricHostQueueNew,
};

const struct LGMPHostQueueOps lgmpFabricHostQueueOps =
{
  .queueHasSubs    = lgmpFabricHostQueueHasSubs,
  .queueNewSubs    = lgmpFabricHostQueueNewSubs,
  .queuePending    = lgmpFabricHostQueuePending,
  .queuePost       = lgmpFabricHostQueuePost,
  .QueuePostSized     = lgmpFabricHostQueuePostSized,
  .readData        = lgmpFabricHostReadData,
  .ackData         = lgmpFabricHostAckData,
  .getClientIDs    = lgmpFabricHostGetClientIDs,
  .memAvail        = lgmpFabricHostMemAvail,
  .memAlloc        = lgmpFabricHostMemAlloc,
  .memAllocAligned = lgmpFabricHostMemAllocAligned,
  .memFree         = lgmpFabricHostMemFree,
  .memPtr          = lgmpFabricHostMemPtr,
};
