// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Tim Dettmar <beanfacts@protonmail.com>

#ifndef LGMP_FABRIC_HOST_CALLBACK_H
#define LGMP_FABRIC_HOST_CALLBACK_H

#include "nfr_resource.h"

/* Callbacks for the send, receive, and RDMA write operations respectively. */

void nfrHostProcessInternalTx(struct NFRFabricContext * ctx);
void nfrHostProcessInternalRx(struct NFRFabricContext * ctx);
void nfrHostProcessInternalWrite(struct NFRFabricContext * ctx);

#endif
