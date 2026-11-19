cmake_minimum_required(VERSION 3.25)
project(MoviePlayer VERSION 0.1.0 LANGUAGES CXX)

if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(WIN32)
    set(LOCAL_QT6_DIR "${CMAKE_SOURCE_DIR}/third_party/qt6")
    if(EXISTS "${LOCAL_QT6_DIR}/lib/cmake/Qt6/Qt6Config.cmake")
        list(PREPEND CMAKE_PREFIX_PATH "${LOCAL_QT6_DIR}")
    endif()
endif()

find_package(Qt6 QUIET COMPONENTS Core Gui Widgets OpenGLWidgets Network)

if(WIN32 AND NOT Qt6_FOUND)
    set(QT_REPO_BASE "https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_682/qt6_682")
    set(QT_PKG_NAME "qt.qt6.682.win64_msvc2022_64")
    set(QT_UPDATES_XML "${CMAKE_BINARY_DIR}/qt6_updates.xml")
    message(STATUS "Qt6 not found on system; fetching Qt 6.8.2 metadata from download.qt.io...")
    file(DOWNLOAD "${QT_REPO_BASE}/Updates.xml" "${QT_UPDATES_XML}" TLS_VERIFY ON)
    file(READ "${QT_UPDATES_XML}" QT_UPDATES_CONTENT)

    string(FIND "${QT_UPDATES_CONTENT}" "<Name>${QT_PKG_NAME}</Name>" PKG_POS)
    if(PKG_POS EQUAL -1)
        message(FATAL_ERROR "Could not locate ${QT_PKG_NAME} in Qt Updates.xml")
    endif()
    string(SUBSTRING "${QT_UPDATES_CONTENT}" ${PKG_POS} 4000 PKG_BLOCK)

    string(REGEX MATCH "<Version>([^<]+)</Version>" _ "${PKG_BLOCK}")
    set(QT_PKG_VERSION "${CMAKE_MATCH_1}")
    string(REGEX MATCH "<DownloadableArchives>([^<]+)</DownloadableArchives>" _ "${PKG_BLOCK}")
    string(REPLACE ", " ";" QT_ARCHIVES "${CMAKE_MATCH_1}")

    file(MAKE_DIRECTORY "${LOCAL_QT6_DIR}")
    foreach(ARCHIVE IN LISTS QT_ARCHIVES)
        if(ARCHIVE MATCHES "^(qtbase-|icu-|d3dcompiler_)")
            set(ARCHIVE_FILE "${CMAKE_BINARY_DIR}/${QT_PKG_VERSION}${ARCHIVE}")
            if(NOT EXISTS "${ARCHIVE_FILE}")
                message(STATUS "Downloading Qt6 component ${ARCHIVE}...")
                file(DOWNLOAD
                    "${QT_REPO_BASE}/${QT_PKG_NAME}/${QT_PKG_VERSION}${ARCHIVE}"
                    "${ARCHIVE_FILE}"
                    TLS_VERIFY ON)
            endif()
            message(STATUS "Extracting ${ARCHIVE}...")
            file(ARCHIVE_EXTRACT INPUT "${ARCHIVE_FILE}" DESTINATION "${LOCAL_QT6_DIR}")
        endif()
    endforeach()

    unset(Qt6_DIR CACHE)
    list(PREPEND CMAKE_PREFIX_PATH "${LOCAL_QT6_DIR}")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets OpenGLWidgets Network)

if(WIN32)
    # libmpv: pinned to one release and verified by hash. To upgrade, change these
    # three values together (the hash is listed on the GitHub release page).
    set(LIBMPV_RELEASE 20261106)
    set(LIBMPV_BUILD 20261106-git-9028187538)
    set(LIBMPV_SHA256 b5a3cc91b5dabf88ad5bb21d000834da4fd0bbda30f4584d39c082127bd62414)

    set(LIBMPV_DIR "${CMAKE_SOURCE_DIR}/third_party/libmpv-${LIBMPV_BUILD}")
    if(NOT EXISTS "${LIBMPV_DIR}/libmpv-2.dll")
        set(LIBMPV_ARCHIVE "${CMAKE_BINARY_DIR}/libmpv-${LIBMPV_BUILD}.7z")
        message(STATUS "Downloading libmpv ${LIBMPV_BUILD}")
        file(DOWNLOAD
            "https://github.com/shinchiro/mpv-winbuild-cmake/releases/download/${LIBMPV_RELEASE}/mpv-dev-x86_64-${LIBMPV_BUILD}.7z"
            "${LIBMPV_ARCHIVE}"
            EXPECTED_HASH SHA256=${LIBMPV_SHA256}
            TLS_VERIFY ON)
        file(ARCHIVE_EXTRACT INPUT "${LIBMPV_ARCHIVE}" DESTINATION "${LIBMPV_DIR}")
    endif()

    add_library(libmpv SHARED IMPORTED)
    set_target_properties(libmpv PROPERTIES
        IMPORTED_LOCATION "${LIBMPV_DIR}/libmpv-2.dll"
        IMPORTED_IMPLIB "${LIBMPV_DIR}/libmpv.dll.a"
        INTERFACE_INCLUDE_DIRECTORIES "${LIBMPV_DIR}/include")
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(MPV REQUIRED IMPORTED_TARGET mpv)
    add_library(libmpv ALIAS PkgConfig::MPV)
endif()

