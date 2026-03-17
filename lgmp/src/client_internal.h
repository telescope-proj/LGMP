#ifndef LGMP_CLIENT_INTERNAL_H
#define LGMP_CLIENT_INTERNAL_H

#include "lgmp/lgmp.h"
#include "modules/module.h"

struct LGMPClientQueue
{
  const struct LGMPClientQueueOps * ops;
  void        * internal;

  PLGMPClient   client;
  unsigned int  id;
  unsigned int  index;
  uint32_t      position;
};

struct LGMPClient
{
  const struct LGMPClientInterface * iface;
  void * internal;

  uint32_t id;
  uint32_t sessionID;

  struct LGMPClientQueue queues[LGMP_MAX_QUEUES];
};

#endif
