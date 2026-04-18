// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Tim Dettmar <beanfacts@protonmail.com>

#include "lgmp/client.h"
#include "lgmp/status.h"

#include "client_internal.h"
#include "modules/fabric/nfr/nfr_util.h"
#include "modules/module.h"
#include "fabric/module.h"

#include "fabric.h"
#include "nfr_uri.h"
#include "nfr_constants.h"
#include "nfr_log.h"
#include "nfr_mem.h"
#include "nfr_protocol.h"
#include "nfr_resource.h"
#include "client_callback.h"

#include <assert.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

// Internal helpers ------------------------------------------------------------

static int lgmpFabric_InitiateConnection(struct NFRResource * res,
                                         struct sockaddr_in * tgt)
{
  assert(res);
  assert(tgt);
  assert(res->connState == NFR_CONN_STATE_READY_TO_CONNECT);
  NFR_LOG_DEBUG("Initiating connection to %s:%d", inet_ntoa(tgt->sin_addr),
                ntohs(tgt->sin_port));

  struct fi_info * info = fi_dupinfo(res->info);
  if (info->dest_addr)
    free(info->dest_addr);
  info->dest_addr    = tgt;
  info->dest_addrlen = sizeof(*tgt);
  int ret            = fi_endpoint(res->domain, info, &res->ep, res);
  info->dest_addr    = 0;
  info->dest_addrlen = 0;
  fi_freeinfo(info);
  if (ret < 0)
  {
    NFR_LOG_DEBUG("Failed to create EP: %s (%d)", fi_strerror(-ret), ret);
    return ret;
  }

  ret = nfrEndpointSetup(res);
  if (ret < 0)
    return ret;

  struct NFRMsgClientHello hello;
  nfrSetHeader(&hello.header, NFR_MSG_CLIENT_HELLO);
  ret = fi_connect(res->ep, (void *) tgt, &hello, sizeof(hello));
  if (ret < 0)
  {
    NFR_LOG_DEBUG("Failed to connect: %s (%d)", fi_strerror(-ret), ret);
    fi_close(&res->ep->fid);
    res->ep = 0;
    return ret;
  }

  res->connState = NFR_CONN_STATE_CONNECTING;
  return 0;
}

static int lgmpFabric_CheckConnState(struct NFRResource * res)
{
  assert(res);

  struct NFRExtCMEntry entry;
  uint32_t             event;
  int ret = (int) fi_eq_read(res->eq, &event, &entry, sizeof(entry), 0);
  if (ret == 0 || ret == -FI_EAGAIN)
    return (res->connState == NFR_CONN_STATE_CONNECTED);

  if (ret == -FI_EAVAIL)
  {
    struct fi_eq_err_entry err;
    ret = (int) fi_eq_readerr(res->eq, &err, 0);
    if (ret < 0)
      return ret;
    switch (err.err)
    {
      case FI_ECONNREFUSED:
        NFR_LOG_DEBUG("Connection refused");
        return -err.err;
      case FI_EINPROGRESS: return 0;
      default:
        NFR_LOG_DEBUG("Error event: %s (%d)\n", fi_strerror(err.err), err.err);
        return -err.err;
    }
  }
  if (ret < 0)
    return ret;

  switch (event)
  {
    case FI_CONNECTED: res->connState = NFR_CONN_STATE_CONNECTED; return 1;
    case FI_SHUTDOWN:
    {
      fi_close(&res->ep->fid);
      res->ep        = 0;
      res->connState = NFR_CONN_STATE_DISCONNECTED;
      return -FI_ECONNRESET;
    }
    default: NFR_LOG_DEBUG("Unexpected event: %d", event); return -EIO;
  }
}

