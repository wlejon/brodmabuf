#include "brodmabuf/types.h"

#include <fcntl.h>
#include <unistd.h>
#include <cassert>
#include <iostream>
#include <string>

using namespace brodmabuf;

void test_unique_fd() {
    std::cout << "[test_types] Running test_unique_fd..." << std::endl;

    UniqueFd empty;
    assert(!empty.valid());
    assert(empty.get() == -1);
    assert(!empty);

    int pipe_fds[2] = {-1, -1};
    assert(::pipe(pipe_fds) == 0);

    UniqueFd read_fd(pipe_fds[0]);
    UniqueFd write_fd(pipe_fds[1]);

    assert(read_fd.valid());
    assert(write_fd.valid());
    assert(read_fd.get() == pipe_fds[0]);
    assert(write_fd.get() == pipe_fds[1]);

    // Test write and read through UniqueFds
    const char msg[] = "hello";
    ssize_t written = ::write(write_fd.get(), msg, 5);
    assert(written == 5);

    char buf[16] = {};
    ssize_t read_bytes = ::read(read_fd.get(), buf, 5);
    assert(read_bytes == 5);
    assert(std::string(buf, 5) == "hello");

    // Test dup
    UniqueFd read_dup = read_fd.dup();
    assert(read_dup.valid());
    assert(read_dup.get() != read_fd.get());

    // Test move
    UniqueFd moved = std::move(read_dup);
    assert(moved.valid());
    assert(!read_dup.valid());

    // Test reset
    moved.reset();
    assert(!moved.valid());
    assert(moved.get() == -1);

    // Test release
    int raw = read_fd.release();
    assert(!read_fd.valid());
    assert(raw >= 0);
    ::close(raw);

    std::cout << "[test_types] test_unique_fd passed!" << std::endl;
}

void test_status_and_result() {
    std::cout << "[test_types] Running test_status_and_result..." << std::endl;

    Status ok = Status::ok();
    assert(ok.is_ok());
    assert(ok.code() == StatusCode::Ok);

    Status err = Status::invalid_argument("invalid parameter");
    assert(!err.is_ok());
    assert(err.code() == StatusCode::InvalidArgument);
    assert(err.message() == "invalid parameter");

    Result<int> res_val(42);
    assert(res_val.ok());
    assert(res_val.value() == 42);
    assert(res_val.error_code() == StatusCode::Ok);

    Result<int> res_err(Status::not_found("not found"));
    assert(!res_err.ok());
    assert(res_err.error_code() == StatusCode::NotFound);
    assert(res_err.error_message() == "not found");

    Result<std::string> str_res(std::string("brodmabuf"));
    assert(str_res.ok());
    assert(str_res.value() == "brodmabuf");

    std::cout << "[test_types] test_status_and_result passed!" << std::endl;
}

int main() {
    test_unique_fd();
    test_status_and_result();
    std::cout << "[test_types] All tests passed!" << std::endl;
    return 0;
}
