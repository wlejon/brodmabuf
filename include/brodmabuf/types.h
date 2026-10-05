#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace brodmabuf {

/// Maximum number of planes supported by DMA-BUF and DRM format specifications.
inline constexpr size_t kMaxPlanes = 4;

/// RAII wrapper for a POSIX file descriptor.
class UniqueFd {
public:
    constexpr UniqueFd() noexcept : fd_(-1) {}
    explicit UniqueFd(int fd) noexcept : fd_(fd) {}

    ~UniqueFd() noexcept;

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    UniqueFd(UniqueFd&& other) noexcept : fd_(other.release()) {}

    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    [[nodiscard]] int get() const noexcept { return fd_; }
    [[nodiscard]] bool valid() const noexcept { return fd_ >= 0; }
    explicit operator bool() const noexcept { return valid(); }

    int release() noexcept {
        int old = fd_;
        fd_ = -1;
        return old;
    }

    void reset(int new_fd = -1) noexcept;

    /// Duplicates the file descriptor with O_CLOEXEC set.
    /// Returns an invalid UniqueFd on failure.
    [[nodiscard]] UniqueFd dup() const;

private:
    int fd_ = -1;
};

/// Status codes for operations across the library.
enum class StatusCode {
    Ok = 0,
    InvalidArgument,
    NotFound,
    Unsupported,
    DeviceError,
    OutOfMemory,
    Timeout,
    SystemError,
};

/// Represents status of an operation with an optional error description.
class Status {
public:
    constexpr Status() noexcept : code_(StatusCode::Ok) {}
    Status(StatusCode code, std::string message) : code_(code), message_(std::move(message)) {}

    static Status ok() noexcept { return Status(); }
    static Status invalid_argument(std::string msg) { return Status(StatusCode::InvalidArgument, std::move(msg)); }
    static Status not_found(std::string msg) { return Status(StatusCode::NotFound, std::move(msg)); }
    static Status unsupported(std::string msg) { return Status(StatusCode::Unsupported, std::move(msg)); }
    static Status device_error(std::string msg) { return Status(StatusCode::DeviceError, std::move(msg)); }
    static Status out_of_memory(std::string msg) { return Status(StatusCode::OutOfMemory, std::move(msg)); }
    static Status timeout(std::string msg) { return Status(StatusCode::Timeout, std::move(msg)); }
    static Status system_error(std::string msg) { return Status(StatusCode::SystemError, std::move(msg)); }

    [[nodiscard]] bool is_ok() const noexcept { return code_ == StatusCode::Ok; }
    explicit operator bool() const noexcept { return is_ok(); }
    [[nodiscard]] StatusCode code() const noexcept { return code_; }
    [[nodiscard]] std::string_view message() const noexcept { return message_; }

private:
    StatusCode code_;
    std::string message_;
};

/// Generic Result type carrying either a value or an error Status.
template <typename T>
class Result {
public:
    Result(T value) : data_(std::move(value)) {}
    Result(Status status) : data_(std::move(status)) {}
    Result(StatusCode code, std::string msg) : data_(Status(code, std::move(msg))) {}

    [[nodiscard]] bool ok() const noexcept { return std::holds_alternative<T>(data_); }
    [[nodiscard]] bool is_ok() const noexcept { return ok(); }
    explicit operator bool() const noexcept { return ok(); }

    [[nodiscard]] const T& value() const& { return std::get<T>(data_); }
    [[nodiscard]] T& value() & { return std::get<T>(data_); }
    [[nodiscard]] T&& value() && { return std::get<T>(std::move(data_)); }

    [[nodiscard]] const Status& status() const& {
        static const Status kOk = Status::ok();
        return ok() ? kOk : std::get<Status>(data_);
    }

    [[nodiscard]] std::string_view error_message() const noexcept {
        return ok() ? std::string_view{} : std::get<Status>(data_).message();
    }

    [[nodiscard]] StatusCode error_code() const noexcept {
        return ok() ? StatusCode::Ok : std::get<Status>(data_).code();
    }

private:
    std::variant<T, Status> data_;
};

/// Specialization of Result for void-returning operations.
template <>
class Result<void> {
public:
    Result() noexcept : status_(Status::ok()) {}
    Result(Status status) noexcept : status_(std::move(status)) {}
    Result(StatusCode code, std::string msg) : status_(code, std::move(msg)) {}

    [[nodiscard]] bool ok() const noexcept { return status_.is_ok(); }
    [[nodiscard]] bool is_ok() const noexcept { return status_.is_ok(); }
    explicit operator bool() const noexcept { return ok(); }

    [[nodiscard]] const Status& status() const noexcept { return status_; }
    [[nodiscard]] std::string_view error_message() const noexcept { return status_.message(); }
    [[nodiscard]] StatusCode error_code() const noexcept { return status_.code(); }

private:
    Status status_;
};

}  // namespace brodmabuf