static int lgmpFabric_ClientResyncBufs(struct LGMPFabricClient * client,
                                       uint8_t index)
{
  assert(client);
  assert(index < client->numChannels);

  struct LGMPFabricClientChannel * ch  = client->channels + index;
  struct NFRResource *             res = ch->res;
  int                              nUpdated = 0;

  for (int i = 0; i < NETFR_MAX_MEM_REGIONS; ++i)
  {
    if (res->memRegions[i].state == MEM_STATE_AVAILABLE_UNSYNCED &&
        res->memRegions[i].memType != NFR_MEM_TYPE_INTERNAL)
    {
      NFR_LOG_DEBUG("Syncing buffer %d state", i);

      /* The RDMA (Verbs) transport uses the actual virtual address, while the
         TCP transport always uses 0. */
      struct NFRMsgClientBufferState msg;
      nfrSetHeader(&msg.header, NFR_MSG_CLIENT_BUFFER_STATE);
      msg.pageSize = 0;
      msg.addr     = (res->mrMode & FI_MR_VIRT_ADDR)
                         ? (uintptr_t) res->memRegions[i].addr
                         : 0;
      msg.size     = res->memRegions[i].size;
      msg.rkey     = fi_mr_key(res->memRegions[i].mr);
      msg.index    = i;

      void * uData[NFR_TOTAL_CB_UDATA_COUNT];
      uData[NFR_CLIENT_TX_CB_CHANNEL] = ch;

      ssize_t ret = nfrSendMessage(res, &msg, sizeof(msg),
                                   nfrClientProcessInternalTx, uData);
      if (ret < 0)
      {
        if (ret == -EAGAIN)
          return nUpdated;
        return ret;
      }

      NFR_LOG_DEBUG("Buffer %d-%d state sync message sent", index, i);
      res->memRegions[i].state = MEM_STATE_AVAILABLE;
      ++nUpdated;
    }
  }

  return nUpdated;
}

// LGMP vtable implementations -------------------------------------------------

LGMP_STATUS lgmpFabricClientInit(LGMPFabricClientInitOpts * opts, PLGMPClient * result)
{
  if (!opts || !opts->localUri || !opts->remoteUri || !result)
    return LGMP_ERR_INVALID_ARGUMENT;

  /* Parse the local URI */
  struct sockaddr_in localAddr;
  uint8_t            localTransport;
  int ret = nfrParseUri(opts->localUri, &localAddr, &localTransport);
  if (ret < 0)
    return LGMP_ERR_INVALID_ARGUMENT;

  /* Parse the peer URI */
  struct sockaddr_in peerAddr;
  uint8_t            peerTransport;
  ret = nfrParseUri(opts->remoteUri, &peerAddr, &peerTransport);
  if (ret < 0)
    return LGMP_ERR_INVALID_ARGUMENT;

  int numChannels = LGMP_MAX_QUEUES + 1; /* +1 for metadata channel */

  /* Build NFRInitOpts from the local URI */
  struct NFRInitOpts localOpts;
  memset(&localOpts, 0, sizeof(localOpts));
  localOpts.apiVersion = FI_VERSION(2, 0);
  localOpts.flags      = 0;
  localOpts.maxQueues  = LGMP_MAX_QUEUES;

  uint16_t localBasePort = ntohs(localAddr.sin_port);
  for (int i = 0; i < numChannels; ++i)
  {
    localOpts.addrs[i]          = localAddr;
    localOpts.addrs[i].sin_port = htons(localBasePort + i);
    localOpts.transportTypes[i] = localTransport;
  }

  struct NFRResource * res[LGMP_MAX_QUEUES + 1];
  memset(res, 0, sizeof(res));
  ret = nfrResourceOpen(&localOpts, numChannels, res);
  if (ret < 0)
  {
    NFR_LOG_DEBUG("Failed to open resources: %d", ret);
    return LGMP_ERR_TRANSPORT_INIT_FAILURE;
  }

  PLGMPClient client = calloc(1, sizeof(*client));
  if (!client)
  {
    ret = -ENOMEM;
    goto closeResourcesInit;
  }

  struct LGMPFabricClient * fc = calloc(1, sizeof(*fc));
  if (!fc)
  {
    ret = -ENOMEM;
    free(client);
    goto closeResourcesInit;
  }

  client->iface    = &lgmpFabricClientInterface;
  client->internal = fc;

  fc->numChannels    = numChannels;
  fc->maxRegionAlloc = NETFR_MAX_BUFFER_SIZE;
  fc->maxTotalAlloc  = NETFR_MAX_MEM_USAGE;
  fc->useDMABUF      = opts->enableDMABUF;

  for (int i = 0; i < numChannels; ++i)
  {
    res[i]->parentRole    = NFR_ROLE_CLIENT;
    res[i]->parent.client = fc;
  }

  for (int i = 0; i < numChannels; ++i)
  {
    fc->channels[i].parent         = fc;
    fc->channels[i].res            = res[i];
    fc->channels[i].res->txCredits = NETFR_CREDIT_COUNT;
    struct NFRCommBufInfo info      = nfrGetDefaultCommBufInfo();
    ret                             = nfrCommBufOpen(res[i], &info);
    if (ret < 0)
    {
      NFR_LOG_DEBUG("Failed to open communication buffer: %s (%d)",
                    fi_strerror(-ret), ret);
      goto closeResourcesInit;
    }
    fc->channels[i].res->connState = NFR_CONN_STATE_READY_TO_CONNECT;
  }

  /* Build peer info from the peer URI */
  memset(&fc->peerInfo, 0, sizeof(fc->peerInfo));
  fc->peerInfo.apiVersion = FI_VERSION(2, 0);
  fc->peerInfo.maxQueues  = LGMP_MAX_QUEUES;
  uint16_t peerBasePort   = ntohs(peerAddr.sin_port);
  for (int i = 0; i < numChannels; ++i)
  {
    fc->peerInfo.addrs[i]          = peerAddr;
    fc->peerInfo.addrs[i].sin_port = htons(peerBasePort + i);
    fc->peerInfo.transportTypes[i] = peerTransport;
  }

  *result = client;
  return LGMP_OK;

closeResourcesInit:
  for (int i = 0; i < numChannels; ++i)
    nfrResourceClose(res[i]);
  free(client);
  return LGMP_ERR_TRANSPORT_INIT_FAILURE;
}

