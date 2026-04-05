#ifndef LGMP_HOST_INTERNAL_H
#define LGMP_HOST_INTERNAL_H

#include "lgmp/lgmp.h"
#include "modules/module.h"

struct LGMPHostQueue
{
  const struct LGMPHostQueueOps * ops;
  void        * internal;

  PLGMPHost    host;
  unsigned int index;
};

struct LGMPHost
{
  const struct LGMPHostInterface * iface;
  void * internal;

  uint32_t  sessionID;
  uint32_t  numQueues;
  uint8_t * udata;
  uint32_t  udataSize;

  struct LGMPHostQueue queues[LGMP_MAX_QUEUES];
};

struct LGMPMemory
{
  PLGMPHostQueue queue;
  unsigned int   offset;
  uint32_t       size;
  void          *mem;
  void          *internal;
  int            dmaFd;
};

#endif
