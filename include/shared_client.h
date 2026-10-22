// SPDX-FileCopyrightText: Copyright (c) 2026 Expontech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#ifndef WQS_MOCK_SHARED_CLIENT_H
#define WQS_MOCK_SHARED_CLIENT_H

#include <memory>
#include "pyclient.h"

namespace wqs {

class SharedClient : public PyClient {
public:
    SharedClient();
    ~SharedClient() override;
    int
    setup(const std::string &local_hostname,
          const std::string &metadata_server,
          size_t global_segment_size = 1024 * 1024 * 16,
          size_t local_buffer_size = 1024 * 1024 * 16,
          const std::string &protocol = "tcp",
          const std::string &rdma_devices = "",
          const std::string &master_server_addr = "127.0.0.1:50051") override;

    int
    initAll(const std::string &protocol,
            const std::string &device_name,
            size_t mount_segment_size = 1024 * 1024 * 16) override;
    int
    remove(const std::string &key) override;
    long
    removeByRegex(const std::string &str) override;
    long
    removeAll() override;
    int
    isExist(const std::string &key) override;
    std::vector<int>
    batchIsExist(const std::vector<std::string> &keys) override;
    int64_t
    getSize(const std::string &key) override;
    int
    register_buffer(void *buffer, size_t size) override;
    int
    unregister_buffer(void *buffer) override;
    int
    register_buffer_with_fd(void *buffer, size_t size, int fd) override;
    int
    unregister_buffer_with_fd(void *buffer, int fd) override;
    int
    get_into(const std::string &key, void *buffer, size_t size) override;
    std::vector<int>
    batch_get_into(const std::vector<std::string> &keys,
                   const std::vector<void *> &buffers,
                   const std::vector<size_t> &sizes) override;
    std::vector<std::shared_ptr<BufferHandle>>
    batch_get_buffer_internal(
        const std::vector<std::string> &keys,
        const std::shared_ptr<ClientBufferAllocator> &client_buffer_allocator = nullptr) override;
    std::vector<std::shared_ptr<BufferHandle>>
    batch_get_buffer(const std::vector<std::string> &keys) override;
    int
    tearDownAll() override;
    int
    put_from(const std::string &key, void *buffer, size_t size) override;
    int
    put_from_with_metadata(const std::string &key,
                           void *buffer,
                           void *metadata_buffer,
                           size_t size,
                           size_t metadata_size) override;
    std::vector<int>
    batch_put_from(const std::vector<std::string> &keys,
                   const std::vector<void *> &buffers,
                   const std::vector<size_t> &sizes) override;
    int
    put(const std::string &key, const iovec value) override;
    int
    put_batch(const std::vector<std::string> &keys, const std::vector<iovec> &values) override;
    int
    put_parts(const std::string &key, const std::vector<iovec> &values) override;

private:
    static void
    callback(int rc, void *arg);
    bool
    is_buffer_valid(void *buffer, size_t size);
    // Preserve the SDK's data member layout.
    class Impl;
    std::unique_ptr<Impl> impl_;
    int memfd_{-1};
    void *vaddr_{nullptr};
    size_t size_{0};
    bool has_inited_{false};
};

} // namespace wqs


#endif // WQS_MOCK_SHARED_CLIENT_H