static void lgmpFabricClientFree(PLGMPClient * client)
{
  assert(client);
  if (!*client)
    return;

  struct LGMPFabricClient * fc = (*client)->internal;

  for (int i = 0; i < fc->numChannels; ++i)
  {
    if (fc->channels[i].res)
    {
      nfrCommBufClose(&fc->channels[i].res->commBuf);
      nfrResourceClose(fc->channels[i].res);
    }
  }

  free(fc);
  free(*client);
  *client = NULL;
}

static LGMP_STATUS lgmpFabricClientSessionInit(PLGMPClient client,
    uint32_t * udataSize, uint8_t ** udata, uint32_t * clientID)
{
  assert(client);

  struct LGMPFabricClient * fc = client->internal;

  /* Drive the connection until all channels are connected. */
  for (int i = 0; i < fc->numChannels; ++i)
  {
    switch (fc->channels[i].res->connState)
    {
      case NFR_CONN_STATE_READY_TO_CONNECT:
      {
        int ret = lgmpFabric_InitiateConnection(fc->channels[i].res,
            &fc->peerInfo.addrs[i]);
        if (ret < 0)
          return LGMP_ERR_TRANSPORT_CONNECT_FAILURE;
        break;
      }
      case NFR_CONN_STATE_CONNECTING:
      {
        int ret = lgmpFabric_CheckConnState(fc->channels[i].res);
        if (ret < 0)
          return LGMP_ERR_TRANSPORT_CONNECT_FAILURE;
        break;
      }
      case NFR_CONN_STATE_CONNECTED:
        break;
      default:
        return LGMP_ERR_TRANSPORT_DISCONNECTED;
    }
  }

  /* Check all connected */
  int connOk = 0;
  for (int i = 0; i < fc->numChannels; ++i)
    if (fc->channels[i].res->connState == NFR_CONN_STATE_CONNECTED)
      ++connOk;

  if (connOk != fc->numChannels)
    return LGMP_ERR_TRANSPORT_CONNECT_FAILURE;

  /* Poll the metadata channel for the init data message from the host */
  if (!fc->initDataReceived)
  {
    struct LGMPFabricClientChannel * metaCh = &fc->channels[0];
    struct NFR_CallbackInfo cbInfo = {0};
    cbInfo.callback                        = nfrClientProcessInternalRx;
    cbInfo.uData[NFR_CLIENT_RX_CB_CHANNEL] = metaCh;
    nfrChannelPoll(metaCh->res, &cbInfo);

    if (!fc->initDataReceived)
      return LGMP_ERR_TRANSPORT_CONNECT_FAILURE;
  }

  client->id        = fc->clientID;
  client->sessionID = fc->sessionID;
  if (udataSize) *udataSize = fc->udataSize;
  if (udata)     *udata     = fc->udata;
  if (clientID)  *clientID  = fc->clientID;

  return LGMP_OK;
}

static bool lgmpFabricClientSessionValid(PLGMPClient client)
{
  assert(client);
  struct LGMPFabricClient * fc = client->internal;

  for (int i = 0; i < fc->numChannels; ++i)
  {
    if (fc->channels[i].res->connState != NFR_CONN_STATE_CONNECTED)
      return false;
  }
  return true;
}

