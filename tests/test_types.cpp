// UniqueFd, Status and Result, on every platform. UniqueFd is exercised on a
// real pipe: data goes through, dup() yields an independent descriptor, and
// closing is observable (a write to a pipe whose read ends are all closed fails).
#include "check.h"
#include "brodmabuf/types.h"

#include <cstring>
#include <string>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#else
#include <csignal>
#include <unistd.h>
#endif

using namespace brodmabuf;

namespace {

bool make_pipe(int fds[2]) {
#if defined(_WIN32)
    return ::_pipe(fds, 256, _O_BINARY) == 0;
#else
    return ::pipe(fds) == 0;
#endif
}

long write_fd(int fd, const char* data, unsigned n) {
#if defined(_WIN32)
    return ::_write(fd, data, n);
#else
    return static_cast<long>(::write(fd, data, n));
#endif
}

long read_fd(int fd, char* data, unsigned n) {
#if defined(_WIN32)
    return ::_read(fd, data, n);
#else
    return static_cast<long>(::read(fd, data, n));
#endif
}

void test_unique_fd() {
    UniqueFd empty;
    CHECK(!empty.valid());
    CHECK_EQ(empty.get(), -1);
    CHECK(!empty);
    CHECK(!empty.dup().valid());

    int fds[2] = {-1, -1};
    REQUIRE(make_pipe(fds));
    UniqueFd rd(fds[0]);
    UniqueFd wr(fds[1]);
    CHECK(rd.valid());
    CHECK_EQ(rd.get(), fds[0]);

    CHECK_EQ(write_fd(wr.get(), "hello", 5), 5L);
    char buf[16] = {};
    CHECK_EQ(read_fd(rd.get(), buf, 5), 5L);
    CHECK_EQ(std::string(buf, 5), std::string("hello"));

    // A dup is a second descriptor for the same pipe end: data written through
    // the duplicate arrives at the reader.
    UniqueFd wr_dup = wr.dup();
    REQUIRE(wr_dup.valid());
    CHECK(wr_dup.get() != wr.get());
    CHECK_EQ(write_fd(wr_dup.get(), "dup", 3), 3L);
    std::memset(buf, 0, sizeof(buf));
    CHECK_EQ(read_fd(rd.get(), buf, 3), 3L);
    CHECK_EQ(std::string(buf, 3), std::string("dup"));

    // Move leaves the source empty and does not close the descriptor.
    UniqueFd moved = std::move(wr_dup);
    CHECK(moved.valid());
    CHECK(!wr_dup.valid());
    CHECK_EQ(write_fd(moved.get(), "m", 1), 1L);
    CHECK_EQ(read_fd(rd.get(), buf, 1), 1L);

    moved.reset();
    CHECK(!moved.valid());
    CHECK_EQ(moved.get(), -1);

    // release() hands the descriptor out unclosed; resetting the reader closes
    // it, after which writing to the pipe fails (EPIPE).
    int raw = rd.release();
    CHECK(!rd.valid());
    CHECK(raw >= 0);
    rd.reset(raw);
    rd.reset();
#if !defined(_WIN32)
    std::signal(SIGPIPE, SIG_IGN);
#endif
    CHECK(write_fd(wr.get(), "x", 1) < 0);
}

void test_status_and_result() {
    Status ok = Status::ok();
    CHECK(ok.is_ok());
    CHECK(static_cast<bool>(ok));
    CHECK(ok.code() == StatusCode::Ok);

    Status err = Status::invalid_argument("invalid parameter");
    CHECK(!err.is_ok());
    CHECK(err.code() == StatusCode::InvalidArgument);
    CHECK(err.message() == "invalid parameter");
    CHECK(Status::unsupported("u").code() == StatusCode::Unsupported);
    CHECK(Status::timeout("t").code() == StatusCode::Timeout);

    Result<int> val(42);
    CHECK(val.ok());
    CHECK_EQ(val.value(), 42);
    CHECK_EQ(*val, 42);
    CHECK(val.error_code() == StatusCode::Ok);
    CHECK(val.error_message().empty());
    CHECK(val.status().is_ok());

    Result<int> bad(Status::not_found("not found"));
    CHECK(!bad.ok());
    CHECK(bad.error_code() == StatusCode::NotFound);
    CHECK(bad.error_message() == "not found");

    Result<std::string> str(std::string("brodmabuf"));
    CHECK(str.ok());
    CHECK_EQ(str->size(), size_t(9));

    Result<void> v;
    CHECK(v.ok());
    Result<void> verr(StatusCode::DeviceError, "gone");
    CHECK(!verr.ok());
    CHECK(verr.error_code() == StatusCode::DeviceError);
    CHECK(verr.error_message() == "gone");
}

}  // namespace

int main() {
    test_unique_fd();
    test_status_and_result();
    return bstest::finish("test_types");
}
