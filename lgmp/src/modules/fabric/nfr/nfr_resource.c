// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Tim Dettmar <beanfacts@protonmail.com>

#include "nfr_resource.h"
#include "nfr_constants.h"
#include "nfr_mem.h"
#include "nfr_log.h"
#include "nfr_protocol.h"

#include <stddef.h>
#include <string.h>

inline static int nfrGetSlotBase(struct NFRCommBufInfo info, uint8_t type,
                                  int * slotCount)
{
  switch (type)
  {
    case NFR_OP_SEND:
      if (slotCount)
        *slotCount = info.txSlots;
      return NFR_TX_SLOT_BASE(info);
    case NFR_OP_RECV:
      if (slotCount)
        *slotCount = info.rxSlots;
      return NFR_RX_SLOT_BASE(info);
    case NFR_OP_WRITE:
      if (slotCount)
        *slotCount = info.writeSlots;
      return NFR_WRITE_SLOT_BASE(info);
    case NFR_OP_ACK:
      if (slotCount)
        *slotCount = info.ackSlots;
      return NFR_ACK_SLOT_BASE(info);
    default: 
      assert(!"Invalid operation type"); 
      if (slotCount)
        *slotCount = -1;
      return -1;
  }
}

struct NFRFabricContext * nfrContextGet(struct NFRResource * res,
                                         uint8_t opType, uint8_t * index)
{
  assert(res);
  int endIndex = 0;
  int i = nfrGetSlotBase(res->commBuf.info, opType, &endIndex);
  endIndex += i;
  assert(i >= 0);
  assert(endIndex <= NFR_TOTAL_SLOTS(res->commBuf.info));

  if (i < 0 || endIndex <= 0)
  {
    return NULL;
  }

  for (; i < endIndex; ++i)
  {
    if (res->commBuf.ctx[i].state == CTX_STATE_AVAILABLE)
    {
      NFR_LOG_TRACE("Allocating context %d for operation %d", i, opType);
      res->commBuf.ctx[i].state = CTX_STATE_ALLOCATED;
      if (index)
        *index = i;
      return res->commBuf.ctx + i;
    }
  }
  return NULL;
}

int nfrGetContextLocation(void * op_context, struct NFRResource * res,
                           uint8_t * typeOut)
{
  assert(op_context);
  assert(res);

  struct NFRFabricContext * ctx = op_context;

  if (ctx < res->commBuf.ctx ||
      ctx > res->commBuf.ctx + NFR_TOTAL_SLOTS(res->commBuf.info))
    return -EINVAL;

  int ctxIdx = (int) (ctx - res->commBuf.ctx);

  if (typeOut)
  {
    if (ctxIdx > NFR_ACK_SLOT_BASE(res->commBuf.info))
      *typeOut = NFR_OP_ACK;
    else if (ctxIdx > NFR_WRITE_SLOT_BASE(res->commBuf.info))
      *typeOut = NFR_OP_WRITE;
    else if (ctxIdx > NFR_RX_SLOT_BASE(res->commBuf.info))
      *typeOut = NFR_OP_RECV;
    else
      *typeOut = NFR_OP_SEND;
  }

  return ctxIdx;
}

int nfrPrintCQError(int logLevel, const char * func, const char * file,
                     int line, int channel, struct NFRResource * res,
                     struct fi_cq_err_entry * err)
{
  assert(res);
  assert(err);

  if (!res || !err)
    return -EINVAL;

  if (lgmpLogLevel > logLevel)
    return 0;

  char    errStr[128] = {0};
  uint8_t ctxType     = 0;
  int     ctxPos      = nfrGetContextLocation(err->op_context, res, &ctxType);
  if (ctxPos < 0)
  {
    NFR_LOG_ERROR("Failed to get context location: %d", ctxPos);
    return ctxPos;
  }

  const char * slotType;
  switch (ctxType)
  {
    case NFR_OP_SEND: slotType = "send"; break;
    case NFR_OP_RECV: slotType = "recv"; break;
    case NFR_OP_WRITE: slotType = "write"; break;
    case NFR_OP_ACK: slotType = "ack"; break;
    default: slotType = "unknown"; break;
  }

