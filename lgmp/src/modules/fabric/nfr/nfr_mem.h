// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Tim Dettmar <beanfacts@protonmail.com>

#ifndef NETFR_PRIVATE_MEM_H
#define NETFR_PRIVATE_MEM_H

#ifdef _WIN32
#include <sysinfoapi.h>
#else
#include <unistd.h>
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef __linux__
#include <fcntl.h>
#include <linux/udmabuf.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/types.h>
#else
#ifdef NETFR_ENABLE_DMABUF_REGISTRATION
#error "DMABUF registration is not supported on this platform"
#endif
#endif

#include "nfr_resource.h"

void * nfrMemAllocAlign(uint64_t size, uint64_t alignment);
void nfrMemFreeAlign(void * ptr);

/**
 * @brief Attach or register a memory region for RDMA operations.
 * 
 * @param res          Resource to attach/register the memory region with.
 * @param addr         Address of the memory region to attach/register. 
 *                     If NULL, a new memory block will be allocated with
 *                     the specified size.
 * @param size         Size of the memory region to attach/register.
 * @param alignment    Alignment of the memory region to register (only if addr == NULL).
 * @param acs          Access permissions for the memory region.
 * @param externalMem  Whether the memory region is external to the resource.
 * @param initialState Initial state of the memory region. 
 *
 * @return PNFRMemory  Pointer to the memory region object, or NULL on failure.
 */
PNFRMemory nfrRdmaAttach(struct NFRResource * res, void * addr, uint64_t size,
                          uint64_t alignment, uint64_t acs,
                          uint8_t externalMem, uint8_t initialState);

/**
 * @brief Attach a DMABUF memory region for RDMA operations.
 * 
 * @warning This function is highly experimental and only available on
 *          Linux systems with libfabric version 1.20 or later.
 * 
 * @param res     Resource to attach the memory region to.
 * @param mem     Pointer to memory region.
 * @param size    Size of memory region to attach.
 * @param acs     Access permissions for memory region.
 * @param dmaFd   File descriptor of the associated DMABUF.
 * @param memType Type of the memory region (user-managed or system-managed).
 * @param out     Pointer to the memory region object to populate with the 
 *                attached memory region's information.
 * @return PNFRMemory Pointer to the memory region object, or NULL on failure.
 */
int nfrRdmaAttachDMABUF(struct NFRResource * res, void * addr, uint64_t size,
                        uint64_t acs, int dmaFd, int memType, PNFRMemory out);

/**
 * @brief Attach a DMABUF memory region to a resource.
 * 
 * @warning This function is highly experimental and only available on
 *          Linux systems with libfabric version 1.20 or later.
 * 
 * @param res   Resource to attach the DMABUF memory region to.
 * @param buf   Pointer to the DMABUF.
 * @param size  Size of the DMABUF.
 * @param fd    File descriptor of the DMABUF.
 * 
 * @return PNFRMemory Pointer to the memory region object, or NULL on failure.
 */
PNFRMemory nfrRdmaAllocDMABUF(struct NFRResource * res, uint64_t size,
                               uint64_t acs);

/**
 * @brief Allocate and register a memory region for RDMA operations.
 * 
 * @param res          Resource to allocate the memory region from.
 * @param size         Size of the memory region to allocate.
 * @param acs          Access permissions for the memory region.
 * @param initialState Initial state of the memory region.
 * 
 * @return PNFRMemory Pointer to the memory region object, or NULL on failure.
 */
inline static PNFRMemory nfrRdmaAlloc(struct NFRResource * res, uint64_t size,
                                       uint64_t acs, uint8_t initialState)
{
  return nfrRdmaAttach(res, 0, size, 0, acs, 0, initialState);
}

/**
 * @brief Get the system page size.
 * 
 * @return uint64_t Page size in bytes.
 */
inline static uint64_t nfrGetPageSize(void)
{
#ifdef _WIN32
  SYSTEM_INFO si;
  GetSystemInfo(&si);
  return si.dwPageSize;
#else
  return sysconf(_SC_PAGESIZE);
#endif
}

#endif
