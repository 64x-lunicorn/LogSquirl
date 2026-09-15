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

// Saving from the Filtered View runs off the UI thread over a copy of the
// displayed lines taken when the save starts (#157): Marks added while it
// runs neither race it nor change what it writes.

#include <catch2/catch.hpp>

#include <QBuffer>
#include <QSignalSpy>
#include <QTemporaryFile>

#include "filteredview.h"
#include "linessaver.h"
#include "logdata.h"
#include "logfiltereddata.h"
#include "quickfindpattern.h"
#include "regularexpressionpattern.h"
#include "test_policies.h"
#include "test_utils.h"

namespace {

#if defined( Q_OS_WIN )
const QByteArray LineEnding = "\n";
#else
const QByteArray LineEnding = "\r\n";
#endif

class SavingFilteredView : public FilteredView {
public:
    using FilteredView::FilteredView;
    using FilteredView::linesToSave;
};

// A loaded Log File whose Log Lines read "this is line NNNNNN", with a
// Search matching its even Log Lines.
struct SaveLogFile {
    static constexpr int NbLines = 25003;

    SaveLogFile()
        : policies( testSettingsPolicies() )
        , logData( policies.indexing, policies.search, policies.fileAccess, policies.decoding )
    {
        REQUIRE( file.open() );
        for ( int line = 0; line < NbLines; ++line ) {
            file.write( QStringLiteral( "this is line %1\n" )
                            .arg( line, 6, 10, QLatin1Char( '0' ) )
                            .toLatin1() );
        }
        file.flush();

        SafeQSignalSpy loadEndSpy( &logData, SIGNAL( loadingFinished( LoadingStatus ) ) );
        logData.attachFile( file.fileName() );
        REQUIRE( loadEndSpy.safeWait( 10000 ) );

        filteredData = logData.getNewFilteredData();

        SafeQSignalSpy searchStateSpy{ filteredData.get(), &LogFilteredData::searchStateChanged };
        filteredData->request( RegularExpressionPattern( "this is line [0-9]{5}[02468]" ) );
        REQUIRE( waitUiState( [ & ]() {
            return searchStateSpy.count() > 0
                   && qvariant_cast<SearchSession::State>( searchStateSpy.last().at( 0 ) ).progress
                          >= 100;
        } ) );
        QCoreApplication::processEvents( QEventLoop::AllEvents, 50 );
    }

    SettingsPolicies policies;
    QTemporaryFile file{ "filtered_view_save_test_XXXXXX" };
    LogData logData;
    decltype( logData.getNewFilteredData() ) filteredData;
};

QByteArray utf8File( const logsquirl::vector<QString>& lines )
{
    QByteArray bytes = "\xEF\xBB\xBF";
    for ( const auto& line : lines ) {
        bytes += line.toUtf8() + LineEnding;
    }
    return bytes;
}

} // namespace

SCENARIO( "a save from the Filtered View writes the lines displayed when it started, while Marks "
          "change",
          "[filteredview][linessaver]" )
{
    SaveLogFile logFile;
    QuickFindPattern quickFindPattern;
    SavingFilteredView view( logFile.filteredData.get(), &quickFindPattern, false );

    const auto nbDisplayed = logFile.filteredData->getNbLine();
    REQUIRE( nbDisplayed.get() == 12502 );

    const auto [ begin, end ]
        = GENERATE( std::pair{ 0_lnum, 12502_lnum }, std::pair{ 4321_lnum, 11111_lnum } );

    GIVEN( "a save started from the Filtered View" )
    {
        const auto displayedWhenStarted = logFile.filteredData->getLines( begin, end - begin );
        auto linesToSave = view.linesToSave();

        QBuffer output;
        output.open( QIODevice::WriteOnly );
        AtomicFlag interrupt;
        LinesSaver saver;
        QSignalSpy finished( &saver, &LinesSaver::finished );

        WHEN( "Marks are added on odd Log Lines before and while it runs" )
        {
            logFile.filteredData->addMark( 1_lnum );
            logFile.filteredData->addMark( 4323_lnum );
            auto nextMark = 5001_lnum;
            QObject::connect( &saver, &LinesSaver::progressed, [ & ]( int ) {
                logFile.filteredData->addMark( nextMark );
                nextMark += 2_lcount;
            } );

            saver.save( std::move( linesToSave ), begin, end, nullptr, &output, interrupt );
            REQUIRE( finished.wait( 20000 ) );

            THEN( "the saved file holds the lines displayed when the save started" )
            {
                REQUIRE( nextMark > 5001_lnum );
                REQUIRE( logFile.filteredData->getNbLine() > nbDisplayed );
                REQUIRE( finished.at( 0 ).at( 0 ).toBool() );
                REQUIRE( output.data() == utf8File( displayedWhenStarted ) );
            }
        }
    }
}
