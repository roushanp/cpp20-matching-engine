cmake_minimum_required(VERSION 3.20)

project(cpp20_matching_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(matching_engine STATIC
    src/matching_engine.cpp
    src/order_book.cpp
    src/order_pool.cpp
)

target_include_directories(matching_engine
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(matching_engine PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
    )
elseif(MSVC)
    target_compile_options(matching_engine PRIVATE /W4 /permissive-)
endif()

include(CTest)

if(BUILD_TESTING)
    add_executable(matching_engine_order_pool_tests
        tests/order_pool_tests.cpp
    )

    add_executable(matching_engine_order_book_tests
        tests/order_book_tests.cpp
    )

    add_executable(matching_engine_matching_engine_tests
        tests/matching_engine_tests.cpp
    )

    add_executable(matching_engine_system_tests
        tests/matching_engine_system_tests.cpp
    )

    target_link_libraries(matching_engine_order_pool_tests PRIVATE matching_engine)
    target_link_libraries(matching_engine_order_book_tests PRIVATE matching_engine)
    target_link_libraries(matching_engine_matching_engine_tests PRIVATE matching_engine)
    target_link_libraries(matching_engine_system_tests PRIVATE matching_engine)

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(matching_engine_order_pool_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
        )
    elseif(MSVC)
        target_compile_options(matching_engine_order_pool_tests PRIVATE /W4 /permissive-)
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(matching_engine_order_book_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
        )
    elseif(MSVC)
        target_compile_options(matching_engine_order_book_tests PRIVATE /W4 /permissive-)
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(matching_engine_matching_engine_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
        )
    elseif(MSVC)
        target_compile_options(matching_engine_matching_engine_tests PRIVATE /W4 /permissive-)
    endif()

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(matching_engine_system_tests PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wconversion
            -Wsign-conversion
        )
    elseif(MSVC)
        target_compile_options(matching_engine_system_tests PRIVATE /W4 /permissive-)
    endif()

    add_test(NAME matching_engine.order_pool COMMAND matching_engine_order_pool_tests)
    add_test(NAME matching_engine.order_book COMMAND matching_engine_order_book_tests)
    add_test(NAME matching_engine.engine COMMAND matching_engine_matching_engine_tests)
    add_test(NAME matching_engine.system COMMAND matching_engine_system_tests)
endif()
