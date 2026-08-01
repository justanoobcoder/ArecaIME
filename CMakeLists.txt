cmake_minimum_required(VERSION 3.20)

project(fcitx5-areca VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)
include(CTest)

find_package(Fcitx5Core REQUIRED)
find_package(Fcitx5Config REQUIRED)
find_package(Fcitx5Utils REQUIRED)
find_program(GO_EXECUTABLE go REQUIRED)

set(BAMBOO_BRIDGE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/bamboo-bridge")
set(BAMBOO_ARCHIVE "${CMAKE_CURRENT_BINARY_DIR}/libareca-bamboo.a")
file(GLOB BAMBOO_CORE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/bamboo/bamboo-core/*.go")

add_custom_command(
    OUTPUT "${BAMBOO_ARCHIVE}" "${CMAKE_CURRENT_BINARY_DIR}/libareca-bamboo.h"
    COMMAND "${CMAKE_COMMAND}" -E env
            "GOCACHE=${CMAKE_CURRENT_BINARY_DIR}/go-build-cache"
            "${GO_EXECUTABLE}" build -buildvcs=false -buildmode=c-archive
            -o "${BAMBOO_ARCHIVE}" .
    WORKING_DIRECTORY "${BAMBOO_BRIDGE_DIR}"
    DEPENDS
        "${BAMBOO_BRIDGE_DIR}/go.mod"
        "${BAMBOO_BRIDGE_DIR}/main.go"
        ${BAMBOO_CORE_SOURCES}
    VERBATIM)
add_custom_target(areca-bamboo-bridge DEPENDS "${BAMBOO_ARCHIVE}")

add_library(areca-bamboo STATIC IMPORTED GLOBAL)
set_target_properties(areca-bamboo PROPERTIES IMPORTED_LOCATION "${BAMBOO_ARCHIVE}")
add_dependencies(areca-bamboo areca-bamboo-bridge)

option(BUILD_UINPUT_SERVER "Build the companion uinput Unix-socket server" ON)
if(BUILD_UINPUT_SERVER)
    set(ARECA_UINPUT_SERVER "${CMAKE_CURRENT_BINARY_DIR}/areca-uinput-server")
    add_custom_command(
        OUTPUT "${ARECA_UINPUT_SERVER}"
        COMMAND "${CMAKE_COMMAND}" -E env
                "GOCACHE=${CMAKE_CURRENT_BINARY_DIR}/go-build-cache"
                "${GO_EXECUTABLE}" build -buildvcs=false
                -o "${ARECA_UINPUT_SERVER}" .
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/server"
        DEPENDS server/go.mod server/main.go
        VERBATIM)
    add_custom_target(areca-uinput-server-bin ALL
                      DEPENDS "${ARECA_UINPUT_SERVER}")
    install(PROGRAMS "${ARECA_UINPUT_SERVER}"
            DESTINATION "${CMAKE_INSTALL_LIBEXECDIR}")
endif()

add_library(areca MODULE
    src/areca_engine.cpp
    src/bamboo_engine_adapter.cpp
    src/browser_autocomplete.cpp
    src/input_scheduler.cpp
    src/reliability_checker.cpp
    src/sentence_capitalizer.cpp
    src/surrounding_text_cache.cpp
    src/surrounding_text_backend.cpp
    src/uinput_socket_backend.cpp
    src/word_segment.cpp)
set_target_properties(areca PROPERTIES PREFIX "" OUTPUT_NAME "areca")
target_compile_features(areca PRIVATE cxx_std_17)
target_compile_options(areca PRIVATE -Wall -Wextra -Wpedantic)
target_include_directories(areca PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(areca PRIVATE
    areca-bamboo
    Fcitx5::Core
    Fcitx5::Config
    Fcitx5::Utils
    pthread dl m)

configure_file(data/areca-addon.conf.in areca-addon.conf @ONLY)
configure_file(data/areca.conf.in areca-inputmethod.conf @ONLY)

install(TARGETS areca DESTINATION "${CMAKE_INSTALL_LIBDIR}/fcitx5")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/areca-addon.conf"
        RENAME areca.conf DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/addon")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/areca-inputmethod.conf"
        RENAME areca.conf DESTINATION "${CMAKE_INSTALL_DATADIR}/fcitx5/inputmethod")

set(ARECA_SCALABLE_ICON
    "${CMAKE_CURRENT_SOURCE_DIR}/icons/hicolor/scalable/apps/fcitx-areca.svg")
foreach(ARECA_ICON_NAME
        fcitx-areca
        areca
        org.fcitx.Fcitx5.fcitx-areca)
    install(FILES "${ARECA_SCALABLE_ICON}"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps"
            RENAME "${ARECA_ICON_NAME}.svg")
endforeach()

set(ARECA_SYMBOLIC_ICON
    "${CMAKE_CURRENT_SOURCE_DIR}/icons/hicolor/symbolic/apps/fcitx-areca-symbolic.svg")
foreach(ARECA_ICON_NAME
        fcitx-areca-symbolic
        areca-symbolic
        org.fcitx.Fcitx5.fcitx-areca-symbolic)
    install(FILES "${ARECA_SYMBOLIC_ICON}"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/icons/hicolor/symbolic/apps"
            RENAME "${ARECA_ICON_NAME}.svg")
endforeach()

if(BUILD_TESTING)
    add_executable(areca-bamboo-test
        tests/bamboo_engine_adapter_test.cpp
        src/bamboo_engine_adapter.cpp)
    target_compile_features(areca-bamboo-test PRIVATE cxx_std_17)
    target_compile_options(areca-bamboo-test PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(areca-bamboo-test PRIVATE -UNDEBUG)
    target_include_directories(areca-bamboo-test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(areca-bamboo-test PRIVATE
        areca-bamboo Fcitx5::Utils pthread dl m)
    add_test(NAME bamboo-adapter COMMAND areca-bamboo-test)

    add_executable(areca-word-segment-test
        tests/word_segment_test.cpp
        src/word_segment.cpp)
    target_compile_features(areca-word-segment-test PRIVATE cxx_std_17)
    target_compile_options(areca-word-segment-test PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_include_directories(areca-word-segment-test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(areca-word-segment-test PRIVATE Fcitx5::Utils)
    add_test(NAME word-segment COMMAND areca-word-segment-test)

    add_executable(areca-browser-autocomplete-test
        tests/browser_autocomplete_test.cpp
        src/browser_autocomplete.cpp)
    target_compile_features(areca-browser-autocomplete-test PRIVATE cxx_std_17)
    target_compile_options(areca-browser-autocomplete-test PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_include_directories(areca-browser-autocomplete-test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(areca-browser-autocomplete-test PRIVATE Fcitx5::Utils)
    add_test(NAME browser-autocomplete COMMAND areca-browser-autocomplete-test)

    add_executable(areca-sentence-capitalizer-test
        tests/sentence_capitalizer_test.cpp
        src/sentence_capitalizer.cpp)
    target_compile_features(areca-sentence-capitalizer-test PRIVATE cxx_std_17)
    target_compile_options(areca-sentence-capitalizer-test PRIVATE
        -Wall -Wextra -Wpedantic -UNDEBUG)
    target_include_directories(areca-sentence-capitalizer-test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(areca-sentence-capitalizer-test PRIVATE Fcitx5::Utils)
    add_test(NAME sentence-capitalizer COMMAND areca-sentence-capitalizer-test)
endif()
