#include "brodmabuf/types.h"

#include <fcntl.h>
#include <unistd.h>

namespace brodmabuf {

UniqueFd::~UniqueFd() noexcept {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

void UniqueFd::reset(int new_fd) noexcept {
    if (fd_ >= 0 && fd_ != new_fd) {
        ::close(fd_);
    }
    fd_ = new_fd;
}

UniqueFd UniqueFd::dup() const {
    if (fd_ < 0) {
        return UniqueFd();
    }
    int new_fd = ::fcntl(fd_, F_DUPFD_CLOEXEC, 0);
    if (new_fd < 0) {
        return UniqueFd();
    }
    return UniqueFd(new_fd);
}

}  // namespace brodmabuf
