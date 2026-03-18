/**
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

#include "lgmp/status.h"

const char * lgmpStatusString(LGMP_STATUS status)
{
  switch(status)
  {
    case LGMP_OK                    : return "LGMP_OK";
    case LGMP_ERR_CLOCK_FAILURE     : return "LGMP_CLOCK_FAILURE";
    case LGMP_ERR_INVALID_ARGUMENT  : return "LGMP_ERR_INVALID_ARGUMENT";
    case LGMP_ERR_INVALID_SIZE      : return "LGMP_ERR_INVALID_SIZE";
    case LGMP_ERR_INVALID_ALIGNMENT : return "LGMP_ERR_INVALID_ALIGNMENT";
    case LGMP_ERR_INVALID_SESSION   : return "LGMP_ERR_INVALID_SESSION";
    case LGMP_ERR_NO_MEM            : return "LGMP_ERR_NO_MEM";
    case LGMP_ERR_NO_SHARED_MEM     : return "LGMP_ERR_NO_SHARED_MEM";
    case LGMP_ERR_HOST_STARTED      : return "LGMP_ERR_HOST_STARTED";
    case LGMP_ERR_NO_QUEUES         : return "LGMP_ERR_NO_QUEUES";
    case LGMP_ERR_QUEUE_FULL        : return "LGMP_ERR_QUEUE_FULL";
    case LGMP_ERR_QUEUE_EMPTY       : return "LGMP_ERR_QUEUE_EMPTY";
    case LGMP_ERR_QUEUE_UNSUBSCRIBED: return "LGMP_ERR_QUEUE_UNSUBSCRIBED";
    case LGMP_ERR_QUEUE_TIMEOUT     : return "LGMP_ERR_QUEUE_TIMEOUT";
    case LGMP_ERR_INVALID_MAGIC     : return "LGMP_ERR_INVALID_MAGIC";
    case LGMP_ERR_INVALID_VERSION   : return "LGMP_ERR_INVALID_VERSION";
    case LGMP_ERR_NO_SUCH_QUEUE     : return "LGMP_ERR_NO_SUCH_QUEUE";
    case LGMP_ERR_CORRUPTED                : return "LGMP_ERR_CORRUPTED";
    case LGMP_ERR_TRANSPORT_INIT_FAILURE   : return "LGMP_ERR_TRANSPORT_INIT_FAILURE";
    case LGMP_ERR_TRANSPORT_CONNECT_FAILURE: return "LGMP_ERR_TRANSPORT_CONNECT_FAILURE";
    case LGMP_ERR_TRANSPORT_DISCONNECTED   : return "LGMP_ERR_TRANSPORT_DISCONNECTED";
    case LGMP_ERR_TRANSPORT_REJECTED       : return "LGMP_ERR_TRANSPORT_REJECTED";
    case LGMP_ERR_TRANSPORT_IO             : return "LGMP_ERR_TRANSPORT_IO";
    case LGMP_ERR_TRANSPORT_NO_CREDITS     : return "LGMP_ERR_TRANSPORT_NO_CREDITS";
    case LGMP_ERR_TRANSPORT_MEM_REG        : return "LGMP_ERR_TRANSPORT_MEM_REG";
    case LGMP_ERR_NOT_SUPPORTED            : return "LGMP_ERR_NOT_SUPPORTED";
  }
  return "Invalid status!";
}

const char * lgmpStatusInfoString(LGMP_STATUS status)
{
  switch(status)
  {
    case LGMP_OK: 
      return "LGMP_OK";
    case LGMP_ERR_CLOCK_FAILURE:
      return "Failed to get time";
    case LGMP_ERR_INVALID_ARGUMENT:
      return "Invalid argument";
    case LGMP_ERR_INVALID_SIZE:
      return "Invalid size";
    case LGMP_ERR_INVALID_ALIGNMENT:
      return "Invalid alignment";
    case LGMP_ERR_INVALID_SESSION:
      return "Invalid session";
    case LGMP_ERR_NO_MEM:
      return "No memory available";
    case LGMP_ERR_NO_SHARED_MEM:
      return "No shared memory available";
    case LGMP_ERR_HOST_STARTED:
      return "Host already started";
    case LGMP_ERR_NO_QUEUES:
      return "No queues available";
    case LGMP_ERR_QUEUE_FULL:
      return "Queue full";
    case LGMP_ERR_QUEUE_EMPTY:
      return "Queue empty";
    case LGMP_ERR_QUEUE_UNSUBSCRIBED:
      return "Queue unsubscribed";
    case LGMP_ERR_QUEUE_TIMEOUT:
      return "Queue timeout";
    case LGMP_ERR_INVALID_MAGIC:
      return "Invalid magic in queue header (is the host running?)";
    case LGMP_ERR_INVALID_VERSION:
      return "Invalid version in queue header (version mismatch?)";
    case LGMP_ERR_NO_SUCH_QUEUE:
      return "No such queue exists (LG version mismatch?)";
    case LGMP_ERR_CORRUPTED:
      return "Queue corrupted (is the host running?)";
    case LGMP_ERR_TRANSPORT_INIT_FAILURE:
      return "Failed to initialize network transport";
    case LGMP_ERR_TRANSPORT_CONNECT_FAILURE:
      return "Failed to connect to host";
    case LGMP_ERR_TRANSPORT_DISCONNECTED:
      return "Network transport disconnected";
    case LGMP_ERR_TRANSPORT_REJECTED:
      return "Connection rejected by host "
             "(is the host running or busy serving another client?)";
    case LGMP_ERR_TRANSPORT_IO:
      return "Network transport I/O error";
    case LGMP_ERR_TRANSPORT_NO_CREDITS:
      return "No credits available for network transport "
             "(network or peer may be overloaded, try reducing send rate)";
    case LGMP_ERR_TRANSPORT_MEM_REG:
      return "Failed to register memory for network transport";
    case LGMP_ERR_NOT_SUPPORTED:
      return "Operation not supported";
  }
  return "Invalid status!";
}