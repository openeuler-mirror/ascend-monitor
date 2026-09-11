cmake_minimum_required(VERSION 3.16)

project(ascend-monitor VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Charts Concurrent Test)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Charts Concurrent Test)

set(NPU_CONTROL_SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/NpuInfoProvider.cpp
    resources/ascend-monitor.qrc
)

set(NPU_CONTROL_HEADERS
    include/MainWindow.h
    include/NpuInfoProvider.h
    include/NpuTypes.h
)

if(QT_VERSION_MAJOR GREATER_EQUAL 6)
    qt_add_executable(ascend-monitor
        ${NPU_CONTROL_SOURCES}
        ${NPU_CONTROL_HEADERS}
    )
else()
    add_executable(ascend-monitor
        ${NPU_CONTROL_SOURCES}
        ${NPU_CONTROL_HEADERS}
    )
endif()

target_include_directories(ascend-monitor
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(ascend-monitor
    PRIVATE
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Charts
        Qt${QT_VERSION_MAJOR}::Concurrent
)

target_compile_definitions(ascend-monitor
    PRIVATE
        $<$<CONFIG:Debug>:ASCEND_MONITOR_ENABLE_REMOTE>
)

enable_testing()

file(GLOB NPU_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)

if(NPU_TEST_SOURCES)
    if(QT_VERSION_MAJOR GREATER_EQUAL 6)
        qt_add_executable(npu-provider-tests
            ${NPU_TEST_SOURCES}
            src/NpuInfoProvider.cpp
            include/NpuInfoProvider.h
            include/NpuTypes.h
        )
    else()
        add_executable(npu-provider-tests
            ${NPU_TEST_SOURCES}
            src/NpuInfoProvider.cpp
            include/NpuInfoProvider.h
            include/NpuTypes.h
        )
    endif()

    target_include_directories(npu-provider-tests
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(npu-provider-tests
        PRIVATE
            Qt${QT_VERSION_MAJOR}::Core
            Qt${QT_VERSION_MAJOR}::Test
    )

    if(WIN32)
        target_compile_definitions(npu-provider-tests PRIVATE UNICODE _UNICODE)
    endif()

    add_test(NAME npu-provider-tests COMMAND npu-provider-tests)
    set_tests_properties(npu-provider-tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

install(TARGETS ascend-monitor
    RUNTIME DESTINATION bin
)

set(CPACK_PACKAGE_NAME "ascend-monitor")
set(CPACK_PACKAGE_VENDOR "ascend-monitor")
set(CPACK_PACKAGE_CONTACT "ascend-monitor")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Ascend NPU monitor and management GUI")
set(CPACK_GENERATOR "RPM")

set(CPACK_RPM_PACKAGE_LICENSE "Proprietary")
set(CPACK_RPM_PACKAGE_GROUP "Applications/System")
set(CPACK_RPM_PACKAGE_RELEASE 1)
set(CPACK_RPM_PACKAGE_ARCHITECTURE "x86_64")
set(CPACK_RPM_PACKAGE_AUTOREQ YES)

include(CPack)
