cmake_minimum_required(VERSION 3.16)

project(ascend-monitor VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include(CTest)
option(ASCEND_MONITOR_BUILD_GUI "Build the existing Qt GUI" ON)
option(ASCEND_MONITOR_BUILD_TUI "Build the independent ascend-monitor-tui terminal monitor" ON)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(ASCEND_MONITOR_TUI_DCMI_DEFAULT ON)
else()
    set(ASCEND_MONITOR_TUI_DCMI_DEFAULT OFF)
endif()
option(ASCEND_MONITOR_TUI_DCMI "Compile against the installed Ascend DCMI headers" ${ASCEND_MONITOR_TUI_DCMI_DEFAULT})

if(ASCEND_MONITOR_BUILD_GUI)
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

if(BUILD_TESTING)
file(GLOB NPU_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp)

foreach(NPU_TEST_SOURCE ${NPU_TEST_SOURCES})
    get_filename_component(NPU_TEST_NAME ${NPU_TEST_SOURCE} NAME_WE)

    if(QT_VERSION_MAJOR GREATER_EQUAL 6)
        qt_add_executable(${NPU_TEST_NAME}
            ${NPU_TEST_SOURCE}
            src/NpuInfoProvider.cpp
            include/NpuInfoProvider.h
            include/NpuTypes.h
        )
    else()
        add_executable(${NPU_TEST_NAME}
            ${NPU_TEST_SOURCE}
            src/NpuInfoProvider.cpp
            include/NpuInfoProvider.h
            include/NpuTypes.h
        )
    endif()

    target_include_directories(${NPU_TEST_NAME}
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/include
    )

    target_link_libraries(${NPU_TEST_NAME}
        PRIVATE
            Qt${QT_VERSION_MAJOR}::Core
            Qt${QT_VERSION_MAJOR}::Test
    )

    if(WIN32)
        target_compile_definitions(${NPU_TEST_NAME} PRIVATE UNICODE _UNICODE)
    endif()

    add_test(NAME ${NPU_TEST_NAME} COMMAND ${NPU_TEST_NAME})
    set_tests_properties(${NPU_TEST_NAME} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()
endif()

install(TARGETS ascend-monitor
    RUNTIME DESTINATION bin COMPONENT gui
)

endif()

set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC OFF)

if(ASCEND_MONITOR_BUILD_TUI)
    set(CURSES_NEED_WIDE TRUE)
    find_package(Curses REQUIRED)
    find_package(Threads REQUIRED)
    add_library(ascend-monitor-tui-core STATIC src/top/Model.cpp src/top/Proc.cpp src/top/Dcmi.cpp src/top/Graph.cpp src/top/ProcessTable.cpp src/top/ProcessView.cpp)
    target_include_directories(ascend-monitor-tui-core PUBLIC include)
    target_link_libraries(ascend-monitor-tui-core PRIVATE ${CMAKE_DL_LIBS})
    if(ASCEND_MONITOR_TUI_DCMI)
        find_path(DCMI_INCLUDE_DIR dcmi_interface_api.h
            HINTS /usr/local/Ascend/driver/include)
        if(NOT DCMI_INCLUDE_DIR)
            message(FATAL_ERROR "DCMI headers required: set DCMI_INCLUDE_DIR, or ASCEND_MONITOR_TUI_DCMI=OFF for host-only development")
        endif()
        target_include_directories(ascend-monitor-tui-core SYSTEM PRIVATE ${DCMI_INCLUDE_DIR})
        target_compile_definitions(ascend-monitor-tui-core PRIVATE ASCEND_MONITOR_TUI_HAVE_DCMI=1)
    endif()
    add_executable(ascend-monitor-tui src/top/main.cpp src/top/Tui.cpp)
    target_include_directories(ascend-monitor-tui PRIVATE ${CURSES_INCLUDE_DIRS})
    target_link_libraries(ascend-monitor-tui PRIVATE ascend-monitor-tui-core ${CURSES_LIBRARIES} Threads::Threads)
    target_compile_definitions(ascend-monitor-tui PRIVATE ASCEND_MONITOR_TUI_VERSION="${PROJECT_VERSION}")
    install(TARGETS ascend-monitor-tui RUNTIME DESTINATION bin COMPONENT tui)
    if(BUILD_TESTING)
        add_executable(test_top_core tests/top/test_core.cpp)
        target_link_libraries(test_top_core PRIVATE ascend-monitor-tui-core)
        add_test(NAME test_top_core COMMAND test_top_core)
        add_executable(test_history_graph tests/top/test_history_graph.cpp)
        target_link_libraries(test_history_graph PRIVATE ascend-monitor-tui-core)
        add_test(NAME test_history_graph COMMAND test_history_graph)
        add_executable(test_process_sort tests/top/test_process_sort.cpp)
        target_link_libraries(test_process_sort PRIVATE ascend-monitor-tui-core)
        add_test(NAME test_process_sort COMMAND test_process_sort)
        add_executable(test_process_view tests/top/test_process_view.cpp)
        target_link_libraries(test_process_view PRIVATE ascend-monitor-tui-core)
        add_test(NAME test_process_view COMMAND test_process_view)
        # Deterministic PTY fixture: supplies telemetry in the test binary only.
        add_executable(test_process_view_tui tests/top/process_view_fixture.cpp src/top/Tui.cpp)
        target_include_directories(test_process_view_tui PRIVATE ${CURSES_INCLUDE_DIRS})
        target_link_libraries(test_process_view_tui PRIVATE ascend-monitor-tui-core ${CURSES_LIBRARIES} Threads::Threads)
        if(ASCEND_MONITOR_TUI_DCMI AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
            add_executable(test_dcmi_share tests/top/test_dcmi_share.cpp)
            target_include_directories(test_dcmi_share SYSTEM PRIVATE ${DCMI_INCLUDE_DIR})
            target_link_libraries(test_dcmi_share PRIVATE ascend-monitor-tui-core Threads::Threads)
            add_test(NAME test_dcmi_share COMMAND test_dcmi_share)
        endif()
    endif()
endif()

if(NOT ASCEND_MONITOR_BUILD_GUI AND NOT ASCEND_MONITOR_BUILD_TUI)
    message(FATAL_ERROR "Select at least one of ASCEND_MONITOR_BUILD_GUI or ASCEND_MONITOR_BUILD_TUI")
endif()
set(CPACK_PACKAGE_NAME "ascend-monitor")
set(CPACK_PACKAGE_VENDOR "ascend-monitor")
set(CPACK_PACKAGE_CONTACT "ascend-monitor")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Ascend NPU monitor and management GUI")
set(CPACK_GENERATOR "RPM")

set(CPACK_RPM_PACKAGE_LICENSE "Proprietary")
set(CPACK_RPM_PACKAGE_GROUP "Applications/System")
set(CPACK_RPM_PACKAGE_RELEASE 9)
set(CPACK_RPM_PACKAGE_ARCHITECTURE "${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_RPM_PACKAGE_AUTOREQ YES)

set(CPACK_RPM_COMPONENT_INSTALL ON)
set(CPACK_COMPONENTS_GROUPING IGNORE)
set(CPACK_COMPONENTS_ALL "")
if(ASCEND_MONITOR_BUILD_GUI)
    list(APPEND CPACK_COMPONENTS_ALL gui)
endif()
if(ASCEND_MONITOR_BUILD_TUI)
    list(APPEND CPACK_COMPONENTS_ALL tui)
endif()
set(CPACK_RPM_GUI_PACKAGE_NAME "ascend-monitor")
set(CPACK_RPM_TUI_PACKAGE_NAME "ascend-monitor-tui")
set(CPACK_RPM_TUI_PACKAGE_SUMMARY "Local Ascend NPU terminal monitor")
set(CPACK_RPM_FILE_NAME RPM-DEFAULT)
include(CPack)
