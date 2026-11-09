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
#include <QListWidget>
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

namespace {

/// The names the shown palette lists, each with its category.
QStringList paletteEntries( QDialog& palette )
{
    QStringList entries;
    auto* list = palette.findChild<QListWidget*>();
    REQUIRE( list != nullptr );
    for ( int row = 0; row < list->count(); ++row ) {
        const auto* item = list->item( row );
        entries.append( item->data( Qt::UserRole + 1 ).toString() + u'/' + item->text() );
    }
    return entries;
}

} // namespace

// The palette names its entries as the Shortcuts list names its commands:
// without the mnemonic, however a translation writes it, and without the
// dots of an entry that opens a dialog (#937).
SCENARIO( "The Command Palette names its entries without mnemonic leftovers",
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

    GIVEN( "Tools entries written as a Chinese translation and with a doubled ampersand" )
    {
        auto* palette = toolsPaletteAction( window );
        REQUIRE( palette != nullptr );
        QMenu* tools = nullptr;
        for ( auto* top : window.menuBar()->actions() ) {
            if ( top->menu() && top->text().remove( '&' ) == QStringLiteral( "Tools" ) ) {
                tools = top->menu();
            }
        }
        REQUIRE( tools != nullptr );
        auto* chinese = tools->addAction( QStringLiteral( "关闭(&C)..." ) );
        auto* ampersand = tools->addAction( QStringLiteral( "Save && &Reload" ) );

        WHEN( "the palette opens" )
        {
            palette->trigger();
            QTest::qWait( 50 );
            auto* shown = visiblePalette( window );
            REQUIRE( shown != nullptr );
            const auto entries = paletteEntries( *shown );

            THEN( "each is named without its mnemonic and dots, the ampersand kept" )
            {
                CHECK( entries.contains( QStringLiteral( "Tools/关闭" ) ) );
                CHECK( entries.contains( QStringLiteral( "Tools/Save & Reload" ) ) );
                CHECK( entries.contains( QStringLiteral( "Tools/Preferences" ) ) );
                for ( const auto& entry : entries ) {
                    INFO( entry.toStdString() );
                    if ( entry != QStringLiteral( "Tools/Save & Reload" ) ) {
                        CHECK_FALSE( entry.contains( u'&' ) );
                    }
                    CHECK_FALSE( entry.endsWith( QStringLiteral( "..." ) ) );
                }
            }
        }

        hidePalette( window );
        tools->removeAction( chinese );
        tools->removeAction( ampersand );
        delete chinese;
        delete ampersand;
    }

    window.close();
}
