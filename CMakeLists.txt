cmake_minimum_required(VERSION 3.25)

project(mklib VERSION 0.1.0 LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(APPLE)
    set(CMAKE_OBJCXX_STANDARD 17)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")
endif()
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

option(MKLIB_BUILD_SHARED "Build mklib as a shared library" OFF)
option(MKLIB_BUILD_DEMO "Build the keyboard demo" ON)
option(MKLIB_BUILD_MOUSE_DEMO "Build the multi-mouse demo" ON)
option(MKLIB_BUILD_FLAPPYBIRD_DEMO "Build the multi-keyboard Flappy Bird demo" ON)
option(MKLIB_BUILD_TESTS "Build the mklib tests" ON)
set(MKLIB_CODESIGN_IDENTITY "" CACHE STRING "Code signing identity for the macOS demo")

if(APPLE)
    set(MKLIB_BACKEND_SOURCES src/platform/macos/macos_backend.cpp)
elseif(WIN32)
    set(MKLIB_BACKEND_SOURCES
        src/platform/windows/raw_input_backend.cpp
        src/platform/windows/raw_input_normalizer.cpp
    )
else()
    set(MKLIB_BACKEND_SOURCES src/platform/unsupported/unsupported_backend.cpp)
endif()

if(MKLIB_BUILD_SHARED)
    add_library(mklib SHARED ${MKLIB_BACKEND_SOURCES})
    target_compile_definitions(mklib PUBLIC MKLIB_BUILD_SHARED)
else()
    add_library(mklib STATIC ${MKLIB_BACKEND_SOURCES})
endif()

add_library(mklib::mklib ALIAS mklib)

target_include_directories(mklib
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_options(mklib PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra;-Wpedantic>
)
if(APPLE)
    target_link_libraries(mklib PRIVATE
        "-framework IOKit"
        "-framework CoreFoundation"
    )
elseif(WIN32)
    target_link_libraries(mklib PRIVATE
        user32
        setupapi
    )
endif()

if(APPLE AND MKLIB_BUILD_DEMO)
    add_executable(mklib_demo MACOSX_BUNDLE demo/macos/mklib_demo/main.mm)
    target_include_directories(mklib_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_options(mklib_demo PRIVATE -Wall -Wextra -Wpedantic -fobjc-arc)
    target_link_libraries(mklib_demo PRIVATE mklib::mklib "-framework Cocoa")
    set_target_properties(mklib_demo PROPERTIES MACOSX_BUNDLE_GUI_IDENTIFIER "com.mklib.demo" MACOSX_BUNDLE_BUNDLE_NAME "mklib Demo" MACOSX_BUNDLE_BUNDLE_VERSION "0.1.0" MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0" MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/demo/macos/mklib_demo/Info.plist")
elseif(WIN32 AND MKLIB_BUILD_DEMO)
    add_executable(mklib_demo WIN32 demo/windows/mklib_demo.cpp)
    target_include_directories(mklib_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/demo/windows)
    target_compile_definitions(mklib_demo PRIVATE UNICODE _UNICODE)
    target_compile_options(mklib_demo PRIVATE /utf-8)
    target_link_libraries(mklib_demo PRIVATE mklib::mklib)
endif()

if(APPLE AND MKLIB_BUILD_FLAPPYBIRD_DEMO)
    add_executable(mkflappybird MACOSX_BUNDLE
        demo/macos/mkflappybird/main.mm
        demo/macos/mkflappybird/assets/sky_background.png
        demo/macos/mkflappybird/assets/bird_up.png
        demo/macos/mkflappybird/assets/bird_down.png
        demo/macos/mkflappybird/assets/bullet.png
        demo/macos/mkflappybird/assets/gift.png
    )
    set_source_files_properties(
        demo/macos/mkflappybird/assets/sky_background.png
        demo/macos/mkflappybird/assets/bird_up.png
        demo/macos/mkflappybird/assets/bird_down.png
        demo/macos/mkflappybird/assets/bullet.png
        demo/macos/mkflappybird/assets/gift.png
        PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_include_directories(mkflappybird PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_options(mkflappybird PRIVATE -Wall -Wextra -Wpedantic -fobjc-arc)
    target_link_libraries(mkflappybird PRIVATE
        mklib::mklib
        "-framework Cocoa"
    )
    set_target_properties(mkflappybird PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.mklib.flappybird"
        MACOSX_BUNDLE_BUNDLE_NAME "mkflappybird"
        MACOSX_BUNDLE_BUNDLE_VERSION "0.1.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/demo/macos/mkflappybird/Info.plist"
    )
    if(MKLIB_CODESIGN_IDENTITY)
        add_custom_command(TARGET mkflappybird POST_BUILD
            COMMAND codesign --force --deep --sign "${MKLIB_CODESIGN_IDENTITY}"
                    "$<TARGET_BUNDLE_DIR:mkflappybird>"
        )
    endif()
elseif(WIN32 AND MKLIB_BUILD_FLAPPYBIRD_DEMO)
    add_executable(mkflappybird WIN32 demo/windows/mkflappybird.cpp)
    target_include_directories(mkflappybird PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/demo/windows)
    target_compile_definitions(mkflappybird PRIVATE UNICODE _UNICODE)
    target_compile_options(mkflappybird PRIVATE /utf-8)
    target_link_libraries(mkflappybird PRIVATE mklib::mklib)
    add_custom_command(TARGET mkflappybird POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:mkflappybird>/assets"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${CMAKE_CURRENT_SOURCE_DIR}/demo/macos/mkflappybird/assets"
                "$<TARGET_FILE_DIR:mkflappybird>/assets"
    )
endif()

if(APPLE AND MKLIB_BUILD_MOUSE_DEMO)
    add_executable(mklib_mouse_demo MACOSX_BUNDLE
        demo/macos/mklib_mouse_demo/mouse_main.mm
    )
    target_include_directories(mklib_mouse_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_options(mklib_mouse_demo PRIVATE -Wall -Wextra -Wpedantic -fobjc-arc)
    target_link_libraries(mklib_mouse_demo PRIVATE
        mklib::mklib
        "-framework Cocoa"
        "-framework CoreGraphics"
    )
    set_target_properties(mklib_mouse_demo PROPERTIES
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.mklib.mouse-demo"
        MACOSX_BUNDLE_BUNDLE_NAME "mklib Mouse Demo"
        MACOSX_BUNDLE_BUNDLE_VERSION "0.1.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/demo/macos/mklib_mouse_demo/MouseInfo.plist"
    )
    if(MKLIB_CODESIGN_IDENTITY)
        add_custom_command(TARGET mklib_mouse_demo POST_BUILD
            COMMAND codesign --force --deep --sign "${MKLIB_CODESIGN_IDENTITY}"
                    "$<TARGET_BUNDLE_DIR:mklib_mouse_demo>"
        )
    endif()
elseif(WIN32 AND MKLIB_BUILD_MOUSE_DEMO)
    add_executable(mklib_mouse_demo WIN32 demo/windows/mklib_mouse_demo.cpp)
    target_include_directories(mklib_mouse_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR}/demo/windows)
    target_compile_definitions(mklib_mouse_demo PRIVATE UNICODE _UNICODE)
    target_compile_options(mklib_mouse_demo PRIVATE /utf-8)
    target_link_libraries(mklib_mouse_demo PRIVATE mklib::mklib)
endif()

if(MKLIB_BUILD_TESTS)
    enable_testing()
    add_executable(mklib_tests
        tests/test_api.cpp
    )
    target_include_directories(mklib_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_options(mklib_tests PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra;-Wpedantic>
    )
    target_link_libraries(mklib_tests PRIVATE mklib::mklib)
    add_test(NAME mklib_api COMMAND mklib_tests)

    add_executable(mklib_windows_normalization_tests
        tests/test_windows_normalizer.cpp
        src/platform/windows/raw_input_normalizer.cpp
    )
    target_include_directories(mklib_windows_normalization_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/windows
    )
    target_compile_options(mklib_windows_normalization_tests PRIVATE
        $<$<CXX_COMPILER_ID:MSVC>:/W4>
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall;-Wextra;-Wpedantic>
    )
    add_test(NAME mklib_windows_normalization COMMAND mklib_windows_normalization_tests)
    if(APPLE)
        add_executable(mklib_hid_benchmark tests/hid_benchmark.cpp)
        target_include_directories(mklib_hid_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
        target_compile_options(mklib_hid_benchmark PRIVATE -Wall -Wextra -Wpedantic)
        target_link_libraries(mklib_hid_benchmark PRIVATE mklib::mklib)
    endif()
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
configure_package_config_file(
    cmake/mklibConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/mklibConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mklib
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/mklibConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

install(TARGETS mklib
    EXPORT mklibTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT mklibTargets
    FILE mklibTargets.cmake
    NAMESPACE mklib::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mklib
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/mklibConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/mklibConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/mklib
)
