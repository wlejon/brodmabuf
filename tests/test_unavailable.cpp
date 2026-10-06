// Off Linux, DMA-BUF does not exist: the allocator factories must fail with
// Unsupported and a reason, never hand out an allocator. Built on Windows and
// macOS only; on Linux test_gbm allocates for real.
#include "check.h"
#include "brodmabuf/allocator.h"

#include <cstdio>
#include <string>

using namespace brodmabuf;

namespace {

void test_platform_status() {
    Status s = platform_status();
    CHECK(!s.is_ok());
    CHECK(s.code() == StatusCode::Unsupported);
    CHECK(!s.message().empty());
    std::printf("platform_status: %s\n", std::string(s.message()).c_str());
}

void test_factories() {
    auto def = DmaBufAllocator::create_default();
    CHECK(!def.ok());
    CHECK(def.error_code() == StatusCode::Unsupported);
    CHECK(def.error_message() == platform_status().message());

    auto gbm = DmaBufAllocator::create_gbm("/dev/dri/renderD128");
    CHECK(!gbm.ok());
    CHECK(gbm.error_code() == StatusCode::Unsupported);
}

}  // namespace

int main() {
    test_platform_status();
    test_factories();
    return bstest::finish("test_unavailable");
}