static LGMP_STATUS lgmpFabricClientSubscribe(PLGMPClient client,
    uint32_t queueID, PLGMPClientQueue * result)
{
  assert(client);
  assert(result);

  struct LGMPFabricClient * fc = client->internal;

  /* Send a subscribe message on the queue's channel.
     Queue index maps to channel index + NFR_QUEUE_CHANNEL_BASE */
  int chIdx = queueID + NFR_QUEUE_CHANNEL_BASE;
  if (chIdx >= fc->numChannels)
    return LGMP_ERR_INVALID_ARGUMENT;

  struct LGMPFabricClientChannel * ch = &fc->channels[chIdx];
  if (ch->res->connState != NFR_CONN_STATE_CONNECTED)
    return LGMP_ERR_TRANSPORT_DISCONNECTED;

  struct NFRMsgClientSubscribe msg;
  nfrSetHeader(&msg.header, NFR_MSG_CLIENT_SUBSCRIBE);
  msg.queueID = queueID;

  void * uData[NFR_TOTAL_CB_UDATA_COUNT];
  uData[NFR_CLIENT_TX_CB_CHANNEL] = ch;

  ssize_t ret = nfrSendMessage(ch->res, &msg, sizeof(msg),
                               nfrClientProcessInternalTx, uData);
  if (ret < 0)
    return LGMP_ERR_TRANSPORT_IO;

  *result = &client->queues[queueID];
  PLGMPClientQueue q = *result;
  q->ops    = &lgmpFabricClientQueueOps;
  q->client = client;
  q->index  = queueID;
  q->id     = queueID;
  q->position = 0;

  return LGMP_OK;
}

static LGMP_STATUS lgmpFabricClientUnsubscribe(PLGMPClientQueue * result)
{
  assert(result);
  if (!*result)
    return LGMP_OK;

  PLGMPClientQueue q = *result;
  PLGMPClient client = q->client;
  struct LGMPFabricClient * fc = client->internal;

  /* Send an unsubscribe message on the queue's channel */
  int chIdx = q->index + NFR_QUEUE_CHANNEL_BASE;
  if (chIdx < fc->numChannels)
  {
    struct LGMPFabricClientChannel * ch = &fc->channels[chIdx];
    if (ch->res->connState == NFR_CONN_STATE_CONNECTED)
    {
      struct NFRMsgClientUnsubscribe msg;
      nfrSetHeader(&msg.header, NFR_MSG_CLIENT_UNSUBSCRIBE);
      msg.queueID = q->id;

      void * uData[NETFR_CALLBACK_USER_DATA_COUNT] = {0};
      uData[NFR_CLIENT_TX_CB_CHANNEL] = ch;
      nfrSendMessage(ch->res, &msg, sizeof(msg), nfrClientProcessInternalTx,
                     uData);
    }
  }

  memset(*result, 0, sizeof(struct LGMPClientQueue));
  *result = NULL;
  return LGMP_OK;
}

static LGMP_STATUS lgmpFabricClientAdvanceToLast(PLGMPClientQueue queue)
{
  // No ring to advance in fabric mode
  (void)queue;
  return LGMP_OK;
}

static inline void lgmpFabric_PollChannel(
    struct LGMPFabricClient * fc, int i)
{
  struct LGMPFabricClientChannel * ch  = &fc->channels[i];
  struct NFRResource *             res = ch->res;

  uint32_t expected = 0;
  if (!atomic_compare_exchange_weak_explicit(&ch->lock, &expected, 1, 
      memory_order_acquire, memory_order_relaxed))
  {
    return;
  }

  if (res->connState != NFR_CONN_STATE_CONNECTED)
  {
    atomic_store_explicit(&ch->lock, 0, memory_order_release);
    return;
  }

  /* Check connection state */
  int ret = lgmpFabric_CheckConnState(res);
  if (ret < 0)
  {
    atomic_store_explicit(&ch->lock, 0, memory_order_release);
    return;
  }

  /* Resync buffers */
  lgmpFabric_ClientResyncBufs(fc, i);

  /* Process CQ and post receives */
  struct NFR_CallbackInfo cbInfo = {0};
  cbInfo.callback                         = nfrClientProcessInternalRx;
  cbInfo.uData[NFR_CLIENT_RX_CB_CHANNEL]  = ch;
  nfrChannelPoll(res, &cbInfo);
  atomic_store_explicit(&ch->lock, 0, memory_order_release);
}

