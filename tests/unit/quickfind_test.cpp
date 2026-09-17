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

// QuickFind over the QuickFindLines a view hands it (#287): it finds the same
// matches forwards and backwards wherever they lie -- on the first or the last
// Log Line, or on either side of where one block of Log Lines it reads ends
// and the next begins (a thousand Log Lines each).

#include "fake_log_data.h"
#include "qfnotifications.h"
#include "quickfind.h"
#include "quickfindpattern.h"
#include "selection.h"
#include "test_utils.h"

#include <QStringList>

#include <atomic>
#include <optional>
#include <span>

#include <catch2/catch.hpp>

namespace {

constexpr int NbLogLines = 3500;

QString logLineText( int line )
{
    return QStringLiteral( "this is line %1" ).arg( line, 6, 10, QLatin1Char( '0' ) );
}

QStringList logLineTexts( int nbLogLines = NbLogLines )
{
    QStringList texts;
    for ( int line = 0; line < nbLogLines; ++line ) {
        texts.append( logLineText( line ) );
    }
    return texts;
}

// A Log File in memory that counts how QuickFind reads it: Log Line by Log
// Line or in blocks, and whether a reader stays attached while it does.
class CountingLogData : public FakeLogData {
public:
    using FakeLogData::FakeLogData;

    mutable std::atomic<int> linesReadOneByOne{ 0 };
    mutable std::atomic<int> blocksRead{ 0 };
    mutable std::atomic<int> readsWithoutReader{ 0 };
    mutable std::atomic<int> readers{ 0 };

protected:
    QString doGetExpandedLineString( LineNumber line ) const override
    {
        ++linesReadOneByOne;
        noteRead();
        return FakeLogData::doGetExpandedLineString( line );
    }

    logsquirl::vector<QString>
    doGetExpandedLinesSparse( std::span<const LineNumber> lines ) const override
    {
        ++blocksRead;
        noteRead();
        logsquirl::vector<QString> text;
        for ( const auto line : lines ) {
            text.push_back( FakeLogData::doGetExpandedLineString( line ) );
        }
        return text;
    }

    void doAttachReader() const override
    {
        ++readers;
    }

    void doDetachReader() const override
    {
        --readers;
    }

private:
    void noteRead() const
    {
        if ( readers <= 0 ) {
            ++readsWithoutReader;
        }
    }
};

// What a QuickFind reported when it was done.
struct QuickFindResult {
    bool hasMatch = false;
    Portion match;
};

// A QuickFind over copyLines, every Log Line displayed.
class QuickFindRun {
public:
    explicit QuickFindRun( std::function<QuickFindLines()> copyLines )
        : quickFind_( std::move( copyLines ), []( LineNumber ) { return true; } )
    {
        QObject::connect( &quickFind_, &QuickFind::searchDone, &quickFind_,
                          [ this ]( bool hasMatch, Portion match ) {
                              result_ = QuickFindResult{ hasMatch, match };
                          } );
        QObject::connect( &quickFind_, &QuickFind::notify, &quickFind_,
                          [ this ]( const QFNotification& notification ) {
                              notifications_.append( notification.message() );
                          } );
    }

    // The messages of every notification QuickFind sent.
    const QStringList& notifications() const
    {
        return notifications_;
    }

    // Searches forward for pattern from the Log Line selected.
    QuickFindResult forwardFrom( LineNumber selected, const QString& pattern )
    {
        return run( selected, pattern, true );
    }

    // Searches backward for pattern from the Log Line selected.
    QuickFindResult backwardFrom( LineNumber selected, const QString& pattern )
    {
        return run( selected, pattern, false );
    }

private:
    QuickFindResult run( LineNumber selected, const QString& pattern, bool forward )
    {
        QuickFindPattern quickFindPattern;
        quickFindPattern.changeSearchPattern( pattern, /* useExtendedRegexp */ true,
                                              /* isRegex */ true );
        Selection selection;
        selection.selectLine( selected );

        result_.reset();
        if ( forward ) {
            quickFind_.searchForward( selection, quickFindPattern.getMatcher() );
        }
        else {
            quickFind_.searchBackward( selection, quickFindPattern.getMatcher() );
        }
        REQUIRE( waitUiState( [ this ]() { return result_.has_value(); }, 10000 ) );
        return *result_;
    }

    QuickFind quickFind_;
    std::optional<QuickFindResult> result_;
    QStringList notifications_;
};

QString pattern( int line )
{
    return QStringLiteral( "line %1$" ).arg( line, 6, 10, QLatin1Char( '0' ) );
}

} // namespace

