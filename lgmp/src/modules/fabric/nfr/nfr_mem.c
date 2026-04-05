// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Tim Dettmar <beanfacts@protonmail.com>

#include "nfr_mem.h"
#include "nfr_log.h"
#include "nfr_resource.h"

/**
 * @brief Perform an aligned memory allocation.
 * 
 * @param size      Size of the memory region to allocate.
 * @param alignment Alignment of the memory region to allocate.
 * @return void*    Pointer to the allocated memory region.
 */
void * nfrMemAllocAlign(uint64_t size, uint64_t alignment)
{
#ifdef _WIN32
  return _aligned_malloc(size, alignment);
#else
  return aligned_alloc(alignment, size);
#endif
}

/**
 * @brief Free an aligned memory allocation. This is only required for
 *        compatibility with Windows, as free() does not support aligned
 *        memory regions there.
 * 
 * @param ptr Pointer to the memory region to free.
 */
void nfrMemFreeAlign(void * ptr)
{
#ifdef _WIN32
  _aligned_free(ptr);
#else
  free(ptr);
#endif
}

/**
 * @brief Find an empty memory slot in the resource.
 * 
 * @param res Resource to find an empty memory slot in.
 * @return struct NFRMemory* Pointer to the empty memory slot, or NULL if no
 *         empty memory slot is found.
 */
static struct NFRMemory * nfrFindEmptyMemSlot(struct NFRResource * res)
{
  for (int i = 0; i < NETFR_MAX_MEM_REGIONS; ++i)
    if (res->memRegions[i].state == MEM_STATE_EMPTY)
      return res->memRegions + i;
  return NULL;
}

/**
 * @brief Register memory region for RDMA operations with retries for
 *        ENOKEY errors.
 *
 * @param res        Resource to register the memory region with.
 * @param addr       Address of the memory region to register.
 * @param size       Size of the memory region to register.
 * @param acs        Access permissions for the memory region.
 * @param mr         Memory region object to store the result in.
 * @param context    Context to pass to the memory region registration.
 * @param maxRetries Maximum number of retries to attempt.
 *
 * @return ssize_t   Result of the memory region registration.
 */
static ssize_t nfrMrRegWithRetry(struct NFRResource * res, void * addr,
                                   uint64_t size, uint64_t acs,
                                   struct fid_mr ** mr, void * context,
                                   int maxRetries)
{
  ssize_t ret;
  if (res->mrMode & FI_MR_PROV_KEY)
    return fi_mr_reg(res->domain, addr, size, acs, 0, 0, 0, mr, context);

  ret = -FI_ENOKEY;
  for (int i = 0; i < maxRetries; ++i)
  {
    ret = fi_mr_reg(res->domain, addr, size, acs, 0, ++res->rkeyCounter, 0,
                    mr, context);
    if (ret == 0 || ret != -FI_ENOKEY)
      break;
  }
  return ret;
}

PNFRMemory nfrRdmaAttach(struct NFRResource * res, void * addr, uint64_t size,
                         uint64_t alignment, uint64_t acs, uint8_t memType,
                         uint8_t initialState)
{
  assert(res);
  assert(size > 0);

  /* RDMA memory registration requirements are much more strict than the shared
     memory implementation; we therefore take the user's alignment and size
     as suggestions and round them up to the nearest page size boundary.
  */

  // ensure alignment is page-aligned
  uint64_t ps = nfrGetPageSize();
  if (alignment < ps)
    alignment = ps;
  if (alignment % ps)
    alignment = (alignment / ps + 1) * ps;

  // round size up to the next alignment boundary
  size = (size + alignment - 1) & ~(alignment - 1);

  struct NFRMemory * mem = nfrFindEmptyMemSlot(res);
  if (!mem)
  {
    assert(!"Memory region limit reached");
    errno = memType ? ENOSPC : ENOMEM;
    return NULL;
  }
  NFR_LOG_DEBUG("Allocating memory region from resource %p", res);
  assert(!mem->addr);

  mem->parentResource = res;
  mem->size           = size;
  mem->memType        = memType;

  if (!addr)
  {
    mem->addr = nfrMemAllocAlign(mem->size, ps);
    if (!mem->addr)
    {
      NFR_LOG_DEBUG(
        "Failed to allocate %d bytes aligned to %d bytes: %s (%d)",
        mem->size, ps, strerror(errno), errno
      );
      errno = ENOMEM;
      goto free_mem_aligned;
    }
  } else
  {
    mem->addr = addr;
  }

  ssize_t ret = nfrMrRegWithRetry(res, mem->addr, mem->size, acs,
                                    &mem->mr, mem, 8);
  if (ret < 0 && ret != -FI_ENOKEY)
    goto free_mem_aligned;

  if (ret == 0)
  {
    NFR_LOG_DEBUG("Registered %lu byte memory %p with key %lu", mem->size,
                  mem->addr, fi_mr_key(mem->mr));
    mem->state = initialState;
    return mem;
  }

free_mem_aligned:
  if (!addr && mem->addr)
    nfrMemFreeAlign(mem->addr);
  mem->addr  = NULL;
  mem->state = MEM_STATE_EMPTY;
  return NULL;
}

