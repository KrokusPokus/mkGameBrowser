cmake_minimum_required(VERSION 3.19)
project(mkGameBrowser VERSION 1.0.0 LANGUAGES CXX)

# Enforce the C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- 1. BUILD TYPE ---
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(GNUInstallDirs)

include(CheckIPOSupported)
check_ipo_supported(RESULT result)
if(result)
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

add_compile_options(
    -Wall -Wextra
    $<$<NOT:$<CONFIG:Debug>>:-O2>
)

# --- 2. QT & STANDARD SETUP ---
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets LinguistTools)
qt_standard_project_setup()
set(CMAKE_AUTORCC ON)

qt_add_executable(mkGameBrowser
    src/main.cpp

    src/Config.h src/Config.cpp
    src/GameGridView.h src/GameGridView.cpp
    src/GameLibrary.h src/GameLibrary.cpp
    src/InfoPopup.h src/InfoPopup.cpp
    src/MainWindow.h src/MainWindow.cpp
    src/ThumbnailCache.h src/ThumbnailCache.cpp
    src/ViewSettings.h src/ViewSettings.cpp

    res/resources.qrc
)

qt_add_translations(mkGameBrowser
    TS_FILES
        i18n/mkGameBrowser_de.ts
        i18n/mkGameBrowser_en.ts
    RESOURCE_PREFIX
        "/i18n"
)

target_compile_definitions(mkGameBrowser PRIVATE
    MKGAMEBROWSER_VERSION="${PROJECT_VERSION}"
    $<$<CONFIG:Release>:NDEBUG>
)

target_include_directories(mkGameBrowser PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(mkGameBrowser PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(mkGameBrowser PRIVATE -Wl,--as-needed)
endif()

set_target_properties(mkGameBrowser PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
)

install(TARGETS mkGameBrowser
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(FILES res/mkGameBrowser.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)
install(FILES res/mkGameBrowser.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
)

if(LINUX)
    add_custom_command(TARGET mkGameBrowser POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:mkGameBrowser>
            $ENV{HOME}/.local/bin/mkGameBrowser

        COMMENT "Copying mkGameBrowser to ~/.local/bin/"
    )
endif()