static LGMP_STATUS lgmpFabricClientProcess(PLGMPClientQueue queue,
    PLGMPMessage result)
{
  assert(queue);
  assert(result);

  PLGMPClient client = queue->client;
  struct LGMPFabricClient * fc = client->internal;

  int chIdx = queue->index + NFR_QUEUE_CHANNEL_BASE;
  if (chIdx >= fc->numChannels)
    return LGMP_ERR_QUEUE_EMPTY;

  lgmpFabric_PollChannel(fc, NFR_METADATA_CHANNEL_INDEX);
  lgmpFabric_PollChannel(fc, chIdx);

  struct LGMPFabricClientChannel * qch  = &fc->channels[chIdx];
  struct NFRResource *             qres = qch->res;

  if (!qres || qres->connState != NFR_CONN_STATE_CONNECTED)
    return LGMP_ERR_QUEUE_EMPTY;

  /* Check for a readable message (send-based) */
  struct NFRFabricContext * ctx = 0;
  int ret = nfrContextGetOldestMessage(qres, &ctx);
  if (ret > 0 && ctx)
  {
    struct NFRMsgHostData * msg = (struct NFRMsgHostData *) ctx->slot->data;
    result->udata   = 0;
    result->size    = msg->length;
    result->mem     = msg->data;
    qch->activeRxCtx = ctx;
    return LGMP_OK;
  }

  /* Check for RDMA-written data in memory regions */
  for (int j = 0; j < NETFR_MAX_MEM_REGIONS; ++j)
  {
    struct NFRMemory * mem = &qres->memRegions[j];
    if (mem->state == MEM_STATE_HAS_DATA)
    {
      result->udata = mem->udata;
      result->size  = mem->payloadLength;
      result->mem   = (uint8_t *)mem->addr + mem->payloadOffset;
      result->dmaFD = mem->dmaFd;
      mem->state    = MEM_STATE_AVAILABLE_UNSYNCED;
      return LGMP_OK;
    }
  }

  return LGMP_ERR_QUEUE_EMPTY;
}

static LGMP_STATUS lgmpFabricClientMessageDone(PLGMPClientQueue queue)
{
  assert(queue);

  PLGMPClient client = queue->client;
  struct LGMPFabricClient * fc = client->internal;

  int chIdx = queue->index + NFR_QUEUE_CHANNEL_BASE;
  if (chIdx >= fc->numChannels)
    return LGMP_OK;

  struct LGMPFabricClientChannel * ch = &fc->channels[chIdx];
  if (ch->activeRxCtx)
  {
    NFR_RESET_CONTEXT(ch->activeRxCtx);
    ch->activeRxCtx = NULL;
  }

  return LGMP_OK;
}

static LGMP_STATUS lgmpFabricClientSendData(PLGMPClientQueue queue,
    const void * data, uint32_t size, uint32_t * serial)
{
  assert(queue);
  assert(data);

  PLGMPClient client = queue->client;
  struct LGMPFabricClient * fc = client->internal;

  if (size > NETFR_MESSAGE_MAX_PAYLOAD_SIZE)
    return LGMP_ERR_INVALID_SIZE;

  /* Use the queue's channel */
  int chIdx = queue->index + NFR_QUEUE_CHANNEL_BASE;
  if (chIdx >= fc->numChannels)
    return LGMP_ERR_TRANSPORT_DISCONNECTED;

  struct LGMPFabricClientChannel * ch = fc->channels + chIdx;
  if (ch->res->connState != NFR_CONN_STATE_CONNECTED)
    return LGMP_ERR_TRANSPORT_DISCONNECTED;

  if (ch->res->txCredits < NETFR_RESERVED_CREDIT_COUNT)
    return LGMP_ERR_QUEUE_FULL;

  struct NFRResource * res = ch->res;
  ASSERT_COMM_BUF_READY(res->commBuf);

  struct NFRFabricContext * ctx = nfrContextGet(res, NFR_OP_SEND, 0);
  if (!ctx)
    return LGMP_ERR_QUEUE_FULL;

  struct NFRMsgClientData * msg = (struct NFRMsgClientData *) ctx->slot->data;
  nfrSetHeader(&msg->header, NFR_MSG_CLIENT_DATA);
  msg->length        = size;
  msg->msgSerial     = ++ch->msgSerial;
  msg->channelSerial = ++ch->channelSerial;
  msg->udata         = 0;
  memcpy(msg->data, data, size);

  struct NFR_CallbackInfo cbInfo = {0};
  cbInfo.callback                = nfrClientProcessInternalTx;
  cbInfo.uData[NFR_CLIENT_TX_CB_CHANNEL] = ch;

  struct NFR_TransferInfo ti = {0};
  ti.opType                  = NFR_OP_SEND;
  ti.context                 = ctx;
  ti.cbInfo                  = &cbInfo;
  ti.length                  = size + offsetof(struct NFRMsgClientData, data);

  ssize_t ret = nfrPostTransfer(res, &ti);
  if (ret < 0)
  {
    NFR_RESET_CONTEXT(ctx);
    --ch->msgSerial;
    --ch->channelSerial;
    return LGMP_ERR_TRANSPORT_IO;
  }

  --ch->res->txCredits;

  if (serial)
    *serial = ch->msgSerial;

  return LGMP_OK;
}