#if defined(__linux__) && defined(ENABLE_FABRIC_DMABUF) && defined(_GNU_SOURCE)\
  && FI_VERSION(FI_MAJOR_VERSION, FI_MINOR_VERSION) >= FI_VERSION(1, 20)

PNFRMemory nfrRdmaAllocDMABUF(struct NFRResource * res, uint64_t size,
                               uint64_t acs)
{
  uint32_t ver = fi_version();
  if (ver < FI_VERSION(1, 20))
  {
    NFR_LOG_ERROR("Libfabric %d.%d does not support DMABUF registrations, "
                  "version 1.20 or later is required",
                  FI_MAJOR(ver), FI_MINOR(ver));
    return NULL;
  }

  static int memFdCounter = 0;
  PNFRMemory mem = nfrFindEmptyMemSlot(res);
  if (!mem)
  {
    assert(!"Memory region limit reached");
    errno = ENOSPC;
    return NULL;
  }
  NFR_LOG_DEBUG("Allocating memory region from resource %p", res);
  assert(!mem->addr);
  mem->state = MEM_STATE_INVALID;

  // udmabuf requires page-aligned size
  uint64_t ps = nfrGetPageSize();
  size = (size + ps - 1) & ~(ps - 1);

  void * addr = MAP_FAILED;
  int dmaFd   = -1;

  char memName[64];
  snprintf(memName, sizeof(memName), "netfr-dmabuf-%d-%d", getpid(),
           memFdCounter++);
  int fd = memfd_create(memName, MFD_CLOEXEC | MFD_ALLOW_SEALING);
  if (fd < 0)
  {
    NFR_LOG_DEBUG("Failed to create memfd: %s (%d)", strerror(errno), errno);
    goto free_slot;
  }

  if (ftruncate(fd, size) < 0)
  {
    NFR_LOG_DEBUG("Failed to truncate memfd: %s (%d)", strerror(errno), errno);
    goto close_memfd;
  }

  if (fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK) < 0)
  {
    NFR_LOG_DEBUG("Failed to seal memfd: %s (%d)", strerror(errno), errno);
    goto close_memfd;
  }

  int udmaFd = open("/dev/udmabuf", O_RDWR);
  if (udmaFd < 0)
  {
    NFR_LOG_DEBUG("Failed to open /dev/udmabuf: %s (%d)", strerror(errno),
                  errno);
    goto close_memfd;
  }

  struct udmabuf_create dmaBufAttr = {0};
  dmaBufAttr.memfd                 = fd;
  dmaBufAttr.flags                 = UDMABUF_FLAGS_CLOEXEC;
  dmaBufAttr.offset                = 0;
  dmaBufAttr.size                  = size;

  dmaFd = ioctl(udmaFd, UDMABUF_CREATE, &dmaBufAttr);
  close(udmaFd);
  if (dmaFd < 0)
  {
    NFR_LOG_DEBUG("Failed to create udmabuf: %s (%d)", strerror(errno), errno);
    goto close_memfd;
  }

  addr = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (addr == MAP_FAILED)
  {
    NFR_LOG_DEBUG("Failed to map memfd: %s (%d)", strerror(errno), errno);
    goto close_dmafd;
  }

  // memfd is no longer needed: udmabuf holds its own reference and the
  // mapping keeps the underlying pages alive.
  close(fd);
  fd = -1;

  if (mlock(addr, size) < 0)
  {
    NFR_LOG_DEBUG("Failed to lock memory: %s (%d)", strerror(errno), errno);
    goto unmap_memfd;
  }

  struct fi_mr_dmabuf dmaAttr = {0};
  dmaAttr.fd                  = dmaFd;
  dmaAttr.offset              = 0;
  dmaAttr.len                 = size;
  dmaAttr.base_addr           = addr;

  struct fi_mr_attr attr;
  memset(&attr, 0, sizeof(attr));
  attr.dmabuf        = &dmaAttr;
  attr.iov_count     = 1;
  attr.access        = acs;
  attr.offset        = 0;
  attr.context       = res;
  attr.auth_key      = 0;
  attr.auth_key_size = 0;
  attr.iface         = FI_HMEM_SYSTEM;
  attr.hmem_data     = 0;
  if (res->mrMode & FI_MR_PROV_KEY)
    attr.requested_key = 0;
  else
    attr.requested_key = ++res->rkeyCounter;

  ssize_t mrRet = -FI_ENOKEY;
  for (int i = 0; i < 32; ++i)
  {
    mrRet = fi_mr_regattr(res->domain, &attr, FI_MR_DMABUF, &mem->mr);
    if (mrRet == 0 || mrRet != -FI_ENOKEY)
      break;
    if (res->mrMode & FI_MR_PROV_KEY)
      break;
    attr.requested_key = ++res->rkeyCounter;
  }
  if (mrRet < 0)
  {
    NFR_LOG_DEBUG("Failed to register DMABUF: %s (%zd)", fi_strerror(-mrRet),
                  mrRet);
    goto munlock_memfd;
  }

  mem->addr    = addr;
  mem->dmaFd   = dmaFd;
  mem->size    = size;
  mem->state   = MEM_STATE_AVAILABLE_UNSYNCED;
  mem->memType = NFR_MEM_TYPE_SYSTEM_MANAGED_DMABUF;
  return mem;