SCENARIO( "QuickFind over every Log Line finds matches in any block", "[quickfind]" )
{
    const FakeLogData logFile{ logLineTexts() };
    QuickFindRun quickFind( [ & ]() { return QuickFindLines::everyLogLine( logFile ); } );

    // A search reads a thousand Log Lines at a time, from the one after (or
    // before) the selected one.
    SECTION( "forwards, on the last Log Line of the first block" )
    {
        const auto result = quickFind.forwardFrom( 5_lnum, pattern( 1005 ) );
        REQUIRE( result.hasMatch );
        REQUIRE( result.match.line() == 1005_lnum );
        REQUIRE( result.match.startColumn() == 8_lcol );
    }

    SECTION( "forwards, on the first Log Line of the second block" )
    {
        const auto result = quickFind.forwardFrom( 5_lnum, pattern( 1006 ) );
        REQUIRE( result.hasMatch );
        REQUIRE( result.match.line() == 1006_lnum );
    }

    SECTION( "backwards, on the last Log Line of the first block" )
    {
        const auto result = quickFind.backwardFrom( 3000_lnum, pattern( 2000 ) );
        REQUIRE( result.hasMatch );
        REQUIRE( result.match.line() == 2000_lnum );
    }

    SECTION( "backwards, on the first Log Line of the second block" )
    {
        const auto result = quickFind.backwardFrom( 3000_lnum, pattern( 1999 ) );
        REQUIRE( result.hasMatch );
        REQUIRE( result.match.line() == 1999_lnum );
    }

    SECTION( "forwards several blocks away, onto the last Log Line" )
    {
        const auto result = quickFind.forwardFrom( 3_lnum, pattern( NbLogLines - 1 ) );
        REQUIRE( result.hasMatch );
        REQUIRE( result.match.line() == LineNumber( NbLogLines - 1 ) );
    }

    SECTION( "backwards several blocks away, onto the first Log Line" )
    {
        const auto result = quickFind.backwardFrom( 3400_lnum, pattern( 0 ) );
        REQUIRE( result.hasMatch );
        REQUIRE( result.match.line() == 0_lnum );
    }

    SECTION( "forwards finds the next of several matches" )
    {
        const auto result
            = quickFind.forwardFrom( 1000_lnum, QStringLiteral( "line 00[0-9]999$" ) );
        REQUIRE( result.hasMatch );
        REQUIRE( result.match.line() == 1999_lnum );
    }

    SECTION( "backwards finds the previous of several matches" )
    {
        const auto result
            = quickFind.backwardFrom( 2999_lnum, QStringLiteral( "line 00[0-9]000$" ) );
        REQUIRE( result.hasMatch );
        REQUIRE( result.match.line() == 2000_lnum );
    }

    SECTION( "without a match forwards or backwards" )
    {
        REQUIRE_FALSE(
            quickFind.forwardFrom( 10_lnum, QStringLiteral( "no such text" ) ).hasMatch );
        REQUIRE_FALSE(
            quickFind.backwardFrom( 3490_lnum, QStringLiteral( "no such text" ) ).hasMatch );
    }
}

SCENARIO( "QuickFind over some Log Lines finds only those, in any block", "[quickfind]" )
{
    const FakeLogData logFile{ logLineTexts() };
    // Every third Log Line: position 1000 holds Log Line 3000.
    const auto everyThirdLine = [ & ]() {
        SearchResultArray lines;
        for ( uint64_t line = 0; line < NbLogLines; line += 3 ) {
            lines.add( line );
        }
        return QuickFindLines::someLogLines( logFile, std::move( lines ) );
    };
    QuickFindRun quickFind( everyThirdLine );

    SECTION( "forwards across a block boundary, over the Log Lines not searched" )
    {
        // From position 1, Log Line 3: the second block starts on Log Line 3003.
        const auto result = quickFind.forwardFrom( 0_lnum, QStringLiteral( "line 00300[1-9]$" ) );
        REQUIRE( result.hasMatch );
        REQUIRE( result.match.line() == 3003_lnum );
    }

    SECTION( "backwards across a block boundary" )
    {
        // Below position 1001, Log Line 3003: the second block holds Log Line 0.
        const auto result
            = quickFind.backwardFrom( 3003_lnum, QStringLiteral( "line 00000[0-2]$" ) );
        REQUIRE( result.hasMatch );
        REQUIRE( result.match.line() == 0_lnum );
    }

    SECTION( "onto the first and the last of them" )
    {
        REQUIRE( quickFind.backwardFrom( 3000_lnum, pattern( 0 ) ).match.line() == 0_lnum );
        REQUIRE( quickFind.forwardFrom( 0_lnum, pattern( 3498 ) ).match.line() == 3498_lnum );
    }

    SECTION( "not onto a Log Line that is not one of them" )
    {
        REQUIRE_FALSE( quickFind.forwardFrom( 0_lnum, pattern( 3001 ) ).hasMatch );
        REQUIRE_FALSE( quickFind.backwardFrom( 3499_lnum, pattern( 1001 ) ).hasMatch );
    }
}

SCENARIO( "QuickFind reads Log Lines in blocks, with a reader attached", "[quickfind]" )
{
    const CountingLogData logFile{ logLineTexts() };
    QuickFindRun quickFind( [ & ]() { return QuickFindLines::everyLogLine( logFile ); } );

    const auto noHit = GENERATE( true, false );
    const auto forward = GENERATE( true, false );
    const auto searchFor
        = noHit ? QStringLiteral( "no such text" ) : pattern( forward ? 3400 : 100 );

    WHEN( "it searches " << ( forward ? "forwards" : "backwards" ) << " across " << NbLogLines
                         << " Log Lines, " << ( noHit ? "without a match" : "to a match" ) )
    {
        const auto result = forward ? quickFind.forwardFrom( 0_lnum, searchFor )
                                    : quickFind.backwardFrom( 3499_lnum, searchFor );
        REQUIRE( result.hasMatch == !noHit );

        THEN( "it reads no Log Line on its own, and a thousand at a time" )
        {
            REQUIRE( logFile.linesReadOneByOne == 0 );
            REQUIRE( logFile.blocksRead >= 1 );
            REQUIRE( logFile.blocksRead <= 4 );
        }

        THEN( "a reader is attached for every read, and detached once it is done" )
        {
            REQUIRE( logFile.readsWithoutReader == 0 );
            REQUIRE( logFile.readers == 0 );
        }
    }
}
