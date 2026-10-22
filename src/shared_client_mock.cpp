// SPDX-FileCopyrightText: Copyright (c) 2026 Expontech. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include "shared_client.h"

#include <sys/stat.h>

#include <cerrno>
#include <climits>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace {
struct Store {
    std::mutex mutex;
    std::unordered_map<std::string, std::vector<char>> objects;
};

std::shared_ptr<Store>
processStore() {
    static auto store = std::make_shared<Store>();
    return store;
}
} // namespace

namespace wqs {
class SharedClient::Impl {
public:
    std::shared_ptr<Store> store = processStore();
};

SharedClient::SharedClient() : impl_(std::make_unique<Impl>()) {}

SharedClient::~SharedClient() = default;

int
SharedClient::setup(const std::string &,
                    const std::string &,
                    size_t,
                    size_t,
                    const std::string &,
                    const std::string &,
                    const std::string &) {
    std::lock_guard lock(impl_->store->mutex);
    if (has_inited_) {
        return -EINVAL;
    }
    has_inited_ = true;
    return 0;
}

int
SharedClient::register_buffer_with_fd(void *buffer, size_t size, int fd) {
    std::lock_guard lock(impl_->store->mutex);
    struct stat info{};
    if (!has_inited_ || vaddr_ || !buffer || size == 0 ||
        size > UINTPTR_MAX - reinterpret_cast<uintptr_t>(buffer) || fstat(fd, &info) != 0 ||
        !S_ISREG(info.st_mode) || info.st_size < 0 || static_cast<uintmax_t>(info.st_size) < size) {
        return -EINVAL;
    }
    vaddr_ = buffer;
    size_ = size;
    memfd_ = fd;
    return 0;
}

bool
SharedClient::is_buffer_valid(void *buffer, size_t size) {
    const auto base = reinterpret_cast<uintptr_t>(vaddr_);
    const auto address = reinterpret_cast<uintptr_t>(buffer);
    return has_inited_ && vaddr_ && buffer && size > 0 && size < INT_MAX && address >= base &&
        size <= size_ && address - base <= size_ - size;
}

int64_t
SharedClient::getSize(const std::string &key) {
    std::lock_guard lock(impl_->store->mutex);
    if (!has_inited_) {
        return -EINVAL;
    }
    const auto it = impl_->store->objects.find(key);
    return it == impl_->store->objects.end() ? 0 : static_cast<int64_t>(it->second.size());
}

std::vector<int>
SharedClient::batch_put_from(const std::vector<std::string> &keys,
                             const std::vector<void *> &buffers,
                             const std::vector<size_t> &sizes) {
    std::vector<int> results(keys.size(), -1);
    if (keys.size() != buffers.size() || keys.size() != sizes.size()) {
        return results;
    }
    std::lock_guard lock(impl_->store->mutex);
    for (size_t i = 0; i < keys.size(); ++i) {
        if (keys[i].empty() || !is_buffer_valid(buffers[i], sizes[i])) {
            return results;
        }
    }
    for (size_t i = 0; i < keys.size(); ++i) {
        const auto *begin = static_cast<const char *>(buffers[i]);
        impl_->store->objects[keys[i]] = std::vector<char>(begin, begin + sizes[i]);
        results[i] = 0;
    }
    return results;
}

std::vector<int>
SharedClient::batch_get_into(const std::vector<std::string> &keys,
                             const std::vector<void *> &buffers,
                             const std::vector<size_t> &sizes) {
    std::vector<int> results(keys.size(), 0);
    if (keys.size() != buffers.size() || keys.size() != sizes.size()) {
        return results;
    }
    std::lock_guard lock(impl_->store->mutex);
    for (size_t i = 0; i < keys.size(); ++i) {
        if (!is_buffer_valid(buffers[i], sizes[i])) {
            return results;
        }
    }
    for (size_t i = 0; i < keys.size(); ++i) {
        const auto it = impl_->store->objects.find(keys[i]);
        // Model exact logical lengths so a missing getSize() cannot silently pass.
        if (it == impl_->store->objects.end() || sizes[i] != it->second.size()) {
            continue;
        }
        std::memcpy(buffers[i], it->second.data(), sizes[i]);
        results[i] = static_cast<int>(sizes[i]);
    }
    return results;
}

int
SharedClient::get_into(const std::string &key, void *buffer, size_t size) {
    const auto results = batch_get_into({key}, {buffer}, {size});
    return results[0] > 0 ? results[0] : -EINVAL;
}

int
SharedClient::put_from(const std::string &key, void *buffer, size_t size) {
    return batch_put_from({key}, {buffer}, {size})[0];
}

int
SharedClient::isExist(const std::string &key) {
    return getSize(key) > 0;
}

std::vector<int>
SharedClient::batchIsExist(const std::vector<std::string> &keys) {
    std::vector<int> results;
    results.reserve(keys.size());
    for (const auto &key : keys) {
        results.push_back(isExist(key));
    }
    return results;
}

int
SharedClient::put(const std::string &key, const iovec value) {
    return put_from(key, value.iov_base, value.iov_len);
}

int
SharedClient::put_batch(const std::vector<std::string> &keys, const std::vector<iovec> &values) {
    std::vector<void *> buffers;
    std::vector<size_t> sizes;
    for (const auto &value : values) {
        buffers.push_back(value.iov_base);
        sizes.push_back(value.iov_len);
    }
    const auto results = batch_put_from(keys, buffers, sizes);
    for (const int result : results) {
        if (result != 0) {
            return result;
        }
    }
    return keys.size() == values.size() ? 0 : -EINVAL;
}

int
SharedClient::initAll(const std::string &, const std::string &, size_t) {
    return -ENOSYS;
}

int
SharedClient::remove(const std::string &) {
    return -ENOSYS;
}

long
SharedClient::removeByRegex(const std::string &) {
    return -ENOSYS;
}

long
SharedClient::removeAll() {
    return -ENOSYS;
}

int
SharedClient::register_buffer(void *, size_t) {
    return -ENOSYS;
}

int
SharedClient::unregister_buffer(void *) {
    return -ENOSYS;
}

int
SharedClient::unregister_buffer_with_fd(void *, int) {
    return -ENOSYS;
}

int
SharedClient::tearDownAll() {
    return -ENOSYS;
}

int
SharedClient::put_from_with_metadata(const std::string &, void *, void *, size_t, size_t) {
    return -ENOSYS;
}

std::vector<std::shared_ptr<BufferHandle>>
SharedClient::batch_get_buffer_internal(const std::vector<std::string> &,
                                        const std::shared_ptr<ClientBufferAllocator> &) {
    return {};
}

std::vector<std::shared_ptr<BufferHandle>>
SharedClient::batch_get_buffer(const std::vector<std::string> &keys) {
    return batch_get_buffer_internal(keys);
}

int
SharedClient::put_parts(const std::string &, const std::vector<iovec> &) {
    return -ENOSYS;
}
} // namespace wqs
