cmake_minimum_required(VERSION 3.25)

project(mklib VERSION 0.1.0 LANGUAGES C CXX OBJCXX)

if(NOT APPLE)
    message(FATAL_ERROR "mklib 0.1.0 currently supports the macOS backend only")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OBJCXX_STANDARD 17)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")

option(MKLIB_BUILD_DEMO "Build the macOS Cocoa demo" ON)
option(MKLIB_BUILD_TESTS "Build the mklib tests" ON)

add_library(mklib STATIC
    src/mklib.cpp
)

add_library(mklib::mklib ALIAS mklib)

target_include_directories(mklib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_options(mklib PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(mklib PRIVATE
    "-framework IOKit"
    "-framework CoreFoundation"
)

if(MKLIB_BUILD_DEMO)
    add_executable(mklib_demo MACOSX_BUNDLE
        demo/main.mm
    )
    target_include_directories(mklib_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_options(mklib_demo PRIVATE -Wall -Wextra -Wpedantic -fobjc-arc)
    target_link_libraries(mklib_demo PRIVATE
        mklib::mklib
        "-framework Cocoa"
    )
    set_target_properties(mklib_demo PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.mklib.demo"
        MACOSX_BUNDLE_BUNDLE_NAME "mklib Demo"
        MACOSX_BUNDLE_BUNDLE_VERSION "0.1.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/demo/Info.plist"
    )
endif()

if(MKLIB_BUILD_TESTS)
    enable_testing()
    add_executable(mklib_tests
        tests/test_api.cpp
    )
    target_include_directories(mklib_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_options(mklib_tests PRIVATE -Wall -Wextra -Wpedantic)
    target_link_libraries(mklib_tests PRIVATE mklib::mklib)
    add_test(NAME mklib_api COMMAND mklib_tests)
endif()

include(GNUInstallDirs)
install(TARGETS mklib
    EXPORT mklibTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT mklibTargets
    FILE mklibTargets.cmake
    NAMESPACE mklib::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mklib
)
