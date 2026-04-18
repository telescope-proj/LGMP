// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Tim Dettmar <beanfacts@protonmail.com>

#ifndef NETFR_PRIVATE_RESOURCE_H
#define NETFR_PRIVATE_RESOURCE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <assert.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdlib.h>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include <rdma/fabric.h>
#include <rdma/fi_cm.h>
#include <rdma/fi_domain.h>
#include <rdma/fi_eq.h>
#include <rdma/fi_rma.h>

#include "nfr_constants.h"
#include "lgmp/lgmp.h"

/* ---- Public Types ---- */

typedef void (*NFRCallback)(const void ** uData);

typedef struct NFRResource *     PNFRResource;
typedef struct NFRClient *       PNFRClient;
typedef struct NFRHost *         PNFRHost;
typedef struct NFRMemory *       PNFRMemory;
typedef struct NFRRemoteMemory * PNFRRemoteMemory;

extern int lgmpLogLevel;

#define BETWEEN_EXCL(x, a, b) ((x) > (a) && (x) < (b))
#define BETWEEN_INCL(x, a, b) ((x) >= (a) && (x) <= (b))

struct NFRRemoteMemory {
  struct NFRResource * parentResource;
  void *               activeContext;
  uint64_t             addr;
  uint64_t             size;
  uint64_t             rkey;
  uint32_t             align;
  uint8_t              state;
  uint8_t              index;
};

struct NFRCallbackInfo {
  NFRCallback callback;
  // This is passed to the callback when it is invoked
  void *      uData[NETFR_CALLBACK_USER_DATA_COUNT];
};

enum NFRTransportType {
  NFR_TRANSPORT_TCP  = 1, // Libfabric TCP MSG provider
  NFR_TRANSPORT_RDMA = 2, // Libfabric Verbs MSG provider
  NFR_TRANSPORT_MAX
};

struct NFRInitOpts {
  uint32_t           apiVersion;
  uint64_t           flags;
  uint8_t            maxQueues;
  struct sockaddr_in addrs[LGMP_MAX_QUEUES + 1];
  uint8_t            transportTypes[LGMP_MAX_QUEUES + 1];
};

void nfrReleaseMemory(PNFRMemory * mem);
void nfrAckBuffer(PNFRMemory mem);
void nfrFreeMemory(PNFRMemory * mem);

/* ---- Internal Types ---- */

struct NFRFabricContext;

/**
 * @brief NetFR internal callback handle.
 *
 * @param ctx The context associated with the callback
 *
 */
typedef void (*NFR_Callback)(struct NFRFabricContext * ctx);

/* INTERNAL callback structure */
struct NFR_CallbackInfo {
  // The callback to invoke when the operation completes
  NFR_Callback callback;
  /* The user data to be made available to the callback. The elements can refer
     to arbitrary user data, and are not interpreted in any way by the internal
     queue manager. You can allocate NFR_CallbackInfo on the stack; the values
     of this array are copied into the context when the operation is posted.

     When the callback is invoked, the context, which contains the uData array,
     is passed as the first argument. */
  void *       uData[NFR_TOTAL_CB_UDATA_COUNT];
};

struct NFRResource;

struct NFRFabricContext {
  struct NFRResource *    parentResource;
  uint8_t                 state;
  struct NFR_CallbackInfo cbInfo;
  struct NFRDataSlot *    slot;
};

struct NFRCompQueueEntry {
  union {
    struct fi_cq_data_entry data;
    struct fi_cq_err_entry  err;
  } entry;
  uint8_t isError;
};

struct NFRDataSlot {
  uint32_t msgSerial;
  uint32_t channelSerial;
  uint32_t maxDataSize;
  alignas(64) char data[0];
};

struct NFRExtCMEntry {
  fid_t            fid;
  struct fi_info * info;
  uint8_t          data[NETFR_CM_MESSAGE_MAX_SIZE];
};

enum NFRMemoryType {
  NFR_MEM_TYPE_INTERNAL,

  NFR_MEM_INDEX_SYSTEM_TYPES_START,
  NFR_MEM_TYPE_SYSTEM_MANAGED,
  NFR_MEM_TYPE_SYSTEM_MANAGED_DMABUF,
  NFR_MEM_INDEX_SYSTEM_TYPES_END,

  NFR_MEM_INDEX_EXTERNAL_TYPES_START,
  NFR_MEM_TYPE_USER_MANAGED,
  NFR_MEM_TYPE_USER_MANAGED_DMABUF,
  NFR_MEM_INDEX_EXTERNAL_TYPES_END
};

static inline int nfrMemIsExternal(enum NFRMemoryType type)
{
  return type > NFR_MEM_INDEX_EXTERNAL_TYPES_START;
}

static inline int nfrMemIsInternal(enum NFRMemoryType type)
{
  return !nfrMemIsExternal(type);
}

