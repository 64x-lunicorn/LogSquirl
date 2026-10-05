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

// "Jump to line number" is listed in the shortcut settings with no key, and
// a key the user binds to it selects the Log Line whose number was typed
// before it (#601). With no number typed it leaves the selection, and 0 counts
// as the first line (#614).

#include "configuration.h"
#include "logdata.h"
#include "logmainview.h"
#include "quickfindpattern.h"
#include "shortcuts.h"
#include "shown_widget.h"
#include "test_policies.h"
#include "test_utils.h"

#include <QShortcut>
#include <QSignalSpy>
#include <QTemporaryFile>
#include <QTest>

#include <catch2/catch_test_macros.hpp>

namespace {

constexpr int NbLogLines = 20;

// Holds the configured shortcuts, and puts back what they were.
class ConfiguredShortcuts {
public:
    ConfiguredShortcuts()
        : shortcuts_( Configuration::get().shortcuts() )
    {
    }

    ~ConfiguredShortcuts()
    {
        Configuration::get().setShortcuts( shortcuts_ );
    }

    ConfiguredShortcuts( const ConfiguredShortcuts& ) = delete;
    ConfiguredShortcuts& operator=( const ConfiguredShortcuts& ) = delete;

private:
    std::map<std::string, QStringList> shortcuts_;
};

} // namespace

SCENARIO( "Jump to line number is listed in the shortcut settings with no key", "[shortcuts]" )
{
    const auto& shortcuts = ShortcutAction::defaultShortcutList();
    const auto jumpToLineNumber = shortcuts.find( ShortcutAction::LogViewJumpToLineNumber );

    REQUIRE( jumpToLineNumber != shortcuts.end() );
    REQUIRE_FALSE( jumpToLineNumber->second.name.isEmpty() );
    REQUIRE( jumpToLineNumber->second.keySequence.isEmpty() );
}

SCENARIO( "A key bound to Jump to line number selects the typed line number",
          "[shortcuts][logmainview]" )
{
    const ConfiguredShortcuts configuredShortcuts;

    QTemporaryFile file{ "jump_to_line_number_test_XXXXXX" };
    REQUIRE( file.open() );
    for ( int line = 0; line < NbLogLines; ++line ) {
        file.write( QStringLiteral( "log line %1\n" ).arg( line ).toLatin1() );
    }
    file.flush();

    const auto policies = testSettingsPolicies();
    LogData logData( policies.indexing, policies.search, policies.fileAccess, policies.decoding );
    SafeQSignalSpy loadEndSpy( &logData, SIGNAL( loadingFinished( LoadingStatus ) ) );
    logData.attachFile( file.fileName() );
    REQUIRE( loadEndSpy.safeWait( 10000 ) );

    GIVEN( "a key bound to Jump to line number in the main view" )
    {
        const QKeySequence boundKey( Qt::Key_J );
        auto& config = Configuration::get();
        auto shortcuts = config.shortcuts();
        shortcuts[ ShortcutAction::LogViewJumpToLineNumber ]
            = QStringList{ boundKey.toString( QKeySequence::PortableText ) };
        config.setShortcuts( shortcuts );

        QuickFindPattern quickFindPattern;
        LogMainView view( &logData, &quickFindPattern, nullptr, nullptr, false );
        view.resize( 400, 200 );
        showUntilExposed( view );
        view.registerShortcuts();
        view.selectAndDisplayLine( 5_lnum );
        REQUIRE( view.getSelectedText() == QStringLiteral( "log line 5" ) );

        QShortcut* shortcut = nullptr;
        for ( auto* candidate : view.findChildren<QShortcut*>() ) {
            if ( candidate->key() == boundKey ) {
                shortcut = candidate;
            }
        }
        REQUIRE( shortcut != nullptr );

        WHEN( "a line number is typed and the key is pressed" )
        {
            QTest::keyClick( &view, Qt::Key_1 );
            QTest::keyClick( &view, Qt::Key_2 );

            QSignalSpy selected( &view, &AbstractLogView::newSelection );
            Q_EMIT shortcut->activated();

            THEN( "the Log Line with that number is selected" )
            {
                REQUIRE_FALSE( selected.isEmpty() );
                // Line numbers count from 1, Log Lines from 0.
                REQUIRE( qvariant_cast<LineNumber>( selected.last().at( 0 ) ) == 11_lnum );
            }
        }

        WHEN( "the key is pressed with no number typed" )
        {
            QSignalSpy selected( &view, &AbstractLogView::newSelection );
            Q_EMIT shortcut->activated();

            THEN( "the selection stays where it is" )
            {
                REQUIRE( selected.isEmpty() );
                REQUIRE( view.getSelectedText() == QStringLiteral( "log line 5" ) );
            }
        }

        WHEN( "0 is typed and the key is pressed" )
        {
            QTest::keyClick( &view, Qt::Key_0 );

            QSignalSpy selected( &view, &AbstractLogView::newSelection );
            Q_EMIT shortcut->activated();

            THEN( "the first Log Line is selected" )
            {
                REQUIRE_FALSE( selected.isEmpty() );
                REQUIRE( qvariant_cast<LineNumber>( selected.last().at( 0 ) ) == 0_lnum );
                REQUIRE( view.getSelectedText() == QStringLiteral( "log line 0" ) );
            }
        }

        WHEN( "0, 1 and 2 are typed and the key is pressed" )
        {
            QTest::keyClick( &view, Qt::Key_0 );
            QTest::keyClick( &view, Qt::Key_1 );
            QTest::keyClick( &view, Qt::Key_2 );

            QSignalSpy selected( &view, &AbstractLogView::newSelection );
            Q_EMIT shortcut->activated();

            THEN( "the Log Line with number 12 is selected" )
            {
                REQUIRE_FALSE( selected.isEmpty() );
                REQUIRE( qvariant_cast<LineNumber>( selected.last().at( 0 ) ) == 11_lnum );
            }
        }
    }
}
