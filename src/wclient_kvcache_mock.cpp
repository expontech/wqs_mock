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
 * In-memory mock of the WQS KVCache client library.
 *
 * Provides an Apache-2.0 implementation of the symbol set declared in
 * kv_interface.h so consumers (notably the NIXL WQSKV backend plugin) can
 * link, build, and run end-to-end in CI without the proprietary vendor
 * library.
 *
 * Threading model
 * ---------------
 * Callbacks are invoked from a small worker thread pool, not on the
 * submitting thread, in order to mirror the vendor library's async
 * dispatch and to exercise the consumer's wakeup paths (the NIXL plugin's
 * std::atomic pending counter + condition-variable wakeup design).
 *
 * - wds_kvcache_init spawns max(1, opts.thread_num) worker threads.
 *   opts.thread_mode is accepted but treated uniformly as event-driven
 *   (workers block on a condition variable). A pure-spin POLL mode is
 *   intentionally not modeled — it would burn a core for no test value.
 * - Calls that accept the request enqueue a task and return 0
 *   immediately; the callback fires later from one of the worker
 *   threads with the operation's final status.
 * - Calls rejected up front (null cb, library not initialized,
 *   unsupported entry point) return a non-zero rc synchronously and
 *   never fire the callback, matching the contract "rc != 0 means no
 *   callback will arrive".
 *
 * Caller lifetime contract
 * ------------------------
 * For put: the iovec.iov_base buffers must remain readable until the
 * callback fires. The mock copies the data into its store inside the
 * worker, not in the submitting call.
 *
 * For get_vec: both the `value` vector reference and the iov_base
 * buffers must remain valid until the callback fires. The mock writes
 * the result through the caller's iovec list inside the worker.
 *
 * The NIXL WQSKV plugin satisfies both contracts by holding a request
 * handle alive until its pending counter reaches zero.
 */

#include "kv_interface.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace {

struct State {
    // KV store
    std::mutex store_mu;
    std::unordered_map<std::string, std::vector<uint8_t>> store;
    bool inited = false;

    // Worker pool
    std::mutex q_mu;
    std::condition_variable q_cv;
    std::queue<std::function<void()>> tasks;
    std::vector<std::thread> workers;
    std::atomic<bool> stop{false};
};

State &
state() {
    static State s;
    return s;
}

void
worker_loop() {
    State &s = state();
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lk(s.q_mu);
            s.q_cv.wait(lk, [&] { return s.stop.load() || !s.tasks.empty(); });
            if (s.stop.load() && s.tasks.empty()) {
                return;
            }
            task = std::move(s.tasks.front());
            s.tasks.pop();
        }
        task();
    }
}

void
enqueue(std::function<void()> w) {
    State &s = state();
    {
        std::lock_guard<std::mutex> lk(s.q_mu);
        s.tasks.push(std::move(w));
    }
    s.q_cv.notify_one();
}

void
shutdown_workers() {
    State &s = state();
    {
        std::lock_guard<std::mutex> lk(s.q_mu);
        s.stop.store(true);
    }
    s.q_cv.notify_all();
    for (auto &t : s.workers) {
        if (t.joinable()) {
            t.join();
        }
    }
    s.workers.clear();
}

size_t
iovec_total_len(const std::vector<iovec> &iov) {
    size_t total = 0;
    for (const auto &v : iov) {
        total += v.iov_len;
    }
    return total;
}

std::vector<uint8_t>
copy_iovecs_in(const std::vector<iovec> &src) {
    std::vector<uint8_t> dst;
    dst.reserve(iovec_total_len(src));
    for (const auto &v : src) {
        const auto *p = static_cast<const uint8_t *>(v.iov_base);
        dst.insert(dst.end(), p, p + v.iov_len);
    }
    return dst;
}

// Copies `src` bytes into the caller's iovec list `dst`. Returns -ENOSPC
// when the caller-supplied capacity is smaller than `src.size()`.
int
copy_iovecs_out(const std::vector<uint8_t> &src,
                std::vector<iovec> &dst,
                size_t length) {
    if (length < src.size()) {
        return -ENOSPC;
    }
    size_t remaining = src.size();
    size_t offset = 0;
    for (auto &v : dst) {
        if (remaining == 0) {
            break;
        }
        const size_t to_copy = std::min(remaining, v.iov_len);
        std::memcpy(v.iov_base, src.data() + offset, to_copy);
        offset += to_copy;
        remaining -= to_copy;
    }
    return 0;
}

}  // namespace

