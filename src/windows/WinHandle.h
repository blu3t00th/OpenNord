#pragma once

#include <windows.h>

#include <utility>

namespace opennord::windows {

class UniqueHandle final
{
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE handle) : handle_(handle) {}
    ~UniqueHandle() { reset(); }

    UniqueHandle(const UniqueHandle &) = delete;
    UniqueHandle &operator=(const UniqueHandle &) = delete;
    UniqueHandle(UniqueHandle &&other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}
    UniqueHandle &operator=(UniqueHandle &&other) noexcept
    {
        if (this != &other) {
            reset(std::exchange(other.handle_, nullptr));
        }
        return *this;
    }

    [[nodiscard]] HANDLE get() const { return handle_; }
    [[nodiscard]] explicit operator bool() const { return handle_ && handle_ != INVALID_HANDLE_VALUE; }
    HANDLE release() { return std::exchange(handle_, nullptr); }
    void reset(HANDLE handle = nullptr)
    {
        if (*this) {
            CloseHandle(handle_);
        }
        handle_ = handle;
    }

private:
    HANDLE handle_{};
};

class UniqueServiceHandle final
{
public:
    UniqueServiceHandle() = default;
    explicit UniqueServiceHandle(SC_HANDLE handle) : handle_(handle) {}
    ~UniqueServiceHandle() { reset(); }
    UniqueServiceHandle(const UniqueServiceHandle &) = delete;
    UniqueServiceHandle &operator=(const UniqueServiceHandle &) = delete;
    UniqueServiceHandle(UniqueServiceHandle &&other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}
    UniqueServiceHandle &operator=(UniqueServiceHandle &&other) noexcept
    {
        if (this != &other) reset(std::exchange(other.handle_, nullptr));
        return *this;
    }
    [[nodiscard]] SC_HANDLE get() const { return handle_; }
    [[nodiscard]] explicit operator bool() const { return handle_ != nullptr; }
    void reset(SC_HANDLE handle = nullptr)
    {
        if (handle_) CloseServiceHandle(handle_);
        handle_ = handle;
    }
private:
    SC_HANDLE handle_{};
};

}

