#include "brodmabuf/buffer.h"

#include <sys/mman.h>
#include <unistd.h>
#include <cassert>
#include <iostream>

using namespace brodmabuf;

int create_test_memfd(const char* name, size_t size) {
    int fd = ::memfd_create(name, MFD_CLOEXEC);
    if (fd >= 0 && size > 0) {
        if (::ftruncate(fd, static_cast<off_t>(size)) != 0) {
            ::close(fd);
            return -1;
        }
    }
    return fd;
}

void test_dmabuf_plane() {
    std::cout << "[test_buffer] Running test_dmabuf_plane..." << std::endl;

    int raw_fd = create_test_memfd("plane_test", 4096);
    assert(raw_fd >= 0);

    DmaBufPlane plane(UniqueFd(raw_fd), 1920 * 4, 0, 4096);
    assert(plane.fd.valid());
    assert(plane.stride == 1920 * 4);
    assert(plane.offset == 0);
    assert(plane.size == 4096);

    DmaBufPlane dup_plane = plane.dup();
    assert(dup_plane.fd.valid());
    assert(dup_plane.fd.get() != plane.fd.get());
    assert(dup_plane.stride == plane.stride);
    assert(dup_plane.offset == plane.offset);
    assert(dup_plane.size == plane.size);

    std::cout << "[test_buffer] test_dmabuf_plane passed!" << std::endl;
}

void test_dmabuf_attributes() {
    std::cout << "[test_buffer] Running test_dmabuf_attributes..." << std::endl;

    DmaBufAttributes attrs;
    assert(!attrs.is_valid());

    attrs.width = 1920;
    attrs.height = 1080;
    attrs.drm_format = DRM_FORMAT_XRGB8888;
    attrs.modifier = DRM_FORMAT_MOD_LINEAR;

    int fd1 = create_test_memfd("buf1", 1920 * 1080 * 4);
    assert(fd1 >= 0);
    attrs.planes.emplace_back(UniqueFd(fd1), 1920 * 4, 0, 1920 * 1080 * 4);

    assert(attrs.is_valid());
    assert(!attrs.is_disjoint());
    assert(attrs.total_size() == 1920 * 1080 * 4);

    // Test dup
    DmaBufAttributes copied = attrs.dup();
    assert(copied.is_valid());
    assert(copied.width == attrs.width);
    assert(copied.height == attrs.height);
    assert(copied.drm_format == attrs.drm_format);
    assert(copied.modifier == attrs.modifier);
    assert(copied.planes.size() == attrs.planes.size());
    assert(copied.planes[0].fd.get() != attrs.planes[0].fd.get());

    // Test multi-plane disjoint
    DmaBufAttributes nv12_attrs;
    nv12_attrs.width = 1920;
    nv12_attrs.height = 1080;
    nv12_attrs.drm_format = DRM_FORMAT_NV12;
    nv12_attrs.modifier = DRM_FORMAT_MOD_LINEAR;

    int fd_y = create_test_memfd("y_plane", 1920 * 1080);
    int fd_uv = create_test_memfd("uv_plane", 1920 * 540);
    assert(fd_y >= 0 && fd_uv >= 0);

    nv12_attrs.planes.emplace_back(UniqueFd(fd_y), 1920, 0, 1920 * 1080);
    nv12_attrs.planes.emplace_back(UniqueFd(fd_uv), 1920, 0, 1920 * 540);

    assert(nv12_attrs.is_valid());
    assert(nv12_attrs.is_disjoint());
    assert(nv12_attrs.total_size() == (1920 * 1080 + 1920 * 540));

    std::cout << "[test_buffer] test_dmabuf_attributes passed!" << std::endl;
}

void test_calculations() {
    std::cout << "[test_buffer] Running test_calculations..." << std::endl;

    assert(calculate_min_stride(DRM_FORMAT_XRGB8888, 1920, 0) == 1920 * 4);
    assert(calculate_min_stride(DRM_FORMAT_RGB565, 1920, 0) == 1920 * 2);
    assert(calculate_min_stride(DRM_FORMAT_NV12, 1920, 0) == 1920);
    assert(calculate_min_stride(DRM_FORMAT_NV12, 1920, 1) == 1920);

    assert(calculate_min_plane_size(DRM_FORMAT_XRGB8888, 1920, 1080, 0, 1920 * 4) == 1920 * 1080 * 4);
    assert(calculate_min_plane_size(DRM_FORMAT_NV12, 1920, 1080, 0, 1920) == 1920 * 1080);
    assert(calculate_min_plane_size(DRM_FORMAT_NV12, 1920, 1080, 1, 1920) == 1920 * 540);

    std::cout << "[test_buffer] test_calculations passed!" << std::endl;
}

int main() {
    test_dmabuf_plane();
    test_dmabuf_attributes();
    test_calculations();
    std::cout << "[test_buffer] All tests passed!" << std::endl;
    return 0;
}
