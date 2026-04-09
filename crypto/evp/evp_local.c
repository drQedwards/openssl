/*
 * Copyright 2026 The OpenSSL Project Authors. All Rights Reserved.
 *
 * Licensed under the Apache License 2.0 (the "License").  You may not use
 * this file except in compliance with the License.  You can obtain a copy
 * in the file LICENSE in the source distribution or at
 * https://www.openssl.org/source/license.html
 */

#include "internal/deprecated.h"

#include <openssl/evp.h>
#include "crypto/evp.h"
#include "evp_local.h"
#include "internal/threads_common.h"

int ossl_evp_local_is_lockless_atomic_enabled(void)
{
#ifdef OSSL_ATOMICS_LOCKLESS
    return 1;
#else
    return 0;
#endif
}