extern "C" {

int
wds_kvcache_init(KVCacheOptions &opts) {
    State &s = state();
    std::lock_guard<std::mutex> lk(s.store_mu);
    if (s.inited) {
        return 0;
    }
    const int n = std::max(1, opts.thread_num);
    s.workers.reserve(static_cast<size_t>(n));
    for (int i = 0; i < n; ++i) {
        s.workers.emplace_back(worker_loop);
    }
    static std::once_flag atexit_flag;
    std::call_once(atexit_flag, [] { std::atexit(shutdown_workers); });
    s.inited = true;
    return 0;
}

int
wds_kvcache_put(const std::string &key,
                const std::vector<iovec> &value,
                KVCacheCb cb,
                void *cb_arg) {
    if (cb == nullptr) {
        return -EINVAL;
    }
    if (!state().inited) {
        return -EAGAIN;
    }
    // Capture key by value (copy is cheap). value is captured by value too,
    // which copies the iovec metadata (pointer + length); the underlying
    // buffers are owned by the caller and must remain valid until cb fires.
    enqueue([key, value, cb, cb_arg] {
        State &s = state();
        int rc = 0;
        {
            std::lock_guard<std::mutex> lk(s.store_mu);
            if (s.store.find(key) != s.store.end()) {
                rc = -EEXIST;
            } else {
                s.store.emplace(key, copy_iovecs_in(value));
            }
        }
        cb(rc, cb_arg);
    });
    return 0;
}

int
wds_kvcache_get_vec(const std::string &key,
                    std::vector<iovec> &value,
                    size_t length,
                    KVCacheCb cb,
                    void *cb_arg) {
    if (cb == nullptr) {
        return -EINVAL;
    }
    if (!state().inited) {
        return -EAGAIN;
    }
    // The caller's `value` vector and its iov_base buffers must outlive the
    // callback; capture by raw pointer to honor that contract.
    std::vector<iovec> *value_ptr = &value;
    enqueue([key, value_ptr, length, cb, cb_arg] {
        State &s = state();
        std::vector<uint8_t> snapshot;
        bool exists = false;
        {
            std::lock_guard<std::mutex> lk(s.store_mu);
            auto it = s.store.find(key);
            if (it != s.store.end()) {
                snapshot = it->second;
                exists = true;
            }
        }
        int rc;
        if (!exists) {
            rc = -ENOENT;
        } else {
            rc = copy_iovecs_out(snapshot, *value_ptr, length);
        }
        cb(rc, cb_arg);
    });
    return 0;
}

int
wds_kvcache_del(const std::string &key, KVCacheCb cb, void *cb_arg) {
    if (cb == nullptr) {
        return -EINVAL;
    }
    if (!state().inited) {
        return -EAGAIN;
    }
    enqueue([key, cb, cb_arg] {
        State &s = state();
        bool removed = false;
        {
            std::lock_guard<std::mutex> lk(s.store_mu);
            removed = s.store.erase(key) > 0;
        }
        cb(removed ? 0 : -ENOENT, cb_arg);
    });
    return 0;
}

int
wds_kvcache_exist_key(const std::string &key, KVCacheCb cb, void *cb_arg) {
    if (cb == nullptr) {
        return -EINVAL;
    }
    if (!state().inited) {
        return -EAGAIN;
    }
    enqueue([key, cb, cb_arg] {
        State &s = state();
        bool exists = false;
        {
            std::lock_guard<std::mutex> lk(s.store_mu);
            exists = s.store.find(key) != s.store.end();
        }
        cb(exists ? 0 : -ENOENT, cb_arg);
    });
    return 0;
}

int
wds_kvcache_remove_all() {
    State &s = state();
    std::lock_guard<std::mutex> lk(s.store_mu);
    s.store.clear();
    return 0;
}

// ---------------------------------------------------------------------------
// Entry points not used by the NIXL WQSKV plugin. Returning -ENOSYS without
// firing the callback honors the "rc != 0 -> cb won't be invoked" contract.
// They exist so the SONAME exports the same symbol set as the vendor build.
// ---------------------------------------------------------------------------

int
wds_kvcache_get(const std::string &key,
                std::vector<iovec> &value,
                KVCacheCb cb,
                void *cb_arg) {
    (void)key;
    (void)value;
    (void)cb;
    (void)cb_arg;
    return -ENOSYS;
}

int
wds_kvcache_batch_get(const std::vector<std::string> &keys,
                      const std::vector<void *> &buffers,
                      const std::vector<size_t> &in_sizes,
                      std::vector<int64_t> &out_sizes,
                      KVCacheCb cb,
                      void *cb_arg) {
    (void)keys;
    (void)buffers;
    (void)in_sizes;
    (void)out_sizes;
    (void)cb;
    (void)cb_arg;
    return -ENOSYS;
}

int
wds_kvcache_get_str(const std::string &key,
                    std::string &value,
                    size_t length,
                    KVCacheCb cb,
                    void *cb_arg) {
    (void)key;
    (void)value;
    (void)length;
    (void)cb;
    (void)cb_arg;
    return -ENOSYS;
}

int
wds_kvcache_query(const std::string &key, KVCacheCb cb, void *cb_arg) {
    (void)key;
    (void)cb;
    (void)cb_arg;
    return -ENOSYS;
}

int
wds_block_write(void *data, size_t len, uint64_t offset, BlockCb cb, void *cb_arg) {
    (void)data;
    (void)len;
    (void)offset;
    (void)cb;
    (void)cb_arg;
    return -ENOSYS;
}

int
wds_block_read(void *data, size_t len, uint64_t offset, BlockCb cb, void *cb_arg) {
    (void)data;
    (void)len;
    (void)offset;
    (void)cb;
    (void)cb_arg;
    return -ENOSYS;
}

}  // extern "C"