  lgmpLog(logLevel, func, file, line,
          "CQ Err ch[%d]->ctx[%d] (%s) / %s (%d) "
          "/ ProvErr: %s (%d)",
          channel, ctxPos, slotType, fi_strerror(-err->err), err->err,
          fi_cq_strerror(res->cq, err->prov_errno, err->err_data, errStr,
                         sizeof(errStr)),
          err->prov_errno);
  return err->err;
}

int nfrResourceCQProcess(struct NFRResource *       res,
                          struct NFRCompQueueEntry * cqe)
{
  assert(res);
  assert(cqe);

  int ret       = 0;
  int nComp     = 0;
  int totalComp = 0;

  struct NFRFabricContext * ctx = NULL;
  do
  {
    cqe->entry.data.op_context = 0;
    nComp                      = (int) fi_cq_read(res->cq, &cqe->entry.data, 1);
    if (nComp == 0 || nComp == -FI_EAGAIN)
    {
      return 0;
    }
    if (nComp < 0)
    {
      if (nComp == -FI_EAVAIL)
      {
        ret = (int) fi_cq_readerr(res->cq, &cqe->entry.err, 0);
        if (ret < 0)
          return ret;

        if (cqe->entry.err.err == FI_ECANCELED)
        {
          ctx = cqe->entry.err.op_context;
          ASSERT_CONTEXT_VALID(ctx);
          if (ctx)
            NFR_RESET_CONTEXT(ctx);
          continue;
        } else
        {
          cqe->isError = 1;
          return -FI_EAVAIL;
        }
      }
    }
    if (nComp > 0)
    {
      ctx = cqe->entry.data.op_context;
      ASSERT_CONTEXT_VALID(ctx);
      assert(ctx->state > CTX_STATE_AVAILABLE);
    }

    if (ctx->cbInfo.callback)
    {
      NFR_LOG_TRACE("Invoking callback for context %p", ctx);
      ctx->cbInfo.callback(ctx);
      memset(&ctx->cbInfo, 0, sizeof(ctx->cbInfo));
    } else
    {
      NFR_LOG_TRACE("No callback for context %p", ctx);
    }

    if (ctx->state != CTX_STATE_HAS_DATA)
      NFR_RESET_CONTEXT(ctx);

    ++totalComp;
  } while (nComp > 0);

  return totalComp;
}

/**
 * @brief Post receives on all free receive slots.
 * 
 * @param res     Resource to post receives on
 * @param cbInfo  Callback to invoke when receives complete
 * @return 
 */
int nfrResourceConsumeRxSlots(struct NFRResource *      res,
                               struct NFR_CallbackInfo * cbInfo)
{
  ASSERT_COMM_BUF_READY(res->commBuf);
  int totalRx = 0;
  do
  {
    struct NFR_TransferInfo ti = {0};
    ti.opType                  = NFR_OP_RECV;
    ti.cbInfo                  = cbInfo;

    ssize_t ret = nfrPostTransfer(res, &ti);
    if (ret < 0)
    {
      if (ret == -EAGAIN)
        return totalRx;
      return ret;
    }
  } while (1);
}

int nfrContextGetOldestMessage(struct NFRResource *       res,
                                struct NFRFabricContext ** ctx)
{
  ASSERT_COMM_BUF_READY(res->commBuf);
  int base = NFR_RX_SLOT_BASE(res->commBuf.info);

  int      haveData  = 0;
  uint32_t limSerial = 0;
  uint32_t sub       = 0;

  for (int i = base; i < base + res->commBuf.info.rxSlots; ++i)
  {
    struct NFRFabricContext * c = res->commBuf.ctx + i;
    if (c->state == CTX_STATE_HAS_DATA && c->slot->channelSerial > limSerial)
      limSerial = c->slot->channelSerial;
  }

  if (limSerial > ((uint32_t) -1) - 2048)
    sub = 4096;

  limSerial = (uint32_t) -1;

