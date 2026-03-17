#ifndef LGMP_MODULE_H
#define LGMP_MODULE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lgmp/lgmp.h"
#include "lgmp/status.h"
#include "lgmp/client.h"
#include "lgmp/host.h"

typedef enum
{
  LGMP_MODULE_TYPE_INVALID, 
  LGMP_MODULE_TYPE_SHM
} 
LGMPModuleType;

struct LGMPClientInterface {
  LGMPModuleType type;
  void        (*free)(PLGMPClient * client);
  LGMP_STATUS (*sessionInit)(PLGMPClient client, uint32_t * udataSize,
      uint8_t ** udata, uint32_t * clientID);
  bool        (*sessionValid)(PLGMPClient client);

  LGMP_STATUS (*subscribe)(PLGMPClient client, uint32_t queueID,
      PLGMPClientQueue * result);
  LGMP_STATUS (*unsubscribe)(PLGMPClientQueue * result);
};

struct LGMPClientQueueOps {
  LGMP_STATUS (*advanceToLast)(PLGMPClientQueue queue);
  LGMP_STATUS (*process)(PLGMPClientQueue queue, PLGMPMessage result);
  LGMP_STATUS (*messageDone)(PLGMPClientQueue queue);
  LGMP_STATUS (*sendData)(PLGMPClientQueue queue, const void * data,
      uint32_t size, uint32_t * serial);
  LGMP_STATUS (*getSerial)(PLGMPClientQueue queue, uint32_t * serial);
  LGMP_STATUS (*memAttach)(PLGMPClientQueue queue, void * mem,
    uint64_t size, int dmaFd);
};

struct LGMPHostInterface {
  LGMPModuleType type;
  void        (*free)(PLGMPHost * host);
  LGMP_STATUS (*process)(PLGMPHost host);

  LGMP_STATUS (*queueNew)(PLGMPHost host,
    const struct LGMPQueueConfig config, PLGMPHostQueue * result);
};

struct LGMPHostQueueOps {
    bool        (*queueHasSubs)(PLGMPHostQueue queue);
    uint32_t    (*queueNewSubs)(PLGMPHostQueue queue);
    uint32_t    (*queuePending)(PLGMPHostQueue queue);
    LGMP_STATUS (*queuePost)(PLGMPHostQueue queue, uint32_t udata,
      PLGMPMemory payload);
    LGMP_STATUS (*readData)(PLGMPHostQueue queue, void * data, size_t * size);
    LGMP_STATUS (*ackData)(PLGMPHostQueue queue);
    LGMP_STATUS (*getClientIDs)(PLGMPHostQueue queue, uint32_t clientIDs[32],
      unsigned int * count);
    size_t      (*memAvail)(PLGMPHostQueue queue);
    LGMP_STATUS (*memAlloc)(PLGMPHostQueue queue, uint32_t size,
      PLGMPMemory * result);
    LGMP_STATUS (*memAllocAligned)(PLGMPHostQueue queue, uint32_t size,
      uint32_t alignment, PLGMPMemory * result);
    void        (*memFree)(PLGMPMemory * mem);
    void *      (*memPtr)(PLGMPMemory mem);
};

extern const struct LGMPClientInterface lgmpShmClientInterface;
extern const struct LGMPClientQueueOps  lgmpShmClientQueueOps;
extern const struct LGMPHostInterface   lgmpShmHostInterface;
extern const struct LGMPHostQueueOps    lgmpShmHostQueueOps;

LGMP_STATUS lgmpShmClientInit(void * mem, const size_t size,
  PLGMPClient * result);
LGMP_STATUS lgmpShmHostInit(void *mem, const uint32_t size, PLGMPHost * result,
  uint32_t udataSize, uint8_t * udata);

#endif