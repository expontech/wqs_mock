// SPDX-FileCopyrightText: Copyright (c) 2026 Expontech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "shared_client.h"

#include <linux/memfd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstring>
#include <future>
#include <stdexcept>

namespace {
void
require(bool condition, const char *message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class Region {
public:
    Region() {
        fd = static_cast<int>(syscall(SYS_memfd_create, "wqs-mock-test", MFD_CLOEXEC));
        require(fd >= 0, "memfd_create");
        if (ftruncate(fd, size) != 0) {
            close(fd);
            throw std::runtime_error("ftruncate");
        }
        data = static_cast<char *>(mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
        if (data == MAP_FAILED) {
            close(fd);
            throw std::runtime_error("mmap");
        }
    }

    ~Region() {
        munmap(data, size);
        close(fd);
    }

    Region(const Region &) = delete;
    Region &
    operator=(const Region &) = delete;

    int fd;
    char *data;
    static constexpr size_t size = 4096;
};

void
initialize(wqs::SharedClient &client, Region &region) {
    require(client.setup("", "", 0, 0, "", "", "/mock/wqs") == 0, "setup");
    require(client.register_buffer_with_fd(region.data, region.size, region.fd) == 0,
            "register buffer");
}

void
checkRegistrationAndRoundTrip() {
    Region region;
    wqs::SharedClient client;
    require(client.getSize("missing") < 0, "use before setup must fail");
    require(client.register_buffer_with_fd(region.data, region.size, region.fd) < 0,
            "registration before setup must fail");
    require(client.setup("", "") == 0, "setup");
    require(client.setup("", "") == -EINVAL, "repeated setup must fail");
    require(client.register_buffer_with_fd(region.data, region.size, -1) < 0, "invalid fd");
    require(client.register_buffer_with_fd(nullptr, region.size, region.fd) < 0, "null region");
    require(client.register_buffer_with_fd(region.data, region.size + 1, region.fd) < 0,
            "region exceeds fd size");
    require(client.register_buffer_with_fd(region.data, region.size, region.fd) == 0,
            "retry valid registration");
    require(client.register_buffer_with_fd(region.data, region.size, region.fd) == -EINVAL,
            "second registration must fail");

    for (size_t i = 0; i < 513; ++i) {
        region.data[i] = static_cast<char>(i % 251);
    }
    require(client.batch_put_from({"roundtrip"}, {region.data}, {513}) == std::vector<int>{0},
            "write success returns zero");
    require(client.getSize("roundtrip") == 513, "getSize returns logical length");
    const auto query = [](wqs::PyClient &base) { return base.getSize("roundtrip"); };
    require(query(client) == 513, "PyClient virtual dispatch");
    require(client.batch_get_into({"roundtrip"}, {region.data + 1024}, {513}) ==
                std::vector<int>{513},
            "read success returns actual bytes");
    require(std::memcmp(region.data, region.data + 1024, 513) == 0, "unaligned round trip");
    std::memset(region.data, 'x', 513);
    require(client.get_into("roundtrip", region.data + 2048, 513) == 513 && region.data[2049] == 1,
            "write must copy caller data");
    require(client.isExist("roundtrip") == 1 && client.isExist("missing") == 0,
            "existence results");
}

void
checkErrorsAndBatchResults() {
    Region region;
    wqs::SharedClient client;
    initialize(client, region);
    std::memset(region.data, 'a', region.size);
    require(client.batch_put_from({"a", "b"}, {region.data, region.data + 32}, {17, 31}) ==
                std::vector<int>({0, 0}),
            "batch writes");
    require(client.batch_put_from({"no-partial", "invalid"},
                                  {region.data, region.data + region.size},
                                  {17, 1}) == std::vector<int>({-1, -1}),
            "invalid range rejects whole batch");
    require(client.getSize("no-partial") == 0, "invalid batch must not write first key");
    require(client.batch_put_from({"a"}, {}, {}) == std::vector<int>{-1}, "bad write counts");
    require(client.batch_put_from({"a"}, {region.data}, {static_cast<size_t>(INT_MAX)}) ==
                std::vector<int>{-1},
            "oversized length");
    require(client.batch_get_into({"a"}, {}, {}) == std::vector<int>{0}, "bad read counts");
    require(client.batch_get_into({"a"}, {region.data + region.size}, {17}) == std::vector<int>{0},
            "out-of-range read");

    std::memset(region.data + 1024, 'x', 128);
    require(client.batch_get_into({"a", "missing", "b"},
                                  {region.data + 1024, region.data + 1056, region.data + 1088},
                                  {17, 17, 31}) == std::vector<int>({17, 0, 31}),
            "mixed hit/miss batch preserves result order");
    require(region.data[1056] == 'x', "missing key must not alter buffer");
    for (size_t length : {size_t{16}, size_t{18}}) {
        std::memset(region.data + 1024, 'x', 32);
        require(client.batch_get_into({"a"}, {region.data + 1024}, {length}) == std::vector<int>{0},
                "wrong logical size must fail");
        require(region.data[1024] == 'x', "size mismatch must not copy");
    }
    require(client.put_from("a", region.data, 18) == 0, "overwrite");
    require(client.getSize("a") == 18, "getSize must observe replacement length");
    require(client.get_into("a", region.data + 1024, 18) == 18, "single-object read");
    require(client.batchIsExist({"missing", "a", "b"}) == std::vector<int>({0, 1, 1}),
            "batch existence order");
    require(client.unregister_buffer_with_fd(region.data, region.fd) == -ENOSYS,
            "unsupported unregister must not pretend success");
    require(client.remove("a") == -ENOSYS && client.tearDownAll() == -ENOSYS,
            "unsupported operations report ENOSYS");
}

void
checkSharedStoreAndConcurrency() {
    std::vector<std::future<void>> jobs;
    for (int i = 0; i < 4; ++i) {
        jobs.push_back(std::async(std::launch::async, [i] {
            Region region;
            wqs::SharedClient client;
            initialize(client, region);
            const std::string key = "concurrent/" + std::to_string(i);
            std::memset(region.data, 'a' + i, 513);
            for (int count = 0; count < 16; ++count) {
                require(client.put_from(key, region.data, 513) == 0, "concurrent write");
                require(client.getSize(key) == 513, "concurrent size query");
                require(client.get_into(key, region.data + 1024, 513) == 513, "concurrent read");
                require(std::memcmp(region.data, region.data + 1024, 513) == 0,
                        "concurrent data integrity");
            }
        }));
    }
    for (auto &job : jobs) {
        job.get();
    }
    Region region;
    wqs::SharedClient another;
    initialize(another, region);
    require(another.get_into("concurrent/2", region.data, 513) == 513,
            "objects survive client destruction within one process");
    require(std::all_of(region.data, region.data + 513, [](char value) { return value == 'c'; }),
            "cross-client object data");
}
} // namespace

int
main() {
    try {
        checkRegistrationAndRoundTrip();
        checkErrorsAndBatchResults();
        checkSharedStoreAndConcurrency();
        std::puts("PASS: SharedClient registration, logical sizes, batches and concurrency");
        return 0;
    }
    catch (const std::exception &error) {
        std::fprintf(stderr, "FAIL: %s\n", error.what());
        return 1;
    }
}
