// SPDX-FileCopyrightText: Copyright (c) 2026 Expontech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
#ifndef WQS_MOCK_PYCLIENT_H
#define WQS_MOCK_PYCLIENT_H

#include <sys/uio.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace wqs {

class BufferHandle;
class ClientBufferAllocator;

// Keep virtual declarations in SDK order for callers built with the SDK headers.
class PyClient {
public:
    PyClient() {}

    virtual ~PyClient() {}

    virtual int
    setup(const std::string &local_hostname,
          const std::string &metadata_server,
          size_t global_segment_size = 0,
          size_t local_buffer_size = 0,
          const std::string &protocol = "tcp",
          const std::string &rdma_devices = "",
          const std::string &master_server_addr = "127.0.0.1:50051") = 0;

    virtual int
    initAll(const std::string &protocol,
            const std::string &device_name,
            size_t mount_segment_size = 1024 * 1024 * 16) = 0;
    virtual int
    remove(const std::string &key) = 0;
    virtual long
    removeByRegex(const std::string &str) = 0;
    virtual long
    removeAll() = 0;
    virtual int
    isExist(const std::string &key) = 0;
    virtual std::vector<int>
    batchIsExist(const std::vector<std::string> &keys) = 0;
    virtual int64_t
    getSize(const std::string &key) = 0;
    virtual int
    register_buffer(void *buffer, size_t size) = 0;
    virtual int
    unregister_buffer(void *buffer) = 0;
    virtual int
    register_buffer_with_fd(void *buffer, size_t size, int fd) = 0;
    virtual int
    unregister_buffer_with_fd(void *buffer, int fd) = 0;
    virtual int
    get_into(const std::string &key, void *buffer, size_t size) = 0;
    virtual std::vector<int>
    batch_get_into(const std::vector<std::string> &keys,
                   const std::vector<void *> &buffers,
                   const std::vector<size_t> &sizes) = 0;
    virtual std::vector<std::shared_ptr<BufferHandle>>
    batch_get_buffer_internal(
        const std::vector<std::string> &keys,
        const std::shared_ptr<ClientBufferAllocator> &client_buffer_allocator = nullptr) = 0;
    virtual std::vector<std::shared_ptr<BufferHandle>>
    batch_get_buffer(const std::vector<std::string> &keys) = 0;
    virtual int
    tearDownAll() = 0;
    virtual int
    put_from(const std::string &key, void *buffer, size_t size) = 0;
    virtual int
    put_from_with_metadata(const std::string &key,
                           void *buffer,
                           void *metadata_buffer,
                           size_t size,
                           size_t metadata_size) = 0;
    virtual std::vector<int>
    batch_put_from(const std::vector<std::string> &keys,
                   const std::vector<void *> &buffers,
                   const std::vector<size_t> &sizes) = 0;
    virtual int
    put(const std::string &key, const iovec value) = 0;
    virtual int
    put_batch(const std::vector<std::string> &keys, const std::vector<iovec> &values) = 0;
    virtual int
    put_parts(const std::string &key, const std::vector<iovec> &values) = 0;
};

} // namespace wqs

#endif // WQS_MOCK_PYCLIENT_H
