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

#include <catch2/catch.hpp>

#include "fake_log_data.h"
#include "logformatdefinition.h"
#include "logtableview.h"

#include <QHeaderView>
#include <QSettings>
#include <QTest>

namespace {

// A Log Format of its own, so the column widths this test saves cannot meet
// any a real Log Format has saved.
LogFormatDefinition makeFormat()
{
    LogFormatDefinition format;
    format.setName( "logtableview_test_column_widths" );
    format.setTitle( "Table View column widths test" );

    QHash<QString, QString> regex;
    regex[ "basic" ] = R"(^(?<timestamp>\w{3}\s+\d+ \d{2}:\d{2}:\d{2}) (?<host>\S+) (?<body>.*)$)";
    format.setRegexPatterns( regex );
    format.setTimestampField( "timestamp" );
    format.setBodyField( "body" );

    QHash<QString, LogFormatValueDef> values;
    values[ "host" ] = LogFormatValueDef{ "string", true, false };
    format.setValueDefinitions( values );

    return format;
}

const QStringList Lines = {
    "Jan  1 12:00:00 host1 first message",
    "Jan  1 12:00:01 host2 second message",
};

constexpr int WidenedWidth = 321;

// Show the Log File in a Table View, and let its deferred column sizing run.
void open( LogTableView& view, const LogFormatDefinition& format, FakeLogData& logData )
{
    view.resize( 800, 400 );
    view.setLogFormat( &format, &logData );
    view.updateData( nullptr, false );
    QTest::qWait( 20 );
}

} // namespace

SCENARIO( "Column widths saved for a Log Format are restored when its Log File is opened again",
          "[logtableview][columnwidths]" )
{
    const auto format = makeFormat();
    FakeLogData logData( Lines );
    const QString settingsGroup = "logformat/columns/" + format.name();
    QSettings{}.remove( settingsGroup );

    GIVEN( "a Table View in which the user widened the first column" )
    {
        {
            LogTableView view;
            open( view, format, logData );
            REQUIRE( view.columnWidth( 0 ) != WidenedWidth );

            view.horizontalHeader()->resizeSection( 0, WidenedWidth );
        }

        WHEN( "the Log File is opened again in a new Table View" )
        {
            LogTableView view;
            open( view, format, logData );

            THEN( "the first column has the width the user gave it" )
            {
                REQUIRE( view.columnWidth( 0 ) == WidenedWidth );
            }
        }
    }

    QSettings{}.remove( settingsGroup );
}