static LGMP_STATUS lgmpFabricClientGetSerial(PLGMPClientQueue queue,
    uint32_t * serial)
{
  assert(queue);
  assert(serial);

  PLGMPClient client = queue->client;
  struct LGMPFabricClient * fc = client->internal;

  int chIdx = queue->index + NFR_QUEUE_CHANNEL_BASE;
  if (chIdx >= fc->numChannels)
    return LGMP_ERR_INVALID_ARGUMENT;

  *serial = fc->channels[chIdx].channelSerial;
  return LGMP_OK;
}

static LGMP_STATUS lgmpFabricClientMemAttach(PLGMPClientQueue queue,
    void * mem, uint64_t size, int dmaFd)
{
  assert(queue);
  assert(mem);

  PLGMPClient client = queue->client;
  struct LGMPFabricClient * fc = client->internal;

  if (size > fc->maxRegionAlloc)
    return LGMP_ERR_INVALID_SIZE;

  /* Use the channel matching the queue index */
  int chIdx = queue->index + NFR_QUEUE_CHANNEL_BASE;
  if (chIdx >= fc->numChannels)
    return LGMP_ERR_INVALID_ARGUMENT;

  struct NFRResource * res = fc->channels[chIdx].res;
  if (!res || res->connState != NFR_CONN_STATE_CONNECTED)
    return LGMP_ERR_TRANSPORT_DISCONNECTED;

  uint8_t memType = (dmaFd < 0)
      ? NFR_MEM_TYPE_USER_MANAGED
      : NFR_MEM_TYPE_USER_MANAGED_DMABUF;

  if (dmaFd)
  {
#if defined(__linux__) && defined(ENABLE_FABRIC_DMABUF) && defined(_GNU_SOURCE) \
  && FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION) >= FI_VERSION(1, 20)
    uint64_t perm = FI_READ | FI_WRITE | FI_REMOTE_WRITE;
    PNFRMemory out = 0;
    int ret = nfrRdmaAttachDMABUF(res, mem, size, perm, dmaFd, memType, &out);
    if (ret < 0)
    {
      NFR_LOG_DEBUG("Failed to attach DMABUF memory: %s (%d)", fi_strerror(-ret),
                    ret);
      return LGMP_ERR_TRANSPORT_MEM_REG;
    }
#else
    (void)memType;
    return LGMP_ERR_NOT_SUPPORTED;
#endif
  }
  else
  {
    PNFRMemory nfrMem = nfrRdmaAttach(res, mem, size, 0,
        FI_READ | FI_WRITE | FI_REMOTE_WRITE, memType,
        MEM_STATE_AVAILABLE_UNSYNCED);
    if (!nfrMem)
      return LGMP_ERR_TRANSPORT_MEM_REG;
  }

  return LGMP_OK;
}

const struct LGMPClientInterface lgmpFabricClientInterface =
{
  .type         = LGMP_MODULE_TYPE_FABRIC,
  .free         = lgmpFabricClientFree,
  .sessionInit  = lgmpFabricClientSessionInit,
  .sessionValid = lgmpFabricClientSessionValid,
  .subscribe    = lgmpFabricClientSubscribe,
  .unsubscribe  = lgmpFabricClientUnsubscribe,
};

const struct LGMPClientQueueOps lgmpFabricClientQueueOps =
{
  .advanceToLast = lgmpFabricClientAdvanceToLast,
  .process       = lgmpFabricClientProcess,
  .messageDone   = lgmpFabricClientMessageDone,
  .sendData      = lgmpFabricClientSendData,
  .getSerial     = lgmpFabricClientGetSerial,
  .memAttach     = lgmpFabricClientMemAttach,
};
