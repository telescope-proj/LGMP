// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Tim Dettmar <beanfacts@protonmail.com>

#ifndef NETFR_PRIVATE_CONSTANTS_H
#define NETFR_PRIVATE_CONSTANTS_H

#ifdef __cplusplus
extern "C" {
#endif

#define NETFR_VERSION 1
#define NETFR_MAGIC   "NetFrame"

/* NetFR can store a limited amount of additional user data when performing its
   callbacks to user functions. This can eliminate the need for users to
   allocate their own context structures, instead using the uData array to store
   the necessary information. This constant defines how much data should be
   storable. */
#define NETFR_CALLBACK_USER_DATA_COUNT 8

/* The total number of NetFR-managed memory regions that can be allocated. These
   are used specifically for RDMA write operations and are managed internally by
   the NetFR library. You can also allocate your own self-managed memory regions
   which do not count towards this limit. However, such regions cannot be used
   with the standard NetFR protocol functions. */
#define NETFR_MAX_MEM_REGIONS 40

/* The total number of context slots for the NetFR library. A context slot is
   used to store the state of a single operation, a pointer to an exclusively
   owned buffer, the callback to invoke upon its completion, as well as the user
   data to pass to the callback. */
#define NETFR_TOTAL_CONTEXT_COUNT 128

/* The maximum amount of data which can be exchanged on connection setup via
   the Libfabric connection manager channel. */
#define NETFR_CM_MESSAGE_MAX_SIZE 16

/* The maximum size of a message including the header (not for RDMA buffers) */
#define NETFR_MESSAGE_MAX_SIZE 4096

/* The maximum size of the internal fields of a message (i.e. everything except
   the user-defined payload). */
#define NETFR_MESSAGE_INTERNAL_MAX_SIZE 32

/* The maximum size of user messages, with the header and padding subtracted. */
#define NETFR_MESSAGE_MAX_PAYLOAD_SIZE (NETFR_MESSAGE_MAX_SIZE - NETFR_MESSAGE_INTERNAL_MAX_SIZE)

/* The maximum size of a/an (R)DMA buffer is determined by the provider and
   hardware capabilities for the maximum buffer size that can be handled in a
   single work request. For RDMA, this is typically 1 GiB; we set a limit of 256
   MiB as this covers most use cases. */
#define NETFR_MAX_BUFFER_SIZE (1 << 28)

/* The maximum total allocation size for each NetFR resource. */
#define NETFR_MAX_MEM_USAGE (1 << 30)

/* The number of transmit credits, i.e., the number of messages that can be sent
   before waiting for an acknowledgment. */
#define NETFR_CREDIT_COUNT 60

/* The number of reserved credits for internal operations. If the number of
   credits falls below this level, functions such as nfrClientSendData and
   nfrHostSendData will fail until all other ops are done, but the system will
   still be able to process completion events and potentially increase the
   credit count. */
#define NETFR_RESERVED_CREDIT_COUNT 8

enum {
  NFR_LOG_LEVEL_TRACE,
  NFR_LOG_LEVEL_DEBUG,
  NFR_LOG_LEVEL_INFO,
  NFR_LOG_LEVEL_WARNING,
  NFR_LOG_LEVEL_ERROR,
  NFR_LOG_LEVEL_FATAL,
  NFR_LOG_LEVEL_OFF
};

/* ---- Private Constants ---- */

/* Index of the metadata channel reserved for library-internal use. */
#define NFR_METADATA_CHANNEL_INDEX 0

/* Index of the first NetFR channel used for LGMP queues. */
#define NFR_QUEUE_CHANNEL_BASE     1

/* Number of udata slots reserved for internal library use. */
#define NFR_INTERNAL_CB_UDATA_COUNT 8

/* Number of udata slots, including those reserved for internal library use. */
#define NFR_TOTAL_CB_UDATA_COUNT \
  (NETFR_CALLBACK_USER_DATA_COUNT + NFR_INTERNAL_CB_UDATA_COUNT)

/* Index of the first udata slot available for user use. */
#define NFR_USER_CB_INDEX                                                      \
  (NFR_TOTAL_CB_UDATA_COUNT - NETFR_CALLBACK_USER_DATA_COUNT)

#define NFR_INTERNAL_CB_INDEX 0

enum ContextState {
  CTX_STATE_INVALID,

  /* Available for use */
  CTX_STATE_AVAILABLE,

  /* This context is specifically reserved for sending DataAck messages, which
     have no unique data and can thus be used by multiple send requests
     simultaneously */
  CTX_STATE_ACK_ONLY,

  /* Allocated but not yet transitioned to the wait state. Used only to detect
     bugs in the code where a context is reserved and a transmission error
     occurs without proper cleanup afterwards. */
  CTX_STATE_ALLOCATED,

  /* Data transfer request submitted using this context, waiting for it to
     complete. */
  CTX_STATE_WAITING,

  /* Data receive completed, but the data slot of this context still must be
  read using ``nfrHostReadData`` beofre it can be reused. */
  CTX_STATE_HAS_DATA,

  /* The operation associated with the context has been canceled. */
  CTX_STATE_CANCELED,

  CTX_STATE_MAX
};

enum MemoryState {
  MEM_STATE_INVALID,
  /* Does not currently contain a memory region */
  MEM_STATE_EMPTY,
  /* Internal use only */
  MEM_STATE_RESERVED,
  /* Remote end has not yet been informed of the change */
  MEM_STATE_AVAILABLE_UNSYNCED,
  /* Ready to use for RDMA ops */
  MEM_STATE_AVAILABLE,
  /* Memory region is currently being used for an RDMA operation */
  MEM_STATE_BUSY,
  /* Memory region has data that needs to be read */
  MEM_STATE_HAS_DATA,

  MEM_STATE_MAX
};

enum NFROpType {
  NFR_OP_NONE,
  NFR_OP_SEND      = (1 << 0), // Regular message send
  NFR_OP_SEND_COPY = (1 << 1), // Copy data from user-defined buffer to context
  NFR_OP_INJECT = (1 << 2), // Send without consuming a context (limited size)
  NFR_OP_RECV   = (1 << 3), // Regular message receive
  NFR_OP_WRITE  = (1 << 4), // RDMA write
  NFR_OP_ACK    = (1 << 5), // Message acknowledgement
  NFR_OP_MAX
};

enum NFRRemoteMemoryState {
  NFR_RMEM_NONE,        // This index is not in use
  NFR_RMEM_AVAILABLE,   // This index is ready to be used for writes
  NFR_RMEM_ALLOCATED,   // Allocated but not yet used in an operation (debug)
  NFR_RMEM_BUSY_LOCAL,  // Local NIC performing RDMA op on this memory
  NFR_RMEM_BUSY_REMOTE, // Local RDMA op done, remote side did not ack yet
  NFR_RMEM_MAX
};

enum NFRConnState {
  NFR_CONN_STATE_NONE,
  NFR_CONN_STATE_DISCONNECTED,
  NFR_CONN_STATE_READY_TO_CONNECT,
  NFR_CONN_STATE_CONNECTING,
  NFR_CONN_STATE_CONNECTED_NEED_RESOURCES,
  NFR_CONN_STATE_CONNECTED,
  NFR_CONN_STATE_MAX
};

#ifdef __cplusplus
}
#endif

#endif
