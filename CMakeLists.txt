cmake_minimum_required(VERSION 3.25)

if(WIN32 AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VCPKG_ROOT} AND
       EXISTS "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
        set(_vcpkg_root "$ENV{VCPKG_ROOT}")
    else()
        find_program(_vcpkg_executable NAMES vcpkg vcpkg.exe)
        if(_vcpkg_executable)
            get_filename_component(_vcpkg_root "${_vcpkg_executable}" DIRECTORY)
        endif()
    endif()

    if(_vcpkg_root AND EXISTS "${_vcpkg_root}/scripts/buildsystems/vcpkg.cmake")
        set(CMAKE_TOOLCHAIN_FILE
            "${_vcpkg_root}/scripts/buildsystems/vcpkg.cmake"
            CACHE FILEPATH "vcpkg toolchain file")
    else()
        message(FATAL_ERROR
            "vcpkg was not found. Add vcpkg.exe to PATH or define VCPKG_ROOT.")
    endif()
endif()

project(YoloNexusCpp LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR "YoloNexus currently supports Windows only.")
endif()

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(OpenCV CONFIG REQUIRED COMPONENTS core imgproc dnn videoio)
find_path(ONNXRUNTIME_INCLUDE_DIR onnxruntime_cxx_api.h REQUIRED)

file(GLOB_RECURSE YOLO_NEXUS_LIBRARY_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
list(REMOVE_ITEM YOLO_NEXUS_LIBRARY_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/main.cpp"
)

add_library(yolo_nexus_lib STATIC ${YOLO_NEXUS_LIBRARY_SOURCES})
target_include_directories(yolo_nexus_lib PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app"
    "${ONNXRUNTIME_INCLUDE_DIR}"
)
target_compile_definitions(yolo_nexus_lib PUBLIC
    UNICODE
    _UNICODE
    ORT_API_MANUAL_INIT
    _CRT_SECURE_NO_WARNINGS
)
target_link_libraries(yolo_nexus_lib PUBLIC
    ${OpenCV_LIBS}
    d3d11
    dxgi
    d3dcompiler
    gdi32
    user32
    ole32
    strmiids
    winmm
    pdh
    psapi
)

if(MSVC)
    target_compile_options(yolo_nexus_lib PRIVATE
        /W4 /permissive- /utf-8 /wd4005 /wd4864
    )
endif()

add_executable(YoloNexusCpp "src/app/main.cpp")
target_link_libraries(YoloNexusCpp PRIVATE yolo_nexus_lib)

set(DIRECTML_RUNTIME_DIR
    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/$<$<CONFIG:Debug>:debug/>bin"
)

function(deploy_directml_runtime target)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E make_directory
            "$<TARGET_FILE_DIR:${target}>/runtimes/directml"
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${DIRECTML_RUNTIME_DIR}/onnxruntime.dll"
            "${DIRECTML_RUNTIME_DIR}/onnxruntime_providers_shared.dll"
            "${DIRECTML_RUNTIME_DIR}/DirectML.dll"
            "$<TARGET_FILE_DIR:${target}>/runtimes/directml"
        VERBATIM
    )
endfunction()

deploy_directml_runtime(YoloNexusCpp)

foreach(asset IN ITEMS config.txt best4k.onnx)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${asset}")
        add_custom_command(TARGET YoloNexusCpp POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_CURRENT_SOURCE_DIR}/${asset}"
                "$<TARGET_FILE_DIR:YoloNexusCpp>/${asset}"
            VERBATIM
        )
    endif()
endforeach()

if(BUILD_TESTING)
    function(add_yolo_test name source)
        add_executable(${name} "${source}")
        target_link_libraries(${name} PRIVATE yolo_nexus_lib)
        add_test(NAME ${name} COMMAND $<TARGET_FILE:${name}>)
    endfunction()

    add_yolo_test(config_tests tests/config_tests.cpp)
    add_yolo_test(fps_statistics_tests tests/fps_statistics_tests.cpp)
    add_yolo_test(input_preprocessing_tests tests/input_preprocessing_tests.cpp)
    add_yolo_test(source_catalog_tests tests/source_catalog_tests.cpp)

    add_executable(onnxruntime_loader_tests tests/onnxruntime_loader_tests.cpp)
    target_link_libraries(onnxruntime_loader_tests PRIVATE yolo_nexus_lib)
    deploy_directml_runtime(onnxruntime_loader_tests)
    add_test(
        NAME onnxruntime_loader_tests
        COMMAND $<TARGET_FILE:onnxruntime_loader_tests> directml
    )
endif()
