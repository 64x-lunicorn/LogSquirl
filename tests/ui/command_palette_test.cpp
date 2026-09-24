/*
 * Copyright (C) 2026 LogSquirl Contributors
 *
 * This file is part of LogSquirl.
 *
 * LogSquirl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * LogSquirl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LogSquirl.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "configuration.h"
#include "logformatcatalog.h"
#include "mainwindow.h"
#include "session.h"
#include "shortcuts.h"
#include "test_policies.h"

#include <QAction>
#include <QApplication>
#include <QDialog>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QTest>

#include <catch2/catch_test_macros.hpp>

// The Command Palette opens through a Tools menu action and its configurable
// shortcut (#484).

namespace {

QDialog* visiblePalette( MainWindow& window )
{
    for ( auto* dialog : window.findChildren<QDialog*>() ) {
        if ( dialog->inherits( "CommandPalette" ) && dialog->isVisible() ) {
            return dialog;
        }
    }
    return nullptr;
}

void hidePalette( MainWindow& window )
{
    if ( auto* palette = visiblePalette( window ) ) {
        palette->hide();
    }
    QTest::qWait( 50 );
}

QAction* toolsPaletteAction( MainWindow& window )
{
    for ( auto* top : window.menuBar()->actions() ) {
        if ( !top->menu() || top->text().remove( '&' ) != QStringLiteral( "Tools" ) ) {
            continue;
        }
        for ( auto* entry : top->menu()->actions() ) {
            if ( entry->text().startsWith( QStringLiteral( "Command Palette" ) ) ) {
                return entry;
            }
        }
    }
    return nullptr;
}

} // namespace

SCENARIO( "The Command Palette opens from the Tools menu and its shortcut",
          "[mainwindow][palette]" )
{
    auto session
        = std::make_shared<Session>( testSettingsPolicies(), std::make_shared<LogFormatCatalog>() );
    MainWindow window( WindowSession{ session, "Main", 0 },
                       std::make_shared<logsquirl::plugins::ApplicationPlugins>() );
    window.resize( 900, 600 );
    window.show();
    window.activateWindow();
    REQUIRE( QTest::qWaitForWindowActive( &window ) );

    GIVEN( "the default shortcut" )
    {
        auto* action = toolsPaletteAction( window );
        REQUIRE( action != nullptr );

        THEN( "the Tools entry shows Ctrl+Shift+P" )
        {
            REQUIRE( action->shortcut() == QKeySequence( Qt::CTRL | Qt::SHIFT | Qt::Key_P ) );
        }

        WHEN( "the Tools action is triggered" )
        {
            action->trigger();
            QTest::qWait( 50 );

            THEN( "the palette is open" )
            {
                REQUIRE( visiblePalette( window ) != nullptr );
            }
        }

        WHEN( "the shortcut key is pressed" )
        {
            hidePalette( window );
            REQUIRE( visiblePalette( window ) == nullptr );
            QTest::keyClick( &window, Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier );
            QTest::qWait( 50 );

            THEN( "the palette is open" )
            {
                REQUIRE( visiblePalette( window ) != nullptr );
            }
        }
    }

    GIVEN( "the shortcut is rebound to Ctrl+Alt+K" )
    {
        auto& config = Configuration::get();
        const auto original = config.shortcuts();
        auto rebound = original;
        rebound[ ShortcutAction::MainWindowCommandPalette ]
            = QStringList{ QKeySequence( Qt::CTRL | Qt::ALT | Qt::Key_K ).toString() };
        config.setShortcuts( rebound );
        window.applySettingsChange();
        hidePalette( window );

        WHEN( "the old key is pressed" )
        {
            QTest::keyClick( &window, Qt::Key_P, Qt::ControlModifier | Qt::ShiftModifier );
            QTest::qWait( 50 );

            THEN( "nothing opens" )
            {
                REQUIRE( visiblePalette( window ) == nullptr );
            }
        }

        WHEN( "the new key is pressed" )
        {
            QTest::keyClick( &window, Qt::Key_K, Qt::ControlModifier | Qt::AltModifier );
            QTest::qWait( 50 );

            THEN( "the palette is open" )
            {
                REQUIRE( visiblePalette( window ) != nullptr );
            }
        }

        config.setShortcuts( original );
        window.applySettingsChange();
    }

    hidePalette( window );
    window.close();
}
