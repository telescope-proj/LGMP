// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (c) 2026 Tim Dettmar <beanfacts@protonmail.com>

#ifndef LGMP_FABRIC_CLIENT_CALLBACK_H
#define LGMP_FABRIC_CLIENT_CALLBACK_H

#include "nfr_resource.h"

void nfrClientProcessInternalTx(struct NFRFabricContext * ctx);
void nfrClientProcessInternalRx(struct NFRFabricContext * ctx);

#endif
