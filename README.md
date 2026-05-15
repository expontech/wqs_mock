# WQS KVCache Mock

An in-memory, Apache-2.0-licensed mock of the WQS KVCache client library
(`libwclient_kvcache.so`).

## Why this exists

The NIXL WQSKV backend plugin in
[ai-dynamo/nixl](https://github.com/ai-dynamo/nixl)
(`src/plugins/wqskv/`) targets a proprietary WDS KVCache client library
that is not publicly distributed. This mock exposes the exact same ABI
(symbol set, function signatures, struct layouts declared in
`kv_interface.h`) so that:

- Upstream NIXL CI can build and link the WQSKV plugin against a public
  dependency.
- Plugin unit tests can run without a real WDS cluster.
- Downstream consumers can develop and smoke-test against the plugin
  before deploying with the real library.

To use the proprietary implementation in production, install the vendor
library to the same prefix (overwriting this mock's
`libwclient_kvcache.so`) or place it earlier on `LD_LIBRARY_PATH`. The
plugin code does not change.

## Scope

Three entry points are implemented faithfully (these are the ones the NIXL
plugin actually calls):

| Function | Behavior |
| --- | --- |
| `wds_kvcache_init` | First call wins; idempotent thereafter. Options are accepted but otherwise ignored. |
| `wds_kvcache_put` | Stores a copy of the iovec payload under the key. Rejects existing keys with `-EEXIST`. |
| `wds_kvcache_get_vec` | Copies the stored value into the caller's iovec list. Requires the iovec capacity to be `>= length(stored)`; otherwise `-ENOSPC`. Missing key yields `-ENOENT`. |

The remaining symbols declared in `kv_interface.h`
(`wds_kvcache_get`, `wds_kvcache_get_str`, `wds_kvcache_batch_get`,
`wds_kvcache_query`, `wds_block_*`, …) are stubs that report `-ENOSYS`
through the callback. They exist so the SONAME exports the same set as
the vendor library; the NIXL plugin does not use them.

Some convenience entries (`wds_kvcache_del`, `wds_kvcache_exist_key`,
`wds_kvcache_remove_all`) have full mock implementations because they were
trivial and useful for test harnesses.

## Semantics worth noting

- **Callbacks fire asynchronously** from a worker thread pool, mirroring
  the vendor library and exercising the consumer's wakeup paths (the
  NIXL plugin's `pending`-counter + condition-variable design in
  `src/plugins/wqskv/wqskv_backend.cpp`).
  - `wds_kvcache_init` spawns `max(1, opts.thread_num)` worker threads.
  - `opts.thread_mode` is accepted but workers always block on a
    condition variable; a pure-spin POLL mode is intentionally not
    modeled.
  - Operations that are accepted return `0` and report final status
    later via the callback.
  - Operations rejected up front (null cb, library not initialized,
    unsupported entry point) return a non-zero rc and never fire the
    callback. This matches the contract "rc != 0 means no callback".
- **Caller lifetime contract**: for `put`, the iovec.iov_base buffers
  must stay readable until the callback fires. For `get_vec`, both the
  `value` vector reference and its iov_base buffers must stay valid
  until the callback fires. The NIXL plugin holds the request handle
  alive until its pending counter reaches zero, which satisfies this.
- **Storage is in-process** and lives in a function-local `static`. It
  does not survive process exit and is not shared between processes.
- **Concurrency**: one global `std::mutex` serializes all KV-store
  accesses; the dispatch queue has its own mutex. This is deliberate
  for clarity; the mock is intended for correctness testing, not
  throughput.
- **Shutdown**: worker threads are joined from an `atexit` handler so
  the library unloads cleanly on normal process exit.

## Build & install

Requires a C++17 compiler and CMake ≥ 3.13.

```bash
git clone https://github.com/<owner>/wqs-mock.git
cd wqs-mock
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local ..
make -j
sudo make install
sudo ldconfig
```

This installs:

- `${prefix}/lib/libwclient_kvcache.so` (versioned, `SOVERSION=0`)
- `${prefix}/include/kv_interface.h`

After installing, NIXL's `meson setup` discovers the mock through its
normal `find_library('wclient_kvcache')` and pkg-config lookups; the
`-Dwqskv_lib_path` / `-Dwqskv_inc_path` options are not required when the
mock is installed to a default prefix.

## Smoke test

Configure with `-DWQS_MOCK_BUILD_TESTS=ON` (the default) and run
`ctest` from the build directory. Six basic checks cover put/get
round-trip, overwrite rejection, missing-key reads, short-buffer reads,
delete + existence, and the `-ENOSYS` stubs.

## Relationship to the vendor library

This mock describes the public surface of the WQS KVCache client ABI but
not its implementation. Schema, threading, distribution, RDMA, persistence,
mempool behavior, NUMA pinning, security, and performance characteristics
of the production library are all out of scope and behave as no-ops here.

For real-world deployments, replace `libwclient_kvcache.so` with the
vendor build that implements those properties.

## License

Apache License 2.0. See `LICENSE`.