  for (int i = base; i < base + res->commBuf.info.rxSlots; ++i)
  {
    struct NFRFabricContext * c = res->commBuf.ctx + i;
    if (c->state == CTX_STATE_HAS_DATA)
    {
      if (!haveData || c->slot->channelSerial - sub < limSerial - sub)
      {
        limSerial = c->slot->channelSerial;
        *ctx      = res->commBuf.ctx + i;
        haveData  = 1;
      }
    }
  }

  return haveData;
}

int nfrContextDebugCheck(struct NFRResource * res)
{
  ASSERT_COMM_BUF_READY(res->commBuf);
  int base  = NFR_RX_SLOT_BASE(res->commBuf.info);
  int count = 0;
  for (int i = base; i < base + res->commBuf.info.rxSlots; ++i)
  {
    if (res->commBuf.ctx[i].state == CTX_STATE_ALLOCATED)
    {
      assert(!"Context in unexpected state");
      abort();
    }
  }
  return count;
}

int nfrResourceOpenSingle(const struct NFRInitOpts * opts, int index,
                           struct NFRResource ** result)
{
  struct NFRResource * res = calloc(1, sizeof(*res));
  if (!res)
  {
    NFR_LOG_DEBUG("Failed to allocate memory for resource");
    return -ENOMEM;
  }

  int             ret  = 0;
  struct fi_info *info = 0, *hints = fi_allocinfo();
  if (!hints)
  {
    NFR_LOG_DEBUG("Failed to allocate memory for hints");
    ret = -ENOMEM;
    goto free_struct;
  }

  switch (opts->transportTypes[index])
  {
    case NFR_TRANSPORT_TCP:
      hints->fabric_attr->prov_name = strdup("tcp");
      break;
    case NFR_TRANSPORT_RDMA:
      hints->fabric_attr->prov_name = strdup("verbs");
      break;
    default:
      assert(!"Invalid transport type");
      ret = -EINVAL;
      goto free_info;
  }

  NFR_LOG_DEBUG("Selecting transport %s", hints->fabric_attr->prov_name);

  hints->ep_attr->type = FI_EP_MSG;
  hints->domain_attr->mr_mode =
      FI_MR_VIRT_ADDR | FI_MR_ALLOCATED | FI_MR_PROV_KEY | FI_MR_LOCAL;
  hints->mode                = FI_RX_CQ_DATA;
  hints->caps                = FI_MSG | FI_RMA;
  hints->tx_attr->msg_order  = FI_ORDER_SAS | FI_ORDER_SAW;
  hints->tx_attr->comp_order = 0;
  hints->rx_attr->msg_order  = FI_ORDER_SAS | FI_ORDER_SAW;
  hints->rx_attr->comp_order = 0;
  hints->addr_format         = FI_SOCKADDR_IN;

  char         service[8];
  const char * node = inet_ntoa(opts->addrs[index].sin_addr);
  snprintf(service, sizeof(service), "%d", ntohs(opts->addrs[index].sin_port));

  uint64_t flags = opts->flags;

  NFR_LOG_DEBUG("Finding fabric for address %s:%s", node, service);

  for (int i = 0; i < 4; ++i)
  {
    /* We prefer manual progress, since LGMP requires calls to lgmp*Process
       at regular intervals anyway. Try registering with and without FI_HMEM
       for DMABUF support as well.
    */
    switch (i)
    {
      case 0:
        flags = opts->flags | FI_SOURCE | FI_NUMERICHOST | FI_HMEM;
        hints->domain_attr->progress = FI_PROGRESS_MANUAL;
        break;
      case 1:
        flags = opts->flags | FI_SOURCE | FI_NUMERICHOST | FI_HMEM;
        hints->domain_attr->progress = FI_PROGRESS_UNSPEC;
        break;
      case 2:
        flags = opts->flags | FI_SOURCE | FI_NUMERICHOST;
        hints->domain_attr->progress = FI_PROGRESS_MANUAL;
        break;
      case 3:
        flags = opts->flags | FI_SOURCE | FI_NUMERICHOST;
        hints->domain_attr->progress = FI_PROGRESS_UNSPEC;
        break;
      default:
        assert(!"Invalid iteration");
        break;
    }
    ret = fi_getinfo(opts->apiVersion, node, service, flags, hints, &info);
    if (ret < 0)
    {
      if (flags & FI_HMEM)
      {
        NFR_LOG_DEBUG("DMABUF-enabled fabric not found, retrying without");
        flags &= ~FI_HMEM;
        continue;
      }

      NFR_LOG_DEBUG("Unable to find suitable fabric: %s (%d)",
                    fi_strerror(-ret), ret);
      hints->src_addr     = 0;
      hints->src_addrlen  = 0;
      hints->dest_addr    = 0;
      hints->dest_addrlen = 0;
      fi_freeinfo(hints);
      goto free_struct;
    }
    break;
  }

