/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 The WQS KVCache Mock Authors.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * WQS KVCache client API.
 *
 * This header describes the surface that a NIXL WQSKV backend plugin (or any
 * other consumer) links against. Two implementations of this ABI exist:
 *
 *   - This mock library, distributed under Apache-2.0, providing an
 *     in-memory backend suitable for CI, smoke tests, and local
 *     development. Async callbacks are dispatched synchronously inside the
 *     submitting call.
 *   - The proprietary WDS KVCache client (libwclient_kvcache.so) from the
 *     vendor, providing a distributed RDMA-backed implementation. Users
 *     replace the mock at runtime by swapping LD_LIBRARY_PATH or by
 *     installing the vendor library to the same prefix.
 *
 * Note: the functions below are declared extern "C" to suppress C++ name
 * mangling so a single SONAME serves both consumers, but the parameter ABI
 * is C++ (std::string, std::vector). Linkers and the libstdc++ ABI must
 * match between the consumer and the chosen implementation.
 */

#ifndef KV_INTERFACE_H
#define KV_INTERFACE_H

#include <sys/uio.h>
#include <string.h>

#include <cstdint>
#include <string>
#include <vector>

#ifdef __cplusplus
extern "C" {
#endif

enum KVCacheThreadMode {
    KVCACHE_THREAD_POLL,
    KVCACHE_THREAD_EVENT
};

struct KVCacheOptions {
    KVCacheOptions() {
        memset(&poolid, 0, sizeof(KVCacheOptions) - offsetof(KVCacheOptions, poolid));
    }
    std::vector<int> bind_cpus;
    std::string conf;

    // POD trailing fields below; the ctor memsets these to zero.
    uint32_t poolid;
    int thread_num;
    KVCacheThreadMode thread_mode;
    uint32_t nid;
    uint32_t bvar_dummy_port;
    uint32_t mem_size;
    uint32_t mempool_obj_sz;
    uint32_t mempool_obj_num;
    bool use_round_robin;
    bool debug_log;
};

typedef void(*KVCacheCb)(int rc, void *arg);

// Initializes the client. Idempotent: the first call wins; subsequent calls
// return 0 without changing state. Returns 0 on success.
int wds_kvcache_init(KVCacheOptions &opts);

// Stores `value` under `key`. Existing keys are rejected (-EEXIST). The
// callback fires with the final status. The library returns 0 once the
// operation is accepted; the cb may have already run synchronously by then.
int wds_kvcache_put(const std::string &key,
                    const std::vector<iovec> &value,
                    KVCacheCb cb,
                    void *cb_arg);

// Reads `key` into `value` (caller-allocated iovecs). `length` must be the
// sum of the iovec lengths and >= the stored value size. On size mismatch
// the cb fires with -ENOSPC.
int wds_kvcache_get_vec(const std::string &key,
                        std::vector<iovec> &value,
                        size_t length,
                        KVCacheCb cb,
                        void *cb_arg);

// The following entry points are part of the vendor ABI but are not used by
// the NIXL WQSKV plugin. The mock implements them as no-ops returning
// -ENOSYS (via the callback when present) so that the SONAME exports the
// same symbol set as the proprietary library.

int wds_kvcache_get(const std::string &key,
                    std::vector<iovec> &value,
                    KVCacheCb cb,
                    void *cb_arg);

int wds_kvcache_batch_get(const std::vector<std::string> &keys,
                          const std::vector<void *> &buffers,
                          const std::vector<size_t> &in_sizes,
                          std::vector<int64_t> &out_sizes,
                          KVCacheCb cb,
                          void *cb_arg);

int wds_kvcache_get_str(const std::string &key,
                        std::string &value,
                        size_t length,
                        KVCacheCb cb,
                        void *cb_arg);

int wds_kvcache_del(const std::string &key, KVCacheCb cb, void *cb_arg);
int wds_kvcache_query(const std::string &key, KVCacheCb cb, void *cb_arg);
int wds_kvcache_exist_key(const std::string &key, KVCacheCb cb, void *cb_arg);
int wds_kvcache_remove_all();

typedef void(*BlockCb)(int rc, void *arg);
int wds_block_write(void *data, size_t len, uint64_t offset, BlockCb cb, void *cb_arg);
int wds_block_read(void *data, size_t len, uint64_t offset, BlockCb cb, void *cb_arg);

#ifdef __cplusplus
}
#endif

#endif  // KV_INTERFACE_H
