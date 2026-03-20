// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Tim Dettmar <beanfacts@protonmail.com>

#ifndef NETFR_PROTOCOL_H
#define NETFR_PROTOCOL_H

#include <assert.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "nfr_constants.h"

enum NFRMessageType {
  NFR_MSG_CLIENT_HELLO = 1,
  NFR_MSG_SERVER_HELLO,
  NFR_MSG_HOST_BUFFER_STATE,
  NFR_MSG_BUFFER_UPDATE,
  NFR_MSG_CLIENT_BUFFER_STATE,
  NFR_MSG_CLIENT_DATA,
  NFR_MSG_CLIENT_DATA_ACK,
  NFR_MSG_HOST_DATA,
  NFR_MSG_HOST_DATA_ACK,
  NFR_MSG_CLIENT_SUBSCRIBE,
  NFR_MSG_CLIENT_UNSUBSCRIBE,
  NFR_MSG_MAX
};

enum NFRMessageStatus {
  NFR_MSG_STATUS_INVALID = 0,
  NFR_MSG_STATUS_OK,
  NFR_MSG_STATUS_ERROR,
  NFR_MSG_STATUS_REJECTED,
  NFR_MSG_STATUS_MAX
};

/**
 * @brief The header of every NetFR message.
 * 
 */
struct NFRHeader {
  /** Magic value ``NetFrame``, identifying the message as a NetFR message. */
  char    magic[8];
  /** The version of the protocol. */
  uint8_t version;
  /** The type of the message. */
  uint8_t type;
};

_Static_assert(sizeof(struct NFRHeader) == 10, "NFRHeader size mismatch");

inline static void nfrSetHeader(struct NFRHeader * hdr, uint8_t mType)
{
  assert(mType < NFR_MSG_MAX);
  memcpy(hdr->magic, NETFR_MAGIC, 8);
  hdr->version = NETFR_VERSION;
  hdr->type    = mType;
}

/*
  Note: NFRMsgClientHello and NFRMsgServerHello are sent as part of the
  Libfabric CM connection handshake (i.e., it is placed into the CM param
  buffer). They should not be sent over the fabric itself.
*/

// NFRMsgClientHello: no payload

struct NFRMsgClientHello {
  struct NFRHeader header;
};

_Static_assert(
  sizeof(struct NFRMsgClientHello) == 10, 
  "NFRMsgClientHello size mismatch"
);

// NFRMsgServerHello: no payload

struct NFRMsgServerHello {
  struct NFRHeader header;
  uint8_t          status;
};

_Static_assert(
  sizeof(struct NFRMsgServerHello) == 11, 
  "NFRMsgServerHello size mismatch"
);

// NFRMsgBufferUpdate, server -> client

struct NFRMsgBufferUpdate {
  struct NFRHeader    header;
  alignas(4) uint32_t bufferIndex;
  alignas(4) uint32_t payloadSize;
  alignas(4) uint32_t payloadOffset;
  alignas(4) uint32_t writeSerial;
  alignas(4) uint32_t channelSerial;
  alignas(8) uint64_t udata;
};

_Static_assert(
  sizeof(struct NFRMsgBufferUpdate) == 40, 
  "NFRMsgBufferUpdate size mismatch"
);

// NFRMsgClientBufferState, client -> server

struct NFRMsgClientBufferState {
  struct NFRHeader    header;
  alignas(4) uint32_t index;
  alignas(4) uint32_t pageSize;
  alignas(8) uint64_t addr;
  alignas(8) uint64_t size;
  alignas(8) uint64_t rkey;
};

_Static_assert(
  sizeof(struct NFRMsgClientBufferState) == 48, 
  "NFRMsgClientBufferState size mismatch"
);

// NFRMsgHostBufferState, server -> client

struct NFRMsgHostBufferState {
  struct NFRHeader    header;
  // The array ends with a 0 value
  alignas(8) uint64_t size[NETFR_MAX_MEM_REGIONS + 1];
};

_Static_assert(
    sizeof(struct NFRMsgHostBufferState) == \
      16 + (NETFR_MAX_MEM_REGIONS + 1) * sizeof(uint64_t), 
    "NFRMsgHostBufferState size mismatch");

// NFRMsgClientData, client -> server

struct NFRMsgClientData {
  struct NFRHeader    header;
  alignas(4) uint32_t length;
  alignas(4) uint32_t msgSerial;
  alignas(4) uint32_t channelSerial;
  alignas(8) uint64_t udata;
  alignas(32) uint8_t data[NETFR_MESSAGE_MAX_PAYLOAD_SIZE];
};

_Static_assert(offsetof(struct NFRMsgClientData, data) % 32 == 0,
               "NFRMsgClientData.data must be 32-byte aligned");
_Static_assert(offsetof(struct NFRMsgClientData, data) <= NETFR_MESSAGE_INTERNAL_MAX_SIZE,
               "NFRMsgClientData must not be larger than the maximum message size");

// NFRMsgClientDataAck, server -> client

struct NFRMsgClientDataAck {
  struct NFRHeader header;
};

_Static_assert(
  sizeof(struct NFRMsgClientDataAck) == 10, 
  "NFRMsgClientDataAck size mismatch"
);

// NFRMsgHostData, server -> client

struct NFRMsgHostData {
  struct NFRHeader    header;
  alignas(4) uint32_t length;
  alignas(4) uint32_t msgSerial;
  alignas(4) uint32_t channelSerial;
  alignas(8) uint64_t udata;
  alignas(32) uint8_t data[NETFR_MESSAGE_MAX_PAYLOAD_SIZE];
};

_Static_assert(offsetof(struct NFRMsgHostData, data) % 32 == 0,
               "NFRMsgHostData.data must be 32-byte aligned");
_Static_assert(offsetof(struct NFRMsgHostData, data) <= NETFR_MESSAGE_INTERNAL_MAX_SIZE,
               "NFRMsgHostData must not be larger than the maximum message size");

// NFRMsgHostDataAck, client -> server

struct NFRMsgHostDataAck {
  struct NFRHeader header;
};

_Static_assert(
  sizeof(struct NFRMsgHostDataAck) == 10, 
  "NFRMsgHostDataAck size mismatch"
);

// NFRMsgClientSubscribe, client -> server

struct NFRMsgClientSubscribe {
  struct NFRHeader    header;
  alignas(4) uint32_t queueID;
};

_Static_assert(sizeof(struct NFRMsgClientSubscribe) == 16, 
               "NFRMsgClientSubscribe size mismatch");

// NFRMsgClientUnsubscribe, client -> server

struct NFRMsgClientUnsubscribe {
  struct NFRHeader    header;
  alignas(4) uint32_t queueID;
};

_Static_assert(sizeof(struct NFRMsgClientUnsubscribe) == 16, 
               "NFRMsgClientUnsubscribe size mismatch");

#endif
