/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 The WQS KVCache Mock Authors.
 * SPDX-License-Identifier: Apache-2.0
 */

#include "kv_interface.h"

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace {

struct CbState {
    int rc = -1;
    std::atomic<bool> fired{false};

    void
    wait() const {
        while (!fired.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
    }
};

void
record_cb(int rc, void *arg) {
    auto *s = static_cast<CbState *>(arg);
    s->rc = rc;
    s->fired.store(true, std::memory_order_release);
}

bool
expect_eq(int expected, int actual, const char *what) {
    if (expected != actual) {
        std::fprintf(stderr, "FAIL %s: expected %d got %d\n", what, expected, actual);
        return false;
    }
    return true;
}

bool
expect_true(bool cond, const char *what) {
    if (!cond) {
        std::fprintf(stderr, "FAIL %s\n", what);
        return false;
    }
    return true;
}

}  // namespace

int
main() {
    KVCacheOptions opts;
    opts.thread_num = 2;
    opts.thread_mode = KVCACHE_THREAD_EVENT;
    if (wds_kvcache_init(opts) != 0) {
        std::fprintf(stderr, "init failed\n");
        return 1;
    }

    // 1. Round-trip put/get; cb fires asynchronously.
    std::vector<uint8_t> payload = {'h', 'e', 'l', 'l', 'o'};
    std::vector<iovec> put_iov = {{payload.data(), payload.size()}};
    CbState s1;
    if (wds_kvcache_put("k1", put_iov, record_cb, &s1) != 0) {
        std::fprintf(stderr, "put k1 submit failed\n");
        return 1;
    }
    s1.wait();
    if (!expect_eq(0, s1.rc, "put k1 cb rc")) {
        return 1;
    }

    std::vector<uint8_t> buf(8, 0);
    std::vector<iovec> get_iov = {{buf.data(), buf.size()}};
    CbState s2;
    if (wds_kvcache_get_vec("k1", get_iov, buf.size(), record_cb, &s2) != 0) {
        std::fprintf(stderr, "get k1 submit failed\n");
        return 1;
    }
    s2.wait();
    if (!expect_eq(0, s2.rc, "get k1 cb rc")) {
        return 1;
    }
    if (!expect_true(std::memcmp(buf.data(), payload.data(), payload.size()) == 0,
                     "k1 payload round-trip")) {
        return 1;
    }

    // 2. put rejects overwrite (delivered via cb).
    CbState s3;
    wds_kvcache_put("k1", put_iov, record_cb, &s3);
    s3.wait();
    if (!expect_eq(-EEXIST, s3.rc, "put k1 overwrite cb rc")) {
        return 1;
    }

    // 3. get of missing key.
    CbState s4;
    wds_kvcache_get_vec("k_missing", get_iov, buf.size(), record_cb, &s4);
    s4.wait();
    if (!expect_eq(-ENOENT, s4.rc, "get missing key cb rc")) {
        return 1;
    }

    // 4. get with too-small buffer.
    std::vector<uint8_t> tiny(2, 0);
    std::vector<iovec> tiny_iov = {{tiny.data(), tiny.size()}};
    CbState s5;
    wds_kvcache_get_vec("k1", tiny_iov, tiny.size(), record_cb, &s5);
    s5.wait();
    if (!expect_eq(-ENOSPC, s5.rc, "short-buffer cb rc")) {
        return 1;
    }

    // 5. del + exist_key (each delivered via cb).
    CbState s6;
    wds_kvcache_del("k1", record_cb, &s6);
    s6.wait();
    if (!expect_eq(0, s6.rc, "del k1 cb rc")) {
        return 1;
    }
    CbState s7;
    wds_kvcache_exist_key("k1", record_cb, &s7);
    s7.wait();
    if (!expect_eq(-ENOENT, s7.rc, "exist after del cb rc")) {
        return 1;
    }

    // 6. Unimplemented entry points return -ENOSYS synchronously and do NOT
    // fire the callback (contract: rc != 0 means no cb).
    CbState s8;
    std::vector<iovec> dummy_iov;
    const int rc8 = wds_kvcache_get("k1", dummy_iov, record_cb, &s8);
    if (!expect_eq(-ENOSYS, rc8, "get stub return")) {
        return 1;
    }
    // Brief yield to detect any incorrect async cb firing.
    std::this_thread::yield();
    if (!expect_true(!s8.fired.load(), "get stub must not fire cb")) {
        return 1;
    }

    std::printf("OK: 6 smoke checks passed (async cb delivery confirmed)\n");
    return 0;
}
