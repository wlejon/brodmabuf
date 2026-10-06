// DMA-BUF off Linux. DMA-BUF, GBM, drm_syncobj and KMS are Linux kernel
// interfaces; Windows and macOS have none of them. The format model, plane
// layout math and UniqueFd/Result types still build here; the allocator
// factories fail with the reason instead of handing out an allocator that
// could never allocate.
#include "brodmabuf/allocator.h"

namespace brodmabuf {

Status platform_status() {
#if defined(_WIN32)
    return Status::unsupported("DMA-BUF is a Linux kernel interface; Windows has none");
#elif defined(__APPLE__)
    return Status::unsupported("DMA-BUF is a Linux kernel interface; macOS has none");
#else
    return Status::unsupported("DMA-BUF is a Linux kernel interface; this platform has none");
#endif
}

Result<std::unique_ptr<DmaBufAllocator>> DmaBufAllocator::create_default() {
    return platform_status();
}

Result<std::unique_ptr<DmaBufAllocator>> DmaBufAllocator::create_gbm(const std::string&) {
    return platform_status();
}

}  // namespace brodmabuf
