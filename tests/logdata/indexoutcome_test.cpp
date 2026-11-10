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

#include <catch2/catch_test_macros.hpp>

#include <memory>

#include <QByteArray>
#include <QFile>
#include <QTemporaryDir>

#include "fake_run_control.h"
#include "test_policies.h"

#include "indexoperation.h"

// An index job tells, in its outcome, the first Log Line it may have changed
// (#948): every Search of the Open Log File is told it, and keeps what it
// found in the Log Lines before it.

namespace {

void writeContent( const QString& path, const QByteArray& content )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Truncate ) );
    REQUIRE( file.write( content ) == content.size() );
}

void appendContent( const QString& path, const QByteArray& content )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly | QIODevice::Append ) );
    REQUIRE( file.write( content ) == content.size() );
}

class IndexedLogFile {
public:
    explicit IndexedLogFile( const QString& fileName )
        : fileName_( fileName )
    {
    }

    IndexOutcome indexInFull()
    {
        return FullIndexOperation{ fileName_, data_, run_, policy_ }.run();
    }

    IndexOutcome indexAppendedLines()
    {
        return PartialIndexOperation{ fileName_, data_, run_, policy_ }.run();
    }

    LinesCount lines() const
    {
        return IndexingData::ConstAccessor{ data_.get() }.getNbLines();
    }

private:
    QString fileName_;
    IndexingPolicy policy_ = testSettingsPolicies().indexing;
    std::shared_ptr<IndexingData> data_ = std::make_shared<IndexingData>();
    FakeRunControl run_;
};

} // namespace

SCENARIO( "An index job tells the first Log Line it may have changed", "[indexing][outcome]" )
{
    QTemporaryDir logDir;
    REQUIRE( logDir.isValid() );
    const auto fileName = logDir.filePath( "outcome.log" );

    GIVEN( "a Log File of three Log Lines, the last one not ended yet" )
    {
        writeContent( fileName, "first\nsecond\nthi" );
        IndexedLogFile logFile{ fileName };

        WHEN( "it is attached" )
        {
            const auto outcome = logFile.indexInFull();

            THEN( "any Log Line may have changed" )
            {
                REQUIRE( std::get<LoadingStatus>( outcome.status ) == LoadingStatus::Successful );
                REQUIRE( logFile.lines() == 3_lcount );
                REQUIRE( outcome.firstChanged == 0_lnum );
            }
        }

        AND_GIVEN( "it was indexed, and data was then appended to it" )
        {
            REQUIRE( logFile.indexInFull().firstChanged == 0_lnum );
            appendContent( fileName, "rd\nfourth\n" );

            WHEN( "the appended Log Lines are indexed" )
            {
                const auto outcome = logFile.indexAppendedLines();

                THEN( "the last Log Line indexed before the growth is the first that changed" )
                {
                    REQUIRE( std::get<LoadingStatus>( outcome.status )
                             == LoadingStatus::Successful );
                    REQUIRE( logFile.lines() == 4_lcount );
                    REQUIRE( outcome.firstChanged == 2_lnum );
                }
            }

            WHEN( "it is indexed in full again" )
            {
                const auto outcome = logFile.indexInFull();

                THEN( "any Log Line may have changed" )
                {
                    REQUIRE( std::get<LoadingStatus>( outcome.status )
                             == LoadingStatus::Successful );
                    REQUIRE( logFile.lines() == 4_lcount );
                    REQUIRE( outcome.firstChanged == 0_lnum );
                }
            }
        }
    }

    GIVEN( "an empty Log File that was indexed, and Log Lines then appended to it" )
    {
        writeContent( fileName, "" );
        IndexedLogFile logFile{ fileName };
        REQUIRE( logFile.indexInFull().firstChanged == 0_lnum );
        appendContent( fileName, "first\nsecond\n" );

        WHEN( "the appended Log Lines are indexed" )
        {
            const auto outcome = logFile.indexAppendedLines();

            THEN( "they are all new, from the first on" )
            {
                REQUIRE( logFile.lines() == 2_lcount );
                REQUIRE( outcome.firstChanged == 0_lnum );
            }
        }
    }
}