  assert(info);

  int flag = 0;
  for (struct fi_info * tmp = info; tmp; tmp = tmp->next)
  {
    ret = fi_fabric(tmp->fabric_attr, &res->fabric, &res);
    if (ret < 0)
      continue;

    ret = fi_domain(res->fabric, info, &res->domain, &res);
    if (ret < 0)
    {
      fi_close(&res->fabric->fid);
      continue;
    }

    res->info = fi_dupinfo(tmp);
    if (!res->info)
    {
      ret = -ENOMEM;
      fi_freeinfo(info);
      goto free_fabric_domain;
    }
    flag = 1;
    break;
  }

  fi_freeinfo(info);
  if (!flag)
  {
    ret = -ENOENT;
    goto free_fabric_domain;
  }

  NFR_LOG_DEBUG("Using provider %s (%s), mr_mode=0x%x",
                res->info->fabric_attr->prov_name, res->info->fabric_attr->name,
                res->info->domain_attr->mr_mode);

  res->mrMode = res->info->domain_attr->mr_mode;

  struct fi_eq_attr eqAttr;
  memset(&eqAttr, 0, sizeof(eqAttr));
  eqAttr.wait_obj = FI_WAIT_UNSPEC;
  ret             = fi_eq_open(res->fabric, &eqAttr, &res->eq, &res);
  if (ret < 0)
    goto free_res_info;

  struct fi_cq_attr cqAttr;
  memset(&cqAttr, 0, sizeof(cqAttr));
  cqAttr.format = FI_CQ_FORMAT_DATA;
  cqAttr.size   = NETFR_TOTAL_CONTEXT_COUNT;
  ret           = fi_cq_open(res->domain, &cqAttr, &res->cq, &res);
  if (ret < 0)
    goto free_eq;

  for (int i = 0; i < NETFR_MAX_MEM_REGIONS; ++i)
  {
    res->memRegions[i].state = MEM_STATE_EMPTY;
  }

  *result = res;
  return 0;

  // free_cq:
  fi_close(&res->cq->fid);
free_eq:
  fi_close(&res->eq->fid);
free_res_info:
  fi_freeinfo(res->info);
free_fabric_domain:
  fi_close(&res->domain->fid);
  fi_close(&res->fabric->fid);
free_info:
  fi_freeinfo(info);
free_struct:
  free(res);
  return ret;
}

int nfrResourceOpen(const struct NFRInitOpts * opts, int numChannels,
                     struct NFRResource **      result)
{
  NFR_LOG_DEBUG("Opening resources");
  for (int i = 0; i < numChannels; ++i)
  {
    int ret = nfrResourceOpenSingle(opts, i, result + i);
    if (ret < 0)
    {
      NFR_LOG_DEBUG("Failed to open resource %d: %s (%d)", i, fi_strerror(-ret),
                    ret);
      for (int j = 0; j < i; ++j)
        nfrResourceClose(result[j]);
      return ret;
    }
  }

  return 0;
}

void nfrResourceClose(struct NFRResource * t)
{
  if (!t)
    return;
  nfrCommBufClose(&t->commBuf);
  if (t->info)
    fi_freeinfo(t->info);
  if (t->ep)
    fi_close(&t->ep->fid);
  if (t->pep)
    fi_close(&t->pep->fid);
  if (t->cq)
    fi_close(&t->cq->fid);
  if (t->eq)
    fi_close(&t->eq->fid);
  if (t->domain)
    fi_close(&t->domain->fid);
  if (t->fabric)
    fi_close(&t->fabric->fid);
  free(t);
}

