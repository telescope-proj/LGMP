/***
 * LGMP - Looking Glass Memory Protocol
 * Copyright © 2020-2025 Geoffrey McRae <geoff@hostfission.com>
 * https://github.com/gnif/LGMP
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation; either version 2 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc., 59
 * Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#ifndef LGMP_CLIENT_H
#define LGMP_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lgmp.h"
#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief Initialize the shared memory backend. See #lgmpShmClientInit
 *
 * @warning This function implicitly calls the lgmpShmClientInit function,
 *          but lgmpShmClientInit/lgmpFabricClientInit should be used in
 *          future versions to make the requirements explicit.
 */
LGMP_STATUS lgmpClientInit(void * mem, const size_t size, PLGMPClient * result);

/**
 * @brief Initialize the LGMP client using the shared memory backend.
 *
 * @param mem Pointer to the shared memory region to use for the client.
 * @param size Size of the shared memory region in bytes.
 * @param result Pointer to the client to be initialized.
 * @return LGMP_STATUS
 */
LGMP_STATUS lgmpShmClientInit(void * mem, const size_t size, PLGMPClient * result);

typedef struct LGMPFabricClientInitOpts
{
    const char * localUri;
    const char * remoteUri;
    bool         enableDMABUF;
} LGMPFabricClientInitOpts;

/**
 * @brief Initialize the LGMP client using the fabric backend.
 *
 * The URI follows the format ``transport://addr:port?opt=value&opt2=value2``
 *
 * The transport may either be **tcp** or **rdma**. In RDMA mode, the network
 * address MUST always be that of an RDMA-enabled network adapter in order for
 * the system to bind to the hardware resources of that NIC. Addresses such as
 * ``0.0.0.0`` or `127.0.0.1`` are invalid!
 *
 * NetFR allocates up to ``LGMP_MAX_QUEUES + 1`` ports, starting from the port
 * number specified in the URI. For example, with 5 queues, using the URI
 * rdma://10.1.2.3:9000 will allocate port 9000 for the metadata queue, and
 * 9001-9005 for each LGMP queue. Please ensure that this block of ports is
 * free before initializing this backend.
 *
 * @param localUri URI of the local network interface.
 * @param peerUri  URI of the remote network interface.
 * 
 * @warning The peers must use the same transport.
 *
 * @return LGMP_STATUS
 */
LGMP_STATUS lgmpFabricClientInit(LGMPFabricClientInitOpts * opts, 
                                 PLGMPClient * result);

void        lgmpClientFree(PLGMPClient * client);
LGMP_STATUS lgmpClientSessionInit(PLGMPClient client, uint32_t * udataSize,
    uint8_t ** udata, uint32_t * clientID);
bool        lgmpClientSessionValid(PLGMPClient client);

LGMP_STATUS lgmpClientSubscribe(PLGMPClient client, uint32_t queueID,
    PLGMPClientQueue * result);
LGMP_STATUS lgmpClientUnsubscribe(PLGMPClientQueue * result);

typedef struct
{
  uint32_t   udata;
  uint32_t   size;
  void     * mem;
  uint32_t   memSize;
  int        dmaFD;
}
LGMPMessage, * PLGMPMessage;

LGMP_STATUS lgmpClientAdvanceToLast(PLGMPClientQueue queue);
LGMP_STATUS lgmpClientProcess(PLGMPClientQueue queue, PLGMPMessage result);
LGMP_STATUS lgmpClientMessageDone(PLGMPClientQueue queue);

/**
 * @brief Send data to the host of up to ``LGMP_MSGS_SIZE`` in size.
 *
 * @param queue  The client queue to send data on.
 * @param data   Pointer to the data to send.
 * @param size   Size of the data in bytes.
 * @param[out] serial  Set to the serial number of the message if successful.
 * @return LGMP_STATUS
 */
LGMP_STATUS lgmpClientSendData(PLGMPClientQueue queue, const void * data,
    uint32_t size, uint32_t * serial);

/**
 * @brief Get the serial number of the last message processed by the host.
 *
 * This can be used to determine if data messages have been processed by the
 * host.
 *
 * @param queue  The client queue to query.
 * @param[out] serial  Set to the last serial number processed by the host.
 * @return LGMP_STATUS
 */
LGMP_STATUS lgmpClientGetSerial(PLGMPClientQueue queue, uint32_t * serial);

/**
 * @brief Attach a user-managed memory region to the queue.
 *
 * This returns ``LGMP_ERR_NOT_SUPPORTED`` for the shared memory backend.
 *
 * For the fabric backend, this allows you to register memory regions for RDMA
 * access. For instance, you may allocate a DMABUF externally and register it
 * with the RDMA device using this call for direct GPU access, if supported
 * by the underlying driver and hardware.
 * 
 * Depending on the type of memory passed to it, this function may not always
 * return successfully. 
 *
 * **The following conditions must generally be met to enable the memory to be 
 * registered with the RDMA device:**
 *
 * - The memory should be page-aligned and rounded to the nearest page size
 * - The RDMA driver will typically perform memory pinning implicitly, but you
 *   should make sure that the memory can be pinned; on Linux, check the limits 
 *   set in ``/etc/security/limits.conf``, as they are usually set too low for 
 *   proper RDMA operation.
 * - If the memory is backed by hugepages, either explicitly 
 *   (e.g., with madvise) or implicitly (e.g., via THP), you MUST set the 
 *   environment variable ``RDMAV_HUGEPAGES_SAFE=1``. Failure to do so will 
 *   result in unpredictable behavior dependent on the hardware and driver.
 *   In addition, the memory must be page-aligned and rounded up to the hugepage 
 *   size, NOT the standard page size.
 * - If IOMMU is enabled and NOT in passthrough mode, the behaviour of this
 *   function is highly dependent on the hardware and driver. Certain hardware,
 *   typically cards using the ``mlx5`` driver (ConnectX-4+) will be able to 
 *   register host memory correctly, while others might fail to do so. In the 
 *   latter case, you may need to set ``iommu=pt`` on the kernel command line. 
 *   This option is, in most cases, recommended for performance reasons. 
 *   However, it can have security implications with regard to DMA attacks.
 * - Registration of GPU memory regions without ``iommu=pt`` is generally not
 *   possible. Additionally, the memory region must support DMABUF, which 
 *   particularly impacts users of NVIDIA GPUs, as only the open kernel driver
 *   has full DMABUF support. In addition, it is currently untested as to whether 
 *   the fabric backend currently supports DMABUFs at all.
 *
 * When using the fabric backend with TCP (which you generally should not do),
 * the requirements are much more relaxed. Any memory accessible to the CPU will
 * generally work fine.
 *
 * @param queue Queue to attach the memory region to. See the warning in 
 *              #lgmpHostMemAlloc for fabric-specific queue requirements.
 * @param mem   Pointer to the memory region to attach.
 * @param size  Size of the memory region to attach.
 * @param dmaFd File descriptor for DMABUF memory. Set to -1 for regular memory
 *              allocations.
 * @return LGMP_STATUS 
 */
LGMP_STATUS lgmpClientMemAttach(PLGMPClientQueue queue, void * mem,
    uint64_t size, int dmaFd);

#ifdef __cplusplus
}
#endif

#endif
