#ifndef LGMP_SHM_INTERNAL_H
#define LGMP_SHM_INTERNAL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lgmp/lgmp.h"
#include "headers.h"

struct LGMPShmClientQueue
{
  struct LGMPHeader      * header;
  struct LGMPHeaderQueue * hq;
};

struct LGMPShmClient
{
  uint8_t           * mem;
  struct LGMPHeader * header;
  uint64_t            hosttime;
  uint64_t            lastHeartbeat;

  struct LGMPShmClientQueue queues[LGMP_MAX_QUEUES];
};

struct LGMPShmHostQueue
{
  uint32_t                 position;
  uint32_t                 cMsgPos;
  struct LGMPHeaderQueue * hq;
};

struct LGMPShmHost
{
  uint8_t           * mem;
  size_t              size;
  uint32_t            avail;
  uint32_t            nextFree;
  bool                started;
  uint64_t            lastTimestamp;
  struct LGMPHeader * header;

  struct LGMPShmHostQueue queues[LGMP_MAX_QUEUES];
};

#endif
