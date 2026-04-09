# SSL Chain + EVP Context (local workspace)

Date: 2026-04-09 (UTC)

## 1) Build status for current commit

Current commit:

```bash
git rev-parse HEAD
# 631a2cdbb9ef8b47499c001a3616802f5ebc63cc
```

Build commands run:

```bash
./config --debug no-shared
make -j1
```

Result: successful local build (binary present at `apps/openssl`).

## 2) SSL certificate chain data generated from this build

Built a 3-level chain with the local `apps/openssl` binary:
- Root CA (`root.pem`)
- Intermediate CA (`int.pem`)
- Leaf/server cert (`server.pem`, CN=localhost, SAN=DNS:localhost)

Verification command:

```bash
OPENSSL_CONF=/workspace/openssl/apps/openssl.cnf \
  /workspace/openssl/apps/openssl verify -show_chain \
  -CAfile root.pem -untrusted int.pem server.pem
```

Observed output:

```text
server.pem: OK
Chain:
depth=0: CN=localhost (untrusted)
depth=1: CN=Intermediate CA (untrusted)
depth=2: CN=Root CA
```

Leaf certificate metadata:

```text
issuer=CN=Intermediate CA
subject=CN=localhost
notBefore=Apr  9 16:39:21 2026 GMT
notAfter=Jul 12 16:39:21 2028 GMT
```

Purpose check:

```bash
OPENSSL_CONF=/workspace/openssl/apps/openssl.cnf \
  /workspace/openssl/apps/openssl verify -purpose sslserver \
  -CAfile root.pem -untrusted int.pem server.pem
# server.pem: OK
```

Artifacts are in `/tmp/ossl_chain`.

## 3) EVP local-file context

There is currently **no `crypto/evp/evp_local.c`** in this workspace.

Only `crypto/evp/evp_local.h` exists. References were counted with:

```bash
rg -n 'evp_local\.h' crypto/evp test | wc -l
# 37
```

This means there is no current runtime output attributable to a standalone
`evp_local.c` translation unit in this checkout; EVP internals are distributed
across multiple `.c` files that include `evp_local.h`.

