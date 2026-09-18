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

// The font Log Lines are drawn in is the one the view was handed, whatever
// happens to the widget's own font (#354). Qt's stylesheet style replaces a
// widget's font when it polishes it, and a Theme applies a stylesheet to the
// whole application: a view that read the font back from the widget drew its
// Log Lines in the UI font, in rows laid out for another font, so every row
// painted over the descenders of the one above it.

#include "fontutils.h"
#include "logdata.h"
#include "logfiltereddata.h"
#include "logmainview.h"
#include "overview.h"
#include "overviewwidget.h"
#include "quickfindpattern.h"
#include "test_policies.h"
#include "test_utils.h"
#include "theme.h"

#include <QFont>
#include <QImage>
#include <QTemporaryFile>
#include <QTest>

#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch.hpp>

namespace {

constexpr int NbLogLines = 40;

// Descenders in every Log Line: they are what a row drawn too tall paints over.
QString logLineText( int line )
{
    return QStringLiteral( "pygmy jaguar %1" ).arg( line, 4, 10, QLatin1Char( '0' ) );
}

// The part of the Viewport that holds Log Line text: right of the bullet zone
// and the line number area, and above the bottom, so a scrollbar appearing or
// a margin changing cannot decide the comparison. What is painted here is the
// text itself, in whatever font the view draws with.
QImage textArea( const QImage& image )
{
    const auto ratio = image.devicePixelRatio();
    const auto pixels = [ ratio ]( int logical ) { return static_cast<int>( logical * ratio ); };
    return image.copy( pixels( 60 ), 0, pixels( 240 ), pixels( 120 ) );
}

struct FontLogFile {
    FontLogFile()
        : logData( policies.indexing, policies.search, policies.fileAccess, policies.decoding )
    {
        REQUIRE( file.open() );
        for ( int line = 0; line < NbLogLines; ++line ) {
            file.write( logLineText( line ).toLatin1() + '\n' );
        }
        file.flush();

        SafeQSignalSpy loadEndSpy( &logData, SIGNAL( loadingFinished( LoadingStatus ) ) );
        logData.attachFile( file.fileName() );
        REQUIRE( loadEndSpy.safeWait( 10000 ) );

        filteredData = logData.getNewFilteredData();
    }

    SettingsPolicies policies = testSettingsPolicies();
    QTemporaryFile file;
    LogData logData;
    decltype( logData.getNewFilteredData() ) filteredData;
};

} // namespace

SCENARIO( "Log Lines are drawn in the font the view was handed", "[logview][font]" )
{
    // Both grabs happen under the same Theme: applying it again later must
    // change nothing but the repolish it triggers.
    Theme::apply( Theme::defaultTheme() );

    FontLogFile logFile;
    QuickFindPattern quickFindPattern;
    Overview overview;
    OverviewWidget overviewWidget;
    LogMainView view( &logFile.logData, &quickFindPattern, &overview, &overviewWidget, false );
    view.resize( 400, 200 );
    view.show();
    REQUIRE( QTest::qWaitForWindowExposed( &view ) );
    view.updateData();
    QTest::qWait( 50 );

    GIVEN( "a view drawing its Log Lines in a fixed-pitch font" )
    {
        QFont fixedPitch{ FontUtils::platformFixedPitchFamily(), 10 };
        view.updateFont( fixedPitch );
        QTest::qWait( 50 );
        const auto drawnWithItsOwnFont = view.viewport()->grab().toImage();
        REQUIRE_FALSE( drawnWithItsOwnFont.isNull() );

        const auto textBefore = textArea( drawnWithItsOwnFont );
        REQUIRE_FALSE( textBefore.isNull() );

        WHEN( "the widget's font is replaced, as a stylesheet repolish does" )
        {
            // Larger and proportional: painted with, it would make every row
            // taller and run them into one another.
            view.setFont( QFont{ "Helvetica", 18 } );
            // A Theme applies a stylesheet to the application -- what replaces
            // the font in the first place -- and invalidates the paint caches,
            // so the Log Lines really are drawn again.
            Theme::apply( Theme::defaultTheme() );
            QTest::qWait( 50 );

            THEN( "the Log Lines are painted exactly as before" )
            {
                REQUIRE( textArea( view.viewport()->grab().toImage() ) == textBefore );
            }
        }
    }
}
