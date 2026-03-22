// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Tim Dettmar <beanfacts@protonmail.com>

#ifndef NETFR_PRIVATE_URI_H
#define NETFR_PRIVATE_URI_H

#include <string.h>
#include <stdlib.h>
#include <errno.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#endif

#include "nfr_resource.h"

/**
 * @brief Parse a URI of the form transport://addr:port into a sockaddr_in and
 *        transport type. If the address is a hostname rather than a numeric IP,
 *        it will be resolved via DNS.
 *
 * Query strings are currently not supported.
 *
 * @param uri            URI string to parse (e.g. "tcp://10.1.0.100:9000"
 *                       or "tcp://myhost:9000")
 * @param[out] addr      Populated with the parsed/resolved address and port
 * @param[out] transport Set to NFR_TRANSPORT_TCP or NFR_TRANSPORT_RDMA
 * @return 0 on success, negative errno on failure
 */
static inline int nfrParseUri(const char * uri, struct sockaddr_in * addr,
                              uint8_t * transport)
{
  if (!uri || !addr || !transport)
    return -EINVAL;

  const char * sep = strstr(uri, "://");
  if (!sep)
    return -EINVAL;

  size_t schemeLen = (size_t)(sep - uri);

  if (schemeLen == 3 && strncmp(uri, "tcp", 3) == 0)
    *transport = NFR_TRANSPORT_TCP;
  else if (schemeLen == 4 && strncmp(uri, "rdma", 4) == 0)
    *transport = NFR_TRANSPORT_RDMA;
  else
    return -EINVAL;

  const char * hostStart = sep + 3;
  if (*hostStart == '\0')
    return -EINVAL;

  const char * colon = strrchr(hostStart, ':');
  if (!colon || colon == hostStart)
    return -EINVAL;

  size_t hostLen = (size_t)(colon - hostStart);
  if (hostLen >= 256)
    return -EINVAL;

  char hostBuf[256];
  memcpy(hostBuf, hostStart, hostLen);
  hostBuf[hostLen] = '\0';

  /* Extract the port string (null-terminate at '?' for query params) */
  const char * portStr = colon + 1;
  if (*portStr == '\0')
    return -EINVAL;

  size_t portLen = 0;
  while (portStr[portLen] != '\0' && portStr[portLen] != '?')
    ++portLen;
  if (portLen == 0 || portLen >= 8)
    return -EINVAL;

  char portBuf[8];
  memcpy(portBuf, portStr, portLen);
  portBuf[portLen] = '\0';

  char * endPtr;
  long port = strtol(portBuf, &endPtr, 10);
  if (endPtr != portBuf + portLen)
    return -EINVAL;
  if (port <= 0 || port > 65535)
    return -EINVAL;

  struct addrinfo hints, * result;
  memset(&hints, 0, sizeof(hints));
  hints.ai_family   = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  int ret = getaddrinfo(hostBuf, NULL, &hints, &result);
  if (ret != 0)
    return -EINVAL;

  memcpy(addr, result->ai_addr, sizeof(*addr));
  freeaddrinfo(result);

  addr->sin_port = htons((uint16_t)port);

  return 0;
}

#endif
