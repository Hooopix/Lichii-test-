# SPDX-FileCopyrightText: Copyright 2026 Lichii Project
# SPDX-License-Identifier: GPL-3.0-or-later

cmake_minimum_required(VERSION 3.16)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Concurrent Test)

add_executable(glass_ui_tests
    glass_ui_test.cpp
    ../../frontend_common/config.cpp
    ../../frontend_common/profile_import.cpp
    ../../qt_common/config/uisettings.cpp
    ../../qt_common/config/uisettings.h
    ../../lichii/game/game_card.cpp
    ../../lichii/game/game_card.h
    ../../lichii/game/game_filter_proxy.h
    ../../lichii/glass/glass_command_palette.cpp
    ../../lichii/glass/glass_command_palette.h
    ../../lichii/glass/glass_game_entry.h
    ../../lichii/glass/glass_home_page.cpp
    ../../lichii/glass/glass_home_page.h
    ../../lichii/glass/glass_mods_page.cpp
    ../../lichii/glass/glass_mods_page.h
    ../../lichii/glass/glass_multiplayer_page.cpp
    ../../lichii/glass/glass_multiplayer_page.h
    ../../lichii/glass/glass_settings_page.cpp
    ../../lichii/glass/glass_settings_page.h
    ../../lichii/glass/glass_shell.cpp
    ../../lichii/glass/glass_shell.h
    ../../lichii/glass/glass_tokens.cpp
    ../../lichii/glass/glass_tokens.h
    ../../lichii/glass/glass_widgets.cpp
    ../../lichii/glass/glass_widgets.h
    ../../lichii/hotkeys.cpp
    ../../lichii/hotkeys.h
    ../../lichii/mod_store/mod_store_client.cpp
    ../../lichii/mod_store/mod_store_client.h
    ../../lichii/mod_store/mod_store_dialog.cpp
    ../../lichii/mod_store/mod_store_dialog.h
    ../../lichii/mod_store/mod_store_install.cpp
    ../../lichii/mod_store/mod_store_install.h
    ../../../dist/qt_themes/lichii/style.qrc
)

target_compile_features(glass_ui_tests PRIVATE cxx_std_20)
target_compile_definitions(glass_ui_tests PRIVATE
    QT_USE_QSTRINGBUILDER
    QT_NO_CAST_FROM_ASCII
    QT_NO_CAST_TO_ASCII
    QT_NO_NARROWING_CONVERSIONS_IN_CONNECT
    QT_NO_PROCESS_COMBINED_ARGUMENT_START
    QT_NO_URL_CAST_FROM_STRING
)
target_include_directories(glass_ui_tests PRIVATE ../.. ../../lichii)
target_link_libraries(glass_ui_tests PRIVATE
    common
    SimpleIni::SimpleIni
    QuaZip::QuaZip
    Qt6::Widgets
    Qt6::Network
    Qt6::Concurrent
    Qt6::Test
)

add_test(NAME glass_ui_tests COMMAND glass_ui_tests)
set_tests_properties(glass_ui_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
