#include "brodmabuf/types.h"

#if defined(_WIN32)
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace brodmabuf {

namespace {

void close_fd(int fd) noexcept {
#if defined(_WIN32)
    ::_close(fd);
#else
    ::close(fd);
#endif
}

}  // namespace

UniqueFd::~UniqueFd() noexcept {
    if (fd_ >= 0) {
        close_fd(fd_);
        fd_ = -1;
    }
}

void UniqueFd::reset(int new_fd) noexcept {
    if (fd_ >= 0 && fd_ != new_fd) {
        close_fd(fd_);
    }
    fd_ = new_fd;
}

UniqueFd UniqueFd::dup() const {
    if (fd_ < 0) {
        return UniqueFd();
    }
#if defined(_WIN32)
    // CRT descriptors have no close-on-exec flag to set.
    int new_fd = ::_dup(fd_);
#else
    int new_fd = ::fcntl(fd_, F_DUPFD_CLOEXEC, 0);
#endif
    if (new_fd < 0) {
        return UniqueFd();
    }
    return UniqueFd(new_fd);
}

}  // namespace brodmabuf
