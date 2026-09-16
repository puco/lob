cmake_minimum_required(VERSION 3.22)

project(lob VERSION 0.1.0 LANGUAGES CXX)

set(QT_MIN_VERSION 6.6.0)
set(KF_MIN_VERSION 6.0.0)

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMQmlModule)
include(ECMMarkAsTest)
include(FeatureSummary)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS
    Core Gui Qml Quick QuickControls2 Widgets Network DBus)

find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS
    CoreAddons I18n Config Service KIO DBusAddons
    WindowSystem IconThemes ColorScheme Crash Notifications
    StatusNotifierItem)

find_package(LayerShellQt REQUIRED)

# The app id is load-bearing: the D-Bus name KDBusService derives from
# organizationDomain + applicationName must equal the .desktop basename, or
# DBusActivatable silently falls back to Exec=.
set(LOB_APP_ID "io.github.puco.lob")

# The routing logic lives in a library so the tests exercise exactly the code
# the application runs, rather than a recompiled copy of it.
add_library(lobcore STATIC
    src/core/BrowserProfiles.cpp
    src/core/DefaultBrowserManager.cpp
    src/core/Launcher.cpp
    src/core/RuleEngine.cpp
    src/core/RuleStore.cpp
    src/core/Startup.cpp
    src/core/TargetRegistry.cpp
    src/core/UrlSanitizer.cpp
)

target_include_directories(lobcore PUBLIC src)

target_compile_definitions(lobcore PUBLIC
    LOB_APP_ID="${LOB_APP_ID}"
    LOB_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(lobcore PUBLIC
    Qt6::Core Qt6::Gui Qt6::Network
    KF6::CoreAddons KF6::I18n KF6::ConfigCore
    KF6::Service KF6::KIOGui KF6::WindowSystem
)

add_executable(lob)

target_sources(lob PRIVATE
    src/main.cpp
    src/app/Controller.cpp
    src/app/TrayController.cpp
    src/ui/PickerController.cpp
    src/ui/TargetModel.cpp
)

target_include_directories(lob PRIVATE src)

target_link_libraries(lob PRIVATE
    lobcore
    Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickControls2 Qt6::Widgets
    Qt6::Network Qt6::DBus
    KF6::CoreAddons KF6::I18n KF6::I18nQml KF6::ConfigCore KF6::ConfigGui
    KF6::Service KF6::KIOGui KF6::DBusAddons
    KF6::WindowSystem KF6::IconThemes KF6::ColorScheme
    KF6::Crash KF6::Notifications KF6::StatusNotifierItem
    LayerShellQt::Interface
)

ecm_add_qml_module(lob
    URI "io.github.puco.lob"
    GENERATE_PLUGIN_SOURCE
)

ecm_target_qml_sources(lob SOURCES
    qml/Picker.qml
)

install(TARGETS lob ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})

# Absolute path: Exec= and ExecStart= are resolved by the desktop environment
# and by systemd, neither of which necessarily has ~/.local/bin on PATH.
set(LOB_BIN "${CMAKE_INSTALL_PREFIX}/${KDE_INSTALL_BINDIR}/lob")

configure_file(data/${LOB_APP_ID}.desktop.in ${CMAKE_CURRENT_BINARY_DIR}/${LOB_APP_ID}.desktop @ONLY)
configure_file(data/${LOB_APP_ID}.service.in ${CMAKE_CURRENT_BINARY_DIR}/${LOB_APP_ID}.service @ONLY)
configure_file(data/lob.service.in ${CMAKE_CURRENT_BINARY_DIR}/lob.service @ONLY)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${LOB_APP_ID}.desktop
        DESTINATION ${KDE_INSTALL_APPDIR})
install(FILES data/${LOB_APP_ID}.metainfo.xml
        DESTINATION ${KDE_INSTALL_METAINFODIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${LOB_APP_ID}.service
        DESTINATION ${KDE_INSTALL_DBUSSERVICEDIR})
# systemd reads user units from <libdir>/systemd/user only under a system
# prefix; for a per-user prefix the searched location is <datadir>/systemd/user.
# KDE_INSTALL_SYSTEMDUSERUNITDIR always gives the former, so pick by prefix.
if(CMAKE_INSTALL_PREFIX STREQUAL "/usr" OR CMAKE_INSTALL_PREFIX STREQUAL "/usr/local")
    set(LOB_SYSTEMD_UNIT_DIR ${KDE_INSTALL_SYSTEMDUSERUNITDIR})
else()
    set(LOB_SYSTEMD_UNIT_DIR ${KDE_INSTALL_DATAROOTDIR}/systemd/user)
endif()

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/lob.service
        DESTINATION ${LOB_SYSTEMD_UNIT_DIR})

enable_testing()
add_subdirectory(tests)

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)
