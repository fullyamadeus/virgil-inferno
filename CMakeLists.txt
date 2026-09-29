cmake_minimum_required(VERSION 3.24)
project(VirgilDesktop VERSION 0.1.0 LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "Use an out-of-source build: cmake --preset debug")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(CTest)
include(GNUInstallDirs)
find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Widgets Sql)

option(VIRGIL_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
add_library(virgil_options INTERFACE)
target_compile_options(virgil_options INTERFACE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow>
    $<$<CXX_COMPILER_ID:MSVC>:/W4;/utf-8>)
if(VIRGIL_SANITIZERS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(virgil_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(virgil_options INTERFACE -fsanitize=address,undefined)
endif()

add_library(virgil_core STATIC
    assets/catalog.qrc
    src/services/calendar.cpp
    src/services/librarycalendar.cpp
    src/services/catalog.cpp
    src/services/paths.cpp
    src/services/library.cpp
    src/services/librarycontroller.cpp
    src/storage/database.cpp)
target_include_directories(virgil_core PUBLIC src)
target_link_libraries(virgil_core PUBLIC Qt6::Core Qt6::Sql PRIVATE virgil_options)

add_library(virgil_ui STATIC src/ui/mainwindow.cpp src/ui/resources.cpp src/ui/calendarpage.cpp)
target_link_libraries(virgil_ui PUBLIC virgil_core Qt6::Widgets PRIVATE virgil_options)
add_executable(virgil-desktop src/main.cpp assets/application.qrc)
target_link_libraries(virgil-desktop PRIVATE virgil_ui virgil_options)
install(TARGETS virgil-desktop RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    configure_file(packaging/virgil-desktop.desktop.in virgil-desktop.desktop @ONLY)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/virgil-desktop.desktop"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES assets/virgil-mark.svg
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
        RENAME virgil-desktop.svg)
    install(FILES assets/virgil-logo.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/pixmaps RENAME virgil-desktop.png)
endif()

if(BUILD_TESTING)
    find_package(Qt6 6.8 REQUIRED COMPONENTS Test)
    add_executable(library_test tests/library_test.cpp)
    target_link_libraries(library_test PRIVATE virgil_core Qt6::Test virgil_options)
    add_test(NAME library COMMAND library_test)
    set_tests_properties(library PROPERTIES TIMEOUT 90)
    add_executable(calendar_test tests/calendar_test.cpp)
    target_link_libraries(calendar_test PRIVATE virgil_core Qt6::Test virgil_options)
    add_test(NAME calendar COMMAND calendar_test)
    set_tests_properties(calendar PROPERTIES TIMEOUT 90)
    add_executable(workflow_probe tests/workflow_probe.cpp)
    target_link_libraries(workflow_probe PRIVATE virgil_core virgil_options)
    add_executable(widget_test tests/widget_test.cpp)
    target_link_libraries(widget_test PRIVATE virgil_ui Qt6::Test virgil_options)
    add_dependencies(widget_test workflow_probe)
    add_test(NAME widgets COMMAND widget_test)
    set_tests_properties(widgets PROPERTIES TIMEOUT 90 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