int nfrCommBufOpen(struct NFRResource *          res,
                    const struct NFRCommBufInfo * hints)
{
  assert(res);
  assert(hints->txSlots);
  assert(hints->rxSlots);
  assert(hints->writeSlots);
  assert(hints->ackSlots);
  assert(hints->slotSize);

  if (res->commBuf.ctx)
  {
    NFR_LOG_DEBUG("Recreating communication buffer");
    nfrCommBufClose(&res->commBuf);
  } else
  {
    NFR_LOG_DEBUG("Creating communication buffer");
  }

  memcpy(&res->commBuf.info, hints, sizeof(*hints));
  uint32_t msgSlotCount =
      hints->txSlots + hints->rxSlots + hints->writeSlots + hints->ackSlots;
  uint64_t totalSize =
      NETFR_MESSAGE_MAX_SIZE *
      (hints->txSlots + hints->rxSlots + hints->ackSlots + hints->writeSlots);

  res->commBuf.memRegion =
      nfrRdmaAlloc(res, totalSize, FI_READ | FI_WRITE, MEM_STATE_RESERVED);
  if (!res->commBuf.memRegion)
  {
    NFR_LOG_DEBUG("Failed to allocate memory for communication buffer");
    return -ENOMEM;
  }

  res->commBuf.ctx = calloc(msgSlotCount, sizeof(*res->commBuf.ctx));
  if (!res->commBuf.ctx)
  {
    nfrFreeMemory(&res->commBuf.memRegion);
    return -ENOMEM;
  }

  struct NFRDataSlot * slots = res->commBuf.memRegion->addr;
  for (int i = 0; i < NFR_TOTAL_SLOTS(*hints); ++i)
  {
    slots[i].channelSerial = 0;
    slots[i].msgSerial     = 0;
    slots[i].maxDataSize   =
        NETFR_MESSAGE_MAX_SIZE - offsetof(struct NFRDataSlot, data);
    res->commBuf.ctx[i].slot =
        (struct NFRDataSlot *) ((uint8_t *) slots + i * NETFR_MESSAGE_MAX_SIZE);
    res->commBuf.ctx[i].parentResource = res;
    res->commBuf.ctx[i].state          = CTX_STATE_AVAILABLE;
  }

  return 0;
}

void nfrCommBufClose(struct NFRCommBuf * buf)
{
  if (!buf)
  {
    assert(!"Null buf passed to nfrCommBufClose");
    return;
  }

  free(buf->ctx);
  buf->ctx = 0;
  if (buf->memRegion)
  {
    nfrFreeMemory(&buf->memRegion);
    buf->memRegion = 0;
  }
}

ssize_t nfrSendMessage(struct NFRResource * res, const void * msg,
                        size_t len, NFR_Callback cb, 
                        void * uData[NETFR_CALLBACK_USER_DATA_COUNT])
{
  assert(res);
  assert(msg);
  assert(len);

  struct NFR_CallbackInfo cbInfo = {0};
  cbInfo.callback                = cb;
  if (uData)
  {
    for (int i = 0; i < NETFR_CALLBACK_USER_DATA_COUNT; ++i)
    {
      cbInfo.uData[i] = uData[i];
    }
  }

  struct NFR_TransferInfo ti = {0};
  ti.opType                  = NFR_OP_SEND_COPY;
  ti.data                    = (void *) msg;
  ti.cbInfo                  = &cbInfo;
  ti.length                  = len;

  return nfrPostTransfer(res, &ti);
}

int nfrEndpointSetup(struct NFRResource * res)
{
  assert(res);
  assert(res->ep);

  int ret = fi_ep_bind(res->ep, &res->eq->fid, 0);
  if (ret < 0)
  {
    NFR_LOG_DEBUG("Failed to bind EP to EQ: %s (%d)", fi_strerror(-ret), ret);
    goto closeEP;
  }

  ret = fi_ep_bind(res->ep, &res->cq->fid, FI_SEND | FI_RECV);
  if (ret < 0)
  {
    NFR_LOG_DEBUG("Failed to bind EP to CQ: %s (%d)", fi_strerror(-ret), ret);
    goto closeEP;
  }

  ret = fi_enable(res->ep);
  if (ret < 0)
  {
    NFR_LOG_DEBUG("Failed to enable EP: %s (%d)", fi_strerror(-ret), ret);
    goto closeEP;
  }

  return 0;

closeEP:
  fi_close(&res->ep->fid);
  res->ep = 0;
  return ret;
}