struct NFRMemory {
  struct NFRResource * parentResource;
  void *               addr;
  struct fid_mr *      mr;
  uint64_t             udata;
  uint64_t             size;
  uint32_t             writeSerial;   // Message id relative to other writes
  uint32_t             channelSerial; // Message id relative to all messages
  uint32_t             payloadOffset;
  uint32_t             payloadLength;
  enum NFRMemoryType   memType; // Memory allocation type
  uint8_t              index;
  uint8_t              state;
  uint8_t              refCount;
  int                  dmaFd; // DMABUF fd if enabled
};

struct NFRCommBufInfo {
  uint32_t txSlots;
  uint32_t rxSlots;
  uint32_t writeSlots;
  uint32_t ackSlots;
  uint32_t slotSize; // Size of a single data slot
};

struct NFRCommBuf {
  struct NFRMemory *        memRegion;
  struct NFRFabricContext * ctx;
  struct NFRCommBufInfo     info;
};

struct LGMPFabricHost;
struct LGMPFabricClient;

enum NFRParentRole {
  NFR_ROLE_HOST,
  NFR_ROLE_CLIENT
};

struct NFRResource {
  enum NFRParentRole  parentRole;
  union {
    struct LGMPFabricHost   * host;
    struct LGMPFabricClient * client;
  }                   parent;
  struct fi_info *    info;
  struct fid_fabric * fabric;
  struct fid_domain * domain;
  struct fid_cq *     cq;
  struct fid_pep *    pep;
  struct fid_eq *     eq;
  struct fid_ep *     ep;
  struct NFRCommBuf   commBuf;
  struct NFRMemory    memRegions[NETFR_MAX_MEM_REGIONS];
  uint64_t            rkeyCounter;
  uint64_t            lastPing;
  uint32_t            txCredits;
  uint32_t            mrMode;
  uint8_t             connState;
};

/* ---- Assertion Macros ---- */

#define ASSERT_COMM_BUF_READY(cb)                                              \
  assert(cb.memRegion);                                                        \
  assert(cb.ctx);                                                              \
  assert(cb.info.txSlots);                                                     \
  assert(cb.info.rxSlots);                                                     \
  assert(cb.info.writeSlots);                                                  \
  assert(cb.info.ackSlots);                                                    \
  assert(cb.info.slotSize);

#define ASSERT_CONTEXT_VALID(fctx)                                             \
  assert(fctx);                                                                \
  assert(fctx->parentResource);                                                \
  ASSERT_COMM_BUF_READY(fctx->parentResource->commBuf)

/* ---- Callback Cast Macros  ---- */

#define NFR_CAST_UDATA(type, name, ctx, idx)                                   \
  assert(ctx->cbInfo.uData[idx]);                                              \
  assert(idx < NFR_TOTAL_CB_UDATA_COUNT);                                      \
  type name = ((type) ctx->cbInfo.uData[idx])

#define NFR_CAST_UDATA_UNCHECKED(type, name, ctx, idx)                         \
  type name = ((type) ctx->cbInfo.uData[idx])

#define NFR_CAST_UDATA_NUM(type, name, ctx, idx)                               \
  assert(idx < NFR_TOTAL_CB_UDATA_COUNT);                                      \
  type name = ((type) (uintptr_t) ctx->cbInfo.uData[idx])

/* ---- Slot Index Macros ---- */

#define NFR_TX_SLOT_BASE(info)    0
#define NFR_RX_SLOT_BASE(info)    ((info).txSlots)
#define NFR_WRITE_SLOT_BASE(info) (NFR_RX_SLOT_BASE(info) + (info).rxSlots)
#define NFR_ACK_SLOT_BASE(info) (NFR_WRITE_SLOT_BASE(info) + (info).writeSlots)
#define NFR_TOTAL_SLOTS(info)   (NFR_ACK_SLOT_BASE(info) + (info).ackSlots)

#define GET_DATA_SLOT_OFFSET(resource, slot)                                   \
  ((uintptr_t) slot->data - (uintptr_t) resource->commBuf->memRegion->addr)

#define NFR_RESET_CONTEXT(ctx)                                                 \
  do                                                                           \
  {                                                                            \
    assert(ctx);                                                               \
    if ((ctx)->state != CTX_STATE_ACK_ONLY)                                    \
      (ctx)->state = CTX_STATE_AVAILABLE;                                      \
  } while (0)

#define NFR_PRINT_CQ_ERROR(logLevel, ch, parent, err)                          \
  nfrPrintCQError(logLevel, __func__, __FILE__, __LINE__,                     \
                   (int) (ch - parent->channels), res, err)

static_assert(NFR_TOTAL_CB_UDATA_COUNT - NETFR_CALLBACK_USER_DATA_COUNT >= 8,
              "At least 8 user data slots must be available for internal use");

