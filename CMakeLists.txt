cmake_minimum_required(VERSION 3.21)
# Numéro de version du programme : écrit ici et nulle part ailleurs.
project(DebridManager VERSION 1.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Network Svg)
# Linux : D-Bus pour l'anti-veille et « Afficher dans le dossier ».
set(DM_QT_LIBS Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Svg)
if(UNIX AND NOT APPLE)
    find_package(Qt6 REQUIRED COMPONENTS DBus)
    list(APPEND DM_QT_LIBS Qt6::DBus)
    set(DM_DEFINES DM_HAVE_DBUS)
endif()

set(SOURCES
    src/main.cpp
    src/core/Types.h          src/core/Types.cpp
    src/core/Utils.h          src/core/Utils.cpp
    src/core/Config.h         src/core/Config.cpp
    src/core/Associations.h   src/core/Associations.cpp
    src/core/Archives.h       src/core/Archives.cpp
    src/core/SevenZip.h       src/core/SevenZip.cpp
    src/core/ExtractionManager.h src/core/ExtractionManager.cpp
    src/core/DownloadModel.h  src/core/DownloadModel.cpp
    src/core/DownloadManager.h src/core/DownloadManager.cpp
    src/core/LibraryModel.h   src/core/LibraryModel.cpp
    src/net/Http.h            src/net/Http.cpp
    src/net/DebridApi.h       src/net/DebridApi.cpp
    src/net/RealDebridApi.h   src/net/RealDebridApi.cpp
    src/net/AllDebridApi.h    src/net/AllDebridApi.cpp
    src/net/HosterDetector.h  src/net/HosterDetector.cpp
    src/jobs/LinkResolver.h   src/jobs/LinkResolver.cpp
    src/jobs/TorrentJob.h     src/jobs/TorrentJob.cpp
    src/jobs/DownloadTask.h   src/jobs/DownloadTask.cpp
    src/jobs/TransferWorker.h src/jobs/TransferWorker.cpp
    src/jobs/ExtractJob.h     src/jobs/ExtractJob.cpp
    src/ui/Theme.h            src/ui/Theme.cpp
    src/ui/Icons.h            src/ui/Icons.cpp
    src/ui/Widgets.h          src/ui/Widgets.cpp
    src/ui/ProgressDelegate.h src/ui/ProgressDelegate.cpp
    src/ui/NameDelegate.h     src/ui/NameDelegate.cpp
    src/ui/DownloadFilterProxy.h src/ui/DownloadFilterProxy.cpp
    src/ui/DetailsPanel.h     src/ui/DetailsPanel.cpp
    src/ui/FileSelectorDialog.h src/ui/FileSelectorDialog.cpp
    src/ui/SettingsDialog.h   src/ui/SettingsDialog.cpp
    src/ui/AddLinksDialog.h   src/ui/AddLinksDialog.cpp
    src/ui/PasswordDialog.h   src/ui/PasswordDialog.cpp
    src/ui/DownloadsPage.h    src/ui/DownloadsPage.cpp
    src/ui/LibraryPage.h      src/ui/LibraryPage.cpp
    src/ui/MainWindow.h       src/ui/MainWindow.cpp
)

set(APP_RESOURCES resources/resources.qrc)
if(WIN32)
    # app.rc (icône + infos de version de l'exe) est généré avec la version ci-dessus.
    set(DM_ICON_PATH "${CMAKE_CURRENT_SOURCE_DIR}/resources/app.ico")
    configure_file(resources/app.rc.in ${CMAKE_CURRENT_BINARY_DIR}/app.rc @ONLY)
    list(APPEND APP_RESOURCES ${CMAKE_CURRENT_BINARY_DIR}/app.rc)
endif()

qt_add_executable(DebridManager WIN32 MACOSX_BUNDLE ${SOURCES} ${APP_RESOURCES})
target_include_directories(DebridManager PRIVATE src)
target_link_libraries(DebridManager PRIVATE ${DM_QT_LIBS})
target_compile_definitions(DebridManager PRIVATE
    APP_VERSION="${PROJECT_VERSION}" ${DM_DEFINES}
)
if(WIN32)
    target_link_libraries(DebridManager PRIVATE shell32)   # SHChangeNotify (associations)
endif()

if(MSVC)
    target_compile_options(DebridManager PRIVATE /W4 /utf-8 /permissive-)
else()
    target_compile_options(DebridManager PRIVATE -Wall -Wextra)
endif()

# ---- Tests hors-ligne (optionnels) : cmake -DDM_BUILD_TESTS=ON ----
option(DM_BUILD_TESTS "Compiler les tests d'intégration hors-ligne" OFF)
if(DM_BUILD_TESTS)
    set(TEST_SOURCES ${SOURCES})
    list(REMOVE_ITEM TEST_SOURCES src/main.cpp)
    qt_add_executable(dm_selftest tests/selftest.cpp ${TEST_SOURCES} resources/resources.qrc)
    target_include_directories(dm_selftest PRIVATE src)
    target_link_libraries(dm_selftest PRIVATE ${DM_QT_LIBS})
    target_compile_definitions(dm_selftest PRIVATE APP_VERSION="${PROJECT_VERSION}" ${DM_DEFINES})
endif()
if(DM_BUILD_TESTS)
    qt_add_executable(dm_gallery tests/gallery.cpp ${TEST_SOURCES} resources/resources.qrc)
    target_include_directories(dm_gallery PRIVATE src)
    target_link_libraries(dm_gallery PRIVATE ${DM_QT_LIBS})
    target_compile_definitions(dm_gallery PRIVATE APP_VERSION="${PROJECT_VERSION}" ${DM_DEFINES})
endif()
if(DM_BUILD_TESTS AND UNIX AND NOT APPLE)
    # Faux services de bureau (anti-veille, gestionnaire de fichiers) pour les tests
    qt_add_executable(dm_fakedesktop tests/fakedesktop.cpp)
    target_link_libraries(dm_fakedesktop PRIVATE Qt6::Core Qt6::DBus)
endif()
if(DM_BUILD_TESTS AND WIN32)
    target_link_libraries(dm_selftest PRIVATE shell32)
    target_link_libraries(dm_gallery PRIVATE shell32)
endif()