set(PROJECT_SOURCES
    src/main.cpp
    src/ContextMenu.cpp
    src/ContextMenu.h
    src/ControlBar.cpp
    src/ControlBar.h
    src/FileAssociation.cpp
    src/FileAssociation.h
    src/FileDialogs.cpp
    src/FileDialogs.h
    src/InfoDialog.cpp
    src/InfoDialog.h
    src/MainWindow.cpp
    src/MainWindow.h
    src/MpvPlayer.cpp
    src/MpvPlayer.h
    src/MpvVideoWidget.cpp
    src/MpvVideoWidget.h
    src/PlaybackInfo.cpp
    src/PlaybackInfo.h
    src/Playlist.cpp
    src/Playlist.h
    src/PlaylistPanel.cpp
    src/PlaylistPanel.h
    src/Settings.cpp
    src/Settings.h
    src/SettingsDialog.cpp
    src/SettingsDialog.h
    src/Shortcuts.cpp
    src/Shortcuts.h
    src/SmiSubtitles.cpp
    src/SmiSubtitles.h
    src/TimeFormat.h
    res/resources.qrc
)

if(WIN32)
    list(APPEND PROJECT_SOURCES res/app.rc)
endif()

add_executable(MoviePlayer WIN32 MACOSX_BUNDLE ${PROJECT_SOURCES})

target_link_libraries(MoviePlayer PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGLWidgets
    Qt6::Network
    libmpv
)

if(WIN32)
    target_compile_definitions(MoviePlayer PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
    if(MSVC)
        target_compile_options(MoviePlayer PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4 /permissive- /utf-8>)
    endif()
    target_link_libraries(MoviePlayer PRIVATE advapi32 shell32 shlwapi user32 dxgi d3d11 dcomp psapi opengl32)

    # The executable loads libmpv-2.dll from its own folder.
    add_custom_command(TARGET MoviePlayer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:libmpv>" "$<TARGET_FILE_DIR:MoviePlayer>")

    # Deploy Qt runtime dependencies if windeployqt is available.
    get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(QT_WINDEPLOYQT_DIR "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${QT_WINDEPLOYQT_DIR}")
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET MoviePlayer POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                --no-translations
                --no-system-d3d-compiler
                --no-opengl-sw
                "$<TARGET_FILE:MoviePlayer>"
            COMMENT "Running windeployqt...")
    endif()
elseif(APPLE)
    target_compile_options(MoviePlayer PRIVATE -Wall -Wextra)
    target_link_libraries(MoviePlayer PRIVATE "-framework CoreFoundation" "-framework ApplicationServices" "-framework IOKit")
    set_target_properties(MoviePlayer PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/packaging/macos/Info.plist.in"
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.movieplayer"
        MACOSX_BUNDLE_BUNDLE_NAME "MoviePlayer"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
else()
    target_compile_options(MoviePlayer PRIVATE -Wall -Wextra)
endif()

if(WIN32)
    install(TARGETS MoviePlayer RUNTIME DESTINATION .)
    install(FILES "$<TARGET_FILE:libmpv>" LICENSE res/THIRD_PARTY_NOTICES.txt DESTINATION .)
    if(WINDEPLOYQT_EXECUTABLE)
        install(CODE "
            execute_process(
                COMMAND \"${WINDEPLOYQT_EXECUTABLE}\"
                    --no-translations
                    --no-system-d3d-compiler
                    --no-opengl-sw
                    \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/MoviePlayer.exe\"
                RESULT_VARIABLE _windeployqt_res
            )
            if(NOT _windeployqt_res EQUAL 0)
                message(FATAL_ERROR \"windeployqt failed during install/package\")
            endif()
        ")
    endif()
    set(CPACK_GENERATOR ZIP)
    set(CPACK_PACKAGE_FILE_NAME "MoviePlayer-${PROJECT_VERSION}-win64")
elseif(APPLE)
    install(TARGETS MoviePlayer BUNDLE DESTINATION .)
    install(FILES LICENSE res/THIRD_PARTY_NOTICES.txt DESTINATION .)
    get_target_property(QT_QMAKE_EXECUTABLE Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(QT_MACDEPLOYQT_DIR "${QT_QMAKE_EXECUTABLE}" DIRECTORY)
    find_program(MACDEPLOYQT_EXECUTABLE macdeployqt HINTS "${QT_MACDEPLOYQT_DIR}")
    if(MACDEPLOYQT_EXECUTABLE)
        install(CODE "
            execute_process(
                COMMAND \"${MACDEPLOYQT_EXECUTABLE}\"
                    \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/MoviePlayer.app\"
                    -always-overwrite
                RESULT_VARIABLE _macdeployqt_res
            )
            if(NOT _macdeployqt_res EQUAL 0)
                message(FATAL_ERROR \"macdeployqt failed during install/package\")
            endif()
        ")
    endif()
    set(CPACK_GENERATOR DragNDrop)
    set(CPACK_PACKAGE_FILE_NAME "MoviePlayer-${PROJECT_VERSION}-macos")
else()
    install(TARGETS MoviePlayer RUNTIME DESTINATION bin)
    install(FILES packaging/linux/movieplayer.desktop DESTINATION share/applications)
    install(FILES LICENSE res/THIRD_PARTY_NOTICES.txt DESTINATION share/doc/movieplayer)
    set(CPACK_GENERATOR "DEB")
    set(CPACK_PACKAGE_NAME "movieplayer")
    set(CPACK_PACKAGE_CONTACT "MoviePlayer Contributors <noreply@github.com>")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Qt6 and libmpv based video player")
    set(CPACK_DEBIAN_PACKAGE_SECTION "video")
    set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
    set(CPACK_PACKAGE_FILE_NAME "MoviePlayer-${PROJECT_VERSION}-linux-x86_64")
endif()
include(CPack)