munlock_memfd:
  munlock(addr, size);
unmap_memfd:
  munmap(addr, size);
close_dmafd:
  close(dmaFd);
close_memfd:
  if (fd >= 0)
    close(fd);
free_slot:
  mem->dmaFd = -1;
  mem->state = MEM_STATE_EMPTY;
  return NULL;
}
#else
PNFRMemory nfrRdmaAllocDMABUF(struct NFRResource * res, uint64_t size,
                               uint64_t acs)
{
  (void)res; (void)size; (void)acs;
  NFR_LOG_ERROR("DMABUFs not enabled/supported");
  return NULL;
}
#endif

/**
 * @brief Acknowledge a buffer, making it available for reuse.
 * 
 * @param mem Pointer to the memory region to acknowledge.
 */
void nfrAckBuffer(PNFRMemory mem)
{
  if (!mem)
  {
    assert(!"Null mem passed to nfrAckBuffer");
    return;
  }

  if (mem->state <= MEM_STATE_EMPTY)
  {
    assert(!"Unexpected memory state");
    return;
  }

  mem->state = MEM_STATE_AVAILABLE_UNSYNCED;
}

/**
 * @brief Free a memory region.
 * 
 * @param mem Pointer to the memory region to free.
 */
void nfrFreeMemory(PNFRMemory * mem)
{
  if (!mem || !*mem)
  {
    assert(!"Null or freed mem passed to nfrFreeMemory");
    return;
  }

  if ((*mem)->mr)
    fi_close(&(*mem)->mr->fid);

#if defined(__linux__) && defined(ENABLE_FABRIC_DMABUF) && defined(_GNU_SOURCE)
  // DMABUF-backed memory has its own teardown path
  if ((*mem)->memType == NFR_MEM_TYPE_SYSTEM_MANAGED_DMABUF)
  {
    if ((*mem)->addr)
    {
      NFR_LOG_DEBUG("Unmapping DMABUF region %p", (*mem)->addr);
      munlock((*mem)->addr, (*mem)->size);
      munmap((*mem)->addr, (*mem)->size);
    }
    if ((*mem)->dmaFd > 0)
      close((*mem)->dmaFd);
  }
  else
#endif
  // The user is responsible for freeing external memory regions
  if (nfrMemIsInternal((*mem)->memType) && (*mem)->addr)
  {
    NFR_LOG_DEBUG("Freeing internal memory region %p", (*mem)->addr);
    nfrMemFreeAlign((*mem)->addr);
  }

  if (!BETWEEN_INCL(*mem, 
                    (*mem)->parentResource->memRegions,
                    (*mem)->parentResource->memRegions + NETFR_MAX_MEM_REGIONS))
  { 
    NFR_LOG_DEBUG("Freeing external memory region reference %p", *mem);
    free(*mem);
  }
  *mem = 0;
}