ssize_t nfrPostTransfer(struct NFRResource * res, struct NFR_TransferInfo * ti)
{
  assert(res);
  assert(ti);

  int                       ret;
  struct NFRFabricContext * ctx  = 0;
  struct NFRFabricContext * wctx = 0;
  struct fid_ep *           ep   = res->ep;

  if (!ep)
    return -ENOTCONN;

  NFR_LOG_TRACE("Posting transfer of type %d on resource %p", ti->opType, res);

  switch (ti->opType)
  {
    case NFR_OP_WRITE:
    {
      uint8_t ctxIdx, wctxIdx;

      ctx = nfrContextGet(res, NFR_OP_SEND, &ctxIdx);
      if (!ctx)
      {
        NFR_LOG_TRACE("Send context unavailable for write operation");
        return -EAGAIN;
      }

      wctx = nfrContextGet(res, NFR_OP_WRITE, &wctxIdx);
      if (!wctx)
      {
        NFR_LOG_TRACE("Write context unavailable");
        NFR_RESET_CONTEXT(ctx);
        return -EAGAIN;
      }

      NFR_LOG_TRACE("Using contexts %p and %p for write operation", ctx, wctx);

      assert(ti->length);

      struct NFR_TransferWrite * tiw = &ti->writeOpts;
      assert(tiw->localMem);
      assert(tiw->remoteMem);
      assert(tiw->localOffset + ti->length <= tiw->localMem->size);
      assert(tiw->remoteOffset + ti->length <= tiw->remoteMem->size);

      struct NFRMsgBufferUpdate * bu =
          (struct NFRMsgBufferUpdate *) ctx->slot->data;
      nfrSetHeader(&bu->header, NFR_MSG_BUFFER_UPDATE);
      bu->bufferIndex   = tiw->remoteMem->index;
      bu->payloadSize   = ti->length;
      bu->payloadOffset = tiw->remoteOffset;
      bu->udata         = ti->udata;

      assert(bu->bufferIndex < NETFR_MAX_MEM_REGIONS);

      void * lbuf = (void *) (uint8_t *) tiw->localMem->addr + tiw->localOffset;
      uint64_t rbuf = tiw->remoteMem->addr + tiw->remoteOffset;

      ret = fi_write(ep, lbuf, ti->length, fi_mr_desc(tiw->localMem->mr), 0,
                     rbuf, tiw->remoteMem->rkey, wctx);
      if (ret < 0)
      {
        NFR_LOG_DEBUG("Failed to post write: %s (%d)", fi_strerror(-ret), ret);
        NFR_RESET_CONTEXT(ctx);
        NFR_RESET_CONTEXT(wctx);
        return ret;
      }

      if (tiw->writeCbInfo)
        memcpy(&wctx->cbInfo, tiw->writeCbInfo, sizeof(*tiw->writeCbInfo));
      wctx->state = CTX_STATE_WAITING;

      ret = fi_send(ep, ctx->slot->data, sizeof(*bu),
                    fi_mr_desc(res->commBuf.memRegion->mr), 0, ctx);
      if (ret < 0)
      {
        NFR_LOG_DEBUG("Failed to post send: %s (%d)", fi_strerror(-ret), ret);
        NFR_RESET_CONTEXT(ctx);
        NFR_RESET_CONTEXT(wctx);
        int ret2 = (int) fi_cancel(&ep->fid, wctx);
        if (ret2 < 0)
          return ret2;
        return ret;
      }

      NFR_LOG_TRACE("Write op posted, ctx %p, wctx %p", ctx, wctx);
      tiw->remoteMem->state = NFR_RMEM_BUSY_LOCAL;
      break;
    }
    case NFR_OP_RECV:
    {
      ctx = nfrContextGet(res, NFR_OP_RECV, 0);
      if (!ctx)
        return -EAGAIN;

      ret = fi_recv(ep, ctx->slot->data, NETFR_MESSAGE_MAX_SIZE,
                    fi_mr_desc(res->commBuf.memRegion->mr), 0, ctx);
      if (ret < 0)
      {
        if (ret != -FI_EAGAIN)
          NFR_LOG_DEBUG("Failed to post receive: %s (%d)", fi_strerror(-ret),
                        ret);
        NFR_RESET_CONTEXT(ctx);
        return ret;
      }

      if (ti->cbInfo)
        memcpy(&ctx->cbInfo, ti->cbInfo, sizeof(*ti->cbInfo));

      NFR_LOG_TRACE("Receive op posted, ctx %p, wctx %p", ctx, wctx);
      break;
    }
    case NFR_OP_SEND:
    {
      assert(ti->length);
      assert(ti->context);
      assert(ti->length <= NETFR_MESSAGE_MAX_SIZE);
      assert(ti->context->slot);

      ctx = ti->context;
      ret = fi_send(ep, ctx->slot->data, ti->length,
                    fi_mr_desc(res->commBuf.memRegion->mr), 0, ctx);
      if (ret < 0)
      {
        NFR_RESET_CONTEXT(ctx);
        return ret;
      }
      break;
    }
    case NFR_OP_SEND_COPY:
    {
      ctx = nfrContextGet(res, NFR_OP_SEND, 0);
      if (!ctx)
      {
        NFR_LOG_TRACE("Send context unavailable for copied send");
        return -EAGAIN;
      }

      assert(ti->data);
      assert(ti->length);
      assert(ti->length <= NETFR_MESSAGE_MAX_SIZE);
      memcpy(ctx->slot->data, ti->data, ti->length);

      ret = fi_send(ep, ctx->slot->data, ti->length,
                    fi_mr_desc(res->commBuf.memRegion->mr), 0, ctx);
      if (ret < 0)
      {
        NFR_LOG_DEBUG("Failed to post send: %s (%d)", fi_strerror(-ret), ret);
        NFR_RESET_CONTEXT(ctx);
        return ret;
      }
      break;
    }
    case NFR_OP_INJECT:
    {
      assert(ti->data);
      assert(ti->length);
      assert(ti->length <= NETFR_MESSAGE_MAX_SIZE);
      assert(ti->length <= res->info->tx_attr->inject_size);

      ret = fi_inject(ep, ti->data, ti->length, 0);
      if (ret < 0)
      {
        NFR_LOG_DEBUG("Failed to inject: %s (%d), trying send",
                      fi_strerror(-ret), ret);
        struct NFR_TransferInfo ti2 = {0};
        memcpy(&ti2, ti, sizeof(ti2));
        ti2.opType   = NFR_OP_SEND_COPY;
        ssize_t ret2 = nfrPostTransfer(res, &ti2);
        if (ret2 < 0)
        {
          NFR_LOG_DEBUG("Failed to convert inject to send: %s (%d)",
                        fi_strerror(-ret2), ret2);
          return ret2;
        }
      }
      break;
    }
    default:
    {
      NFR_LOG_ERROR("Invalid operation type %d", ti->opType);
      assert(!"Invalid operation type");
      return -EINVAL;
    }
  }

  if (ctx)
  {
    if (ti->cbInfo)
      memcpy(&ctx->cbInfo, ti->cbInfo, sizeof(*ti->cbInfo));
    ctx->state = CTX_STATE_WAITING;
  }
  return 0;
}

void nfrDisconnectPeer(struct NFRResource * res)
{
  if (!res || !res->ep)
    return;
  res->connState = NFR_CONN_STATE_DISCONNECTED;
  fi_shutdown(res->ep, 0);
}

int nfrChannelPoll(struct NFRResource *      res,
                    struct NFR_CallbackInfo * rxCbInfo)
{
  if (!res || res->connState != NFR_CONN_STATE_CONNECTED)
    return 0;

  struct NFRCompQueueEntry cqe;
  memset(&cqe, 0, sizeof(cqe));
  nfrResourceCQProcess(res, &cqe);

  nfrResourceConsumeRxSlots(res, rxCbInfo);
  return 0;
}
