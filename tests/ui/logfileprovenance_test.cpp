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

// What follows from where a Log File came from (#643).

#include "logfileprovenance.h"

#include "commandsource.h"

#include <QStringLiteral>

#include <catch2/catch_test_macros.hpp>

#include <functional>
#include <vector>

namespace {

struct Row {
    const char* what;
    std::function<LogFileProvenance()> provenance;
    QString fileName;
    // What the recent files keep; empty for nothing.
    QString recentFile;
    // What the tab's name and group are stored by; empty for nothing.
    QString storedKey;
    QString shownTitle;
    bool savedWithSession;
};

} // namespace

SCENARIO( "Where a Log File came from decides its recent file, its stored tab and its title",
          "[ui][session][provenance]" )
{
    const auto fromZip
        = ArchiveMember{ QStringLiteral( "/logs/logs.zip" ), { QStringLiteral( "a/app.har" ) } };
    const std::vector<Row> rows{
        { "an Ordinary Log File", [] { return LogFileProvenance::ordinary(); },
          QStringLiteral( "/logs/app.log" ), QStringLiteral( "/logs/app.log" ),
          QStringLiteral( "/logs/app.log" ), QStringLiteral( "app.log" ), true },
        { "a Log File decompressed from an archive",
          [] {
              return LogFileProvenance::fromArchive(
                  ArchiveMember{ QStringLiteral( "/logs/app.log.gz" ), { QString{} } } );
          },
          QStringLiteral( "/tmp/work/app.log.gz.a1b2c3" ), QStringLiteral( "/logs/app.log.gz" ),
          QStringLiteral( "/logs/app.log.gz!/" ), QStringLiteral( "app.log.gz.a1b2c3" ), true },
        { "standard input",
          [] {
              return LogFileProvenance::transient( QStringLiteral( "stdin" ),
                                                   QStringLiteral( "Standard input" ) );
          },
          QStringLiteral( "/tmp/stdin.spool" ), QString{}, QString{}, QStringLiteral( "stdin" ),
          false },
        { "a command's output",
          [] {
              return LogFileProvenance::commandOutput( nullptr, QStringLiteral( "tail -f x" ),
                                                       QStringLiteral( "tail -f x" ) );
          },
          QStringLiteral( "/tmp/command.spool" ), QString{}, QString{},
          QStringLiteral( "tail -f x" ), false },
        { "a merge", [] { return LogFileProvenance::transient( QStringLiteral( "Merged" ) ); },
          QStringLiteral( "/tmp/logsquirl_merged_1.log" ), QString{}, QString{},
          QStringLiteral( "Merged" ), false },
        { "the clipboard", [] { return LogFileProvenance::transient(); },
          QStringLiteral( "/tmp/work/logsquirl_clipboard.x1" ), QString{}, QString{},
          QStringLiteral( "logsquirl_clipboard.x1" ), false },
        { "what a converter wrote for an Ordinary Log File",
          [] {
              return LogFileProvenance::conversionOf( QStringLiteral( "/logs/app.har" ),
                                                      LogFileOrigin{} );
          },
          QStringLiteral( "/tmp/work/app.har.txt.y2" ), QStringLiteral( "/logs/app.har" ),
          QString{}, QStringLiteral( "app.har.txt.y2" ), false },
        { "what a converter wrote for a Log File decompressed from an archive",
          [ &fromZip ] {
              return LogFileProvenance::conversionOf( QStringLiteral( "/tmp/work/a/app.har" ),
                                                      LogFileOrigin::fromArchive( fromZip ) );
          },
          QStringLiteral( "/tmp/work/app.har.txt.z3" ), QStringLiteral( "/logs/logs.zip" ),
          QString{}, QStringLiteral( "app.har.txt.z3" ), false },
        { "what a converter wrote for a Transient Log File",
          [] {
              return LogFileProvenance::conversionOf( QStringLiteral( "/tmp/stream.har" ),
                                                      LogFileOrigin::transient() );
          },
          QStringLiteral( "/tmp/work/stream.har.txt.w4" ), QString{}, QString{},
          QStringLiteral( "stream.har.txt.w4" ), false },
    };

    for ( const auto& row : rows ) {
        GIVEN( row.what )
        {
            const auto provenance = row.provenance();

            THEN( "it is kept, stored, titled and saved as it came" )
            {
                REQUIRE( provenance.origin.recentFile( row.fileName ) == row.recentFile );
                REQUIRE( provenance.origin.storedKey( row.fileName ) == row.storedKey );
                REQUIRE( provenance.shownTitle( row.fileName ) == row.shownTitle );
                REQUIRE( provenance.origin.savedWithSession() == row.savedWithSession );
            }
        }
    }
}
