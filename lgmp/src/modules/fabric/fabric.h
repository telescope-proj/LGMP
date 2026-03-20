// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Tim Dettmar <beanfacts@protonmail.com>

#ifndef LGMP_FABRIC_H
#define LGMP_FABRIC_H

#include <stdalign.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "lgmp/lgmp.h"
#include "nfr_resource.h"

// Fabric Host -----------------------------------------------------------------

struct LGMPFabricHost;

struct LGMPFabricHostChannel {
  _Atomic(uint32_t)          lock;
  uint32_t                   msgSerial;
  uint32_t                   writeSerial;
  uint32_t                   channelSerial;
  bool                       subscribed;
  uint32_t                   newSubs;
  struct LGMPFabricHost *    parent;
  struct NFRResource *       res;
  struct NFRMemory *         mem;
  struct NFRRemoteMemory     clientRegions[NETFR_MAX_MEM_REGIONS];
};

struct LGMPFabricHost {
  struct LGMPFabricHostChannel channels[LGMP_MAX_QUEUES + 1];
  uint8_t  numChannels;
  uint64_t maxRegionAlloc;
  uint64_t maxTotalAlloc;
};

// Fabric Client ---------------------------------------------------------------

struct LGMPFabricClient;

struct LGMPFabricClientChannel {
  _Atomic(uint32_t)           lock;
  struct LGMPFabricClient *   parent;
  struct NFRResource *        res;
  uint32_t                    msgSerial;
  uint32_t                    writeSerial;
  uint32_t                    channelSerial;
  uint32_t                    memSerial;
};

struct LGMPFabricClient {
  struct LGMPFabricClientChannel channels[LGMP_MAX_QUEUES + 1];
  uint8_t                        numChannels;
  struct NFRInitOpts              peerInfo;
  uint64_t maxRegionAlloc;
  uint64_t maxTotalAlloc;
};

// Fabric Memory ---------------------------------------------------------------

typedef struct NFRMemory LGMPFabricMemory;

// Channel Connection Check Inlines --------------------------------------------

static inline bool nfrHostChannelConnected(
    struct LGMPFabricHost * fh, int i, struct LGMPFabricHostChannel ** out)
{
  if (i >= fh->numChannels)
    return false;
  struct LGMPFabricHostChannel * ch = &fh->channels[i];
  if (ch->res && ch->res->connState == NFR_CONN_STATE_CONNECTED)
  {
    *out = ch;
    return true;
  }
  return false;
}

static inline bool nfrClientChannelConnected(
    struct LGMPFabricClient * fc, int i, struct LGMPFabricClientChannel ** out)
{
  if (i >= fc->numChannels)
    return false;
  struct LGMPFabricClientChannel * ch = &fc->channels[i];
  if (ch->res && ch->res->connState == NFR_CONN_STATE_CONNECTED)
  {
    *out = ch;
    return true;
  }
  return false;
}

#endif