/* ---- Transfer Types (formerly nfr.h) ---- */

struct NFR_TransferWrite {
  PNFRMemory                      localMem;
  uint64_t                        localOffset;
  PNFRRemoteMemory                remoteMem;
  uint64_t                        remoteOffset;
  const struct NFR_CallbackInfo * writeCbInfo;
};

struct NFR_TransferInfo {
  /*
    See NFR_OP_* in nfrconstants.h
  */
  uint8_t                         opType;
  uint64_t                        length;
  uint64_t                        udata;
  /* Only used for copied sends */
  void *                          data;
  /*  Context pointer.
      - Sends:        required
      - Copied Sends: ignored
      - Receives:     optional
      - Writes:       ignored
  */
  struct NFRFabricContext *       context;
  const struct NFR_CallbackInfo * cbInfo;
  struct NFR_TransferWrite        writeOpts;
};

/* ---- Callback uData Index Enums ---- */

enum NFRHostRxCbArgs {
  NFR_HOST_RX_CB_CHANNEL = NFR_INTERNAL_CB_INDEX
};

enum NFRHostTxCbArgs {
  NFR_HOST_TX_CB_CHANNEL = NFR_INTERNAL_CB_INDEX
};

enum NFRHostWriteCbArgs {
  NFR_HOST_WRITE_CB_CHANNEL  = NFR_INTERNAL_CB_INDEX,
  NFR_HOST_WRITE_CB_LMEM,
  NFR_HOST_WRITE_CB_RMEM,
  NFR_HOST_WRITE_CB_LOFFSET,
  NFR_HOST_WRITE_CB_ROFFSET,
  NFR_HOST_WRITE_CB_LENGTH,
  NFR_HOST_WRITE_CB_USER_CB
};

enum NFRClientRxCbArgs {
  NFR_CLIENT_RX_CB_CHANNEL = NFR_INTERNAL_CB_INDEX
};

enum NFRClientTxCbArgs {
  NFR_CLIENT_TX_CB_CHANNEL = NFR_INTERNAL_CB_INDEX
};

/* ---- Function Declarations ---- */

ssize_t nfrPostTransfer(struct NFRResource *      res,
                         struct NFR_TransferInfo * ti);

/**
 * @brief Send a message using NFR_OP_SEND_COPY with a standard internal
 *        callback. This replaces the common pattern of building an
 *        NFR_CallbackInfo + NFR_TransferInfo for simple copied sends.
 */
ssize_t nfrSendMessage(struct NFRResource * res, const void * msg,
                        size_t len, NFR_Callback cb, 
                        void * uData[NETFR_CALLBACK_USER_DATA_COUNT]);
/**
 * @brief Set up an active endpoint by binding it to the EQ and CQ, then
 *        enabling it. Common setup shared by both client and host.
 */
int nfrEndpointSetup(struct NFRResource * res);

int nfrResourceCQProcess(struct NFRResource *       res,
                          struct NFRCompQueueEntry * cqe);

int nfrResourceConsumeRxSlots(struct NFRResource *      res,
                               struct NFR_CallbackInfo * cbInfo);

int nfrContextGetOldestMessage(struct NFRResource *       res,
                                struct NFRFabricContext ** ctx);

int nfrResourceOpenSingle(const struct NFRInitOpts * opts, int index,
                           struct NFRResource ** result);

int nfrResourceOpen(const struct NFRInitOpts * opts, int numChannels,
                     struct NFRResource **      result);

void nfrResourceClose(struct NFRResource * t);

struct NFRFabricContext * nfrContextGet(struct NFRResource * res,
                                         uint8_t opType, uint8_t * index);

int nfrCommBufOpen(struct NFRResource *          res,
                    const struct NFRCommBufInfo * hints);

void nfrCommBufClose(struct NFRCommBuf * buf);

void nfrDisconnectPeer(struct NFRResource * res);

int nfrChannelPoll(struct NFRResource *      res,
                    struct NFR_CallbackInfo * rxCbInfo);

int nfrContextDebugCheck(struct NFRResource * res);

int nfrGetContextLocation(void * op_context, struct NFRResource * res,
                           uint8_t * typeOut);

int nfrPrintCQError(int logLevel, const char * func, const char * file,
                     int line, int channel, struct NFRResource * res,
                     struct fi_cq_err_entry * err);

inline static struct NFRCommBufInfo nfrGetDefaultCommBufInfo(void)
{
  struct NFRCommBufInfo info = {0};
  info.txSlots               = 40;
  info.rxSlots               = 40;
  info.writeSlots            = 46;
  info.ackSlots              = 2;
  info.slotSize              = NETFR_MESSAGE_MAX_SIZE;
  assert(NFR_TOTAL_SLOTS(info) == NETFR_TOTAL_CONTEXT_COUNT);
  return info;
}

#ifdef __cplusplus
}
#endif

#endif
