function(add_brodmabuf_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE brodmabuf::brodmabuf)
    target_include_directories(${name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
    )
    brodmabuf_warnings(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
endfunction()

add_brodmabuf_test(test_types test_types.cpp)
add_brodmabuf_test(test_formats test_formats.cpp)
add_brodmabuf_test(test_buffer test_buffer.cpp)
add_brodmabuf_test(test_gbm test_gbm.cpp)
add_brodmabuf_test(test_sync test_sync.cpp)
add_brodmabuf_test(test_vulkan test_vulkan.cpp)
add_brodmabuf_test(test_kms test_kms.cpp)
