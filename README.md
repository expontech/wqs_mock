<!--
SPDX-FileCopyrightText: Copyright (c) 2026 Expontech. All rights reserved.
SPDX-License-Identifier: Apache-2.0
-->

# WQS mock SDK

Apache-2.0 implementations of two WQS client interfaces for NIXL plugin builds
and correctness tests:

- `libwqs_store.so` and `shared_client.h` for the current **WQS** backend using
  `wqs::SharedClient`, `DRAM_SEG`, and `OBJ_SEG`.
- `libwclient_kvcache.so` and `kv_interface.h` for the legacy **WQSKV** backend.

Both implementations store objects in process memory. They require no WQS
cluster, wqsd, SPDK, RDMA device, or proprietary library. The two interfaces
have independent stores; data does not survive process exit or cross process
boundaries. This SDK is intended for functional tests, not performance testing.

## Build and install

Requires Linux, a C++17 compiler, CMake 3.13 or newer, and POSIX threads.

```bash
git clone https://github.com/expontech/wqs_mock.git
cd wqs_mock
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/path/to/wqs-mock \
  -DCMAKE_INSTALL_LIBDIR=lib
cmake --build build --parallel
ctest --test-dir build --output-on-failure
cmake --install build
```

The default build installs both libraries and their public headers. Use
`-DWQS_MOCK_BUILD_LEGACY=OFF` to install only `libwqs_store.so`, `shared_client.h`,
and `pyclient.h`. `WQS_MOCK_BUILD_TESTS` defaults to `ON`.

`libwqs_store.so` has an unversioned SONAME matching the SDK name consumed by
NIXL. The legacy library retains `SOVERSION=0`. No production sources or
proprietary binaries are downloaded or bundled.

## SharedClient interface

Include `<shared_client.h>` and link with `-lwqs_store`.

| Method | Mock behavior |
| --- | --- |
| `setup(...)` | Succeeds once per client; repeated setup returns `-EINVAL`. Connection parameters are accepted but no connection is opened. |
| `register_buffer_with_fd(buffer, bytes, fd)` | Registers one caller-owned range after setup. Checks the pointer, length, and backing file descriptor; success returns `0`. |
| `getSize(key)` | Returns the stored positive logical length, `0` for a miss, or a negative result before setup. |
| `batch_put_from(keys, buffers, sizes)` | Copies each value from the registered range. Returns one `0` per object on success or all `-1` for an invalid batch. |
| `batch_get_into(keys, buffers, sizes)` | Copies each hit into the registered range and returns its actual byte count. A missing key or wrong logical length returns `0` for that entry without copying. |
| `put_from` / `get_into` | Single-object wrappers; WRITE success is `0`, READ success is the positive byte count. |
| `isExist` / `batchIsExist` | Return `1` for a positive-size object and `0` otherwise, preserving input order. |
| `put` / `put_batch` | Convenience wrappers for iovec-based writes. |

All SharedClient data calls complete synchronously, as in the SDK wrapper.
Objects are shared across SharedClient instances within one process. A mutex
serializes accesses, including registration state. Writes replace existing
values, so size queries observe the latest write. Applications must enforce
the NIXL WQS backend's immutable-key contract themselves.

The caller owns the registered mapping and file descriptor. The mock neither
maps, unmaps, closes, nor duplicates them; keep them alive while using the client.
An ordinary memfd works for SDK tests. SDK tests exercise invalid registrations,
unaligned payloads, incorrect lengths, mixed-hit batches, and concurrent clients.

Reads require the exact stored logical length, even if the destination could
hold more bytes. This deliberately catches consumers that omit the size query.
Invalid vector lengths or registered ranges reject the entire batch before any
copy. Storage allocation failures may throw standard C++ allocation exceptions.

The headers retain the `PyClient` virtual method order and `SharedClient` object
layout used by the current SDK. Buffer-handle types are opaque. Unimplemented
integer-returning methods, including unregister, removal, teardown, and metadata
writes, return `-ENOSYS`; buffer-handle methods return an empty vector. Do not
infer production support from these declarations. C++ ABI compatibility depends
on the SDK version, compiler, and standard library; rebuild production consumers
against their production SDK.

## Build the NIXL WQS plugin

With a NIXL checkout containing `src/plugins/wqs`, configure its normal Meson
options to use this installation:

```bash
meson setup build-wqs-tests --buildtype=debug \
  -Denable_plugins=WQS -Dbuild_tests=true \
  -Dwqs_inc_path=/path/to/wqs-mock/include \
  -Dwqs_lib_path=/path/to/wqs-mock/lib
meson compile -C build-wqs-tests wqs_unit
meson test -C build-wqs-tests --no-rebuild --suite wqs --print-errorlogs
```

These injected-client plugin unit tests need neither hugepages nor wqsd. To run
NIXL transfers through the production session and this mock, NIXL still allocates
its hugepage-backed staging pool. Reserve 2 MiB hugepages for that pool; the mock
does not change NIXL's allocation policy. Make the mock library available through
the runtime loader's search paths. Production deployments must use the real SDK.

## Legacy KVCache interface

The legacy WQSKV plugin uses `libwclient_kvcache.so` and `kv_interface.h`.
Its API and callback behavior remain available when `WQS_MOCK_BUILD_LEGACY=ON`:

- `wds_kvcache_init` initializes a worker pool once per process.
- `wds_kvcache_put` copies iovecs into the store and rejects existing keys with
  `-EEXIST`.
- `wds_kvcache_get_vec` requires capacity at least as large as the stored value;
  misses return `-ENOENT` and short buffers return `-ENOSPC`.
- Delete, existence, and remove-all helpers are implemented. Other entries
  return `-ENOSYS` synchronously without invoking a callback.

Accepted operations return `0` and deliver their result asynchronously from a
worker thread. A nonzero submission result means no callback will arrive. The
caller must retain iovec containers and buffers until completion. The global
store and dispatch queue are protected by separate mutexes; workers are joined
on normal process exit. The legacy smoke test covers this contract.

## License

Apache License 2.0. See [LICENSE](LICENSE).
