# SPDX-FileCopyrightText: 2026 Tony Ayre <me@tonyayre.net>
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.20)
project(theranch VERSION 0.1.0 LANGUAGES CXX)

# Qt 6.10: QSortFilterProxyModel::begin/endFilterChange. KF 6.10: KLocalization QML
# context. LayerShellQt 6.8: matches the Plasma release this targets (Wayland-only).
# Developed and tested with Qt 6.12, KDE Frameworks 6.31 and Plasma 6.8.2.
set(QT_MIN_VERSION 6.10.0)
set(KF_MIN_VERSION 6.10.0)
set(LAYERSHELLQT_MIN_VERSION 6.8.0)

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
list(APPEND CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(KDECompilerSettings NO_POLICY_SCOPE)
include(ECMQmlModule)
include(ECMFindQmlModule)
include(FeatureSummary)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Gui Widgets Qml Quick QuickControls2)
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS
    CoreAddons I18n KIO DBusAddons GlobalAccel StatusNotifierItem Config XmlGui)
find_package(LayerShellQt ${LAYERSHELLQT_MIN_VERSION} REQUIRED)

# QML modules only needed at run time; checked here so a missing one is reported at
# configure time instead of as a blank screen.
ecm_find_qmlmodule(org.kde.kirigami REQUIRED)
ecm_find_qmlmodule(QtQuick.Dialogs REQUIRED)
ecm_find_qmlmodule(QtQuick.Effects REQUIRED)
ecm_find_qmlmodule(org.kde.desktop) # KDE's QtQuick Controls style (qqc2-desktop-style); recommended

qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

qt_add_executable(theranch
    src/main.cpp
    src/ranch.cpp        src/ranch.h
    src/groupstore.cpp   src/groupstore.h
    src/foldermodel.cpp  src/foldermodel.h
    src/pagemodel.cpp    src/pagemodel.h
    src/desktopstash.cpp src/desktopstash.h
)

qt_add_qml_module(theranch
    URI org.theranch
    VERSION 1.0
    # Plugins (Kirigami etc.) load at runtime; import scanning only matters for static Qt.
    NO_IMPORT_SCAN
    QML_FILES
        src/qml/Main.qml
        src/qml/PastureWindow.qml
        src/qml/DrawerWindow.qml
        src/qml/CorralFrame.qml
        src/qml/DrawerFrame.qml
        src/qml/CorralContent.qml
        src/qml/CorralMenu.qml
        src/qml/CorralBackground.qml
        src/qml/DockGlow.qml
        src/qml/BackgroundDialog.qml
        src/qml/IconPager.qml
        src/qml/IconDelegate.qml
)

# Single source of truth for the version shown by `theranch --version`.
target_compile_definitions(theranch PRIVATE THERANCH_VERSION="${PROJECT_VERSION}")

target_link_libraries(theranch PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::QuickControls2
    KF6::CoreAddons KF6::I18n KF6::I18nQml KF6::KIOCore KF6::KIOGui KF6::KIOWidgets
    KF6::DBusAddons KF6::GlobalAccel KF6::StatusNotifierItem KF6::ConfigCore KF6::XmlGui
    LayerShellQt::Interface
)

install(TARGETS theranch ${KDE_INSTALL_TARGETS_DEFAULT_ARGS})
install(FILES org.theranch.desktop DESTINATION ${KDE_INSTALL_APPDIR})

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)
