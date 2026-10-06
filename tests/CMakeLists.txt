# brodmabuf tests. Exit code 77 is a skip (a device or driver feature the
# machine genuinely lacks, with the reason printed); any other nonzero code is
# a failure. Checks are real in every configuration (tests/check.h, no assert()).
function(brodmabuf_test name)
    cmake_parse_arguments(T "" "TIMEOUT" "SOURCES" ${ARGN})
    add_executable(${name} ${T_SOURCES})
    target_link_libraries(${name} PRIVATE brodmabuf::brodmabuf)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${PROJECT_SOURCE_DIR}/src)
    brodmabuf_warnings(${name})
    if(NOT T_TIMEOUT)
        set(T_TIMEOUT 60)
    endif()
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT ${T_TIMEOUT})
endfunction()

# Portable: every platform.
brodmabuf_test(test_types SOURCES test_types.cpp)
brodmabuf_test(test_formats SOURCES test_formats.cpp)
brodmabuf_test(test_buffer SOURCES test_buffer.cpp)

if(BRODMABUF_LINUX)
    brodmabuf_test(test_fourcc_compat SOURCES test_fourcc_compat.cpp fourcc_compat_values.cpp)
    brodmabuf_test(test_gbm SOURCES test_gbm.cpp)
    brodmabuf_test(test_sync SOURCES test_sync.cpp)
    brodmabuf_test(test_vulkan SOURCES test_vulkan.cpp)
    brodmabuf_test(test_kms SOURCES test_kms.cpp)
    # test_kms may take DRM master and light an output: never alongside the
    # other device tests.
    set_tests_properties(test_gbm test_sync test_vulkan test_kms PROPERTIES RESOURCE_LOCK drm_device)
else()
    brodmabuf_test(test_unavailable SOURCES test_unavailable.cpp)
endif()
