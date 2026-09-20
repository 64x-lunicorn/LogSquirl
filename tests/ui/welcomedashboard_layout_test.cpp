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

#include "theme.h"
#include "welcomedashboard.h"

#include <QApplication>
#include <QFrame>
#include <QImage>
#include <QLabel>
#include <QPushButton>
#include <QRegularExpression>
#include <QTest>

#include <catch2/catch.hpp>

#include <algorithm>
#include <vector>

// The Dashboard uses readable type and shows its lists as cards (#266).

namespace {

std::vector<QFrame*> cards( const WelcomeDashboard& dashboard )
{
    std::vector<QFrame*> result;
    for ( auto* frame : dashboard.findChildren<QFrame*>( QStringLiteral( "dashboardCard" ) ) ) {
        result.push_back( frame );
    }
    std::sort( result.begin(), result.end(), []( const QFrame* lhs, const QFrame* rhs ) {
        return lhs->mapToGlobal( QPoint{} ).y() < rhs->mapToGlobal( QPoint{} ).y();
    } );
    return result;
}

QLabel* cardTitle( const QFrame* card )
{
    return card->findChild<QLabel*>( QStringLiteral( "dashboardCardTitle" ) );
}

} // namespace

SCENARIO( "The Dashboard shows its lists as cards in one column", "[ui][theme]" )
{
    GIVEN( "a Dashboard shown in a wide window" )
    {
        Theme::apply( Theme::LightKey );
        WelcomeDashboard dashboard;
        dashboard.resize( 1200, 900 );
        dashboard.refresh();
        dashboard.show();
        REQUIRE( QTest::qWaitForWindowExposed( &dashboard ) );
        QTest::qWait( 20 );

        THEN( "Recent Files, Favorites and Plugins are cards of one width, directly below one "
              "another" )
        {
            const auto found = cards( dashboard );
            REQUIRE( found.size() == 3 );
            REQUIRE( cardTitle( found[ 0 ] )->text() == QStringLiteral( "Recent Files" ) );
            REQUIRE( cardTitle( found[ 1 ] )->text() == QStringLiteral( "Favorites" ) );
            REQUIRE( cardTitle( found[ 2 ] )->text() == QStringLiteral( "Plugins" ) );

            for ( std::size_t i = 0; i < found.size(); ++i ) {
                INFO( i );
                REQUIRE( found[ i ]->width() == found[ 0 ]->width() );
                REQUIRE( found[ i ]->mapToGlobal( QPoint{} ).x()
                         == found[ 0 ]->mapToGlobal( QPoint{} ).x() );
                REQUIRE( found[ i ]->width() <= 600 );
                if ( i > 0 ) {
                    const auto gap = found[ i ]->mapToGlobal( QPoint{} ).y()
                                     - found[ i - 1 ]->mapToGlobal( QPoint( 0, 0 ) ).y()
                                     - found[ i - 1 ]->height();
                    REQUIRE( gap >= 0 );
                    REQUIRE( gap <= 24 );
                }
            }
        }

        THEN( "no text on the Dashboard has a fixed pixel size, and body text is the "
              "application font" )
        {
            static const QRegularExpression pixelFont( "font-size\\s*:\\s*\\d+px" );
            for ( auto* widget : dashboard.findChildren<QWidget*>() ) {
                INFO( widget->metaObject()->className()
                      << " " << widget->objectName().toStdString() );
                REQUIRE_FALSE( pixelFont.match( widget->styleSheet() ).hasMatch() );
            }
            for ( auto* label : dashboard.findChildren<QLabel*>() ) {
                if ( label->objectName() == QStringLiteral( "dashboardCardTitle" ) ) {
                    continue;
                }
                INFO( label->text().toStdString() );
                REQUIRE( label->font().pointSizeF() >= QApplication::font().pointSizeF() );
            }
        }

        THEN( "a card's title is bold and a quarter larger than the application font" )
        {
            for ( auto* card : cards( dashboard ) ) {
                const auto* title = cardTitle( card );
                REQUIRE( title != nullptr );
                INFO( title->text().toStdString() );
                REQUIRE( title->font().bold() );
                REQUIRE( title->font().pointSizeF()
                         == Approx( QApplication::font().pointSizeF() * 1.25 ) );
            }
        }

        THEN( "Open File is the primary action" )
        {
            QPushButton* openFile = nullptr;
            for ( auto* button : dashboard.findChildren<QPushButton*>() ) {
                if ( button->text() == QStringLiteral( "Open File" ) ) {
                    openFile = button;
                }
            }
            REQUIRE( openFile != nullptr );
            REQUIRE( openFile->property( "primaryAction" ).toBool() );
        }

        Theme::apply( Theme::defaultTheme() );
    }
}

SCENARIO( "The Dashboard's cards take the surface color of every Theme", "[ui][theme]" )
{
    for ( const auto& name : Theme::builtInThemes() ) {
        GIVEN( "a Dashboard shown under " + name.toStdString() )
        {
            Theme::apply( name );
            WelcomeDashboard dashboard;
            dashboard.resize( 1000, 900 );
            dashboard.refresh();
            dashboard.show();
            REQUIRE( QTest::qWaitForWindowExposed( &dashboard ) );
            QTest::qWait( 20 );

            THEN( "every card is filled with the Panel color" )
            {
                const auto panel = Theme::active().color( ColorToken::Panel );
                for ( auto* card : cards( dashboard ) ) {
                    const auto image = card->grab().toImage();
                    // Inside the border and the rounded corner, left of any text.
                    const QColor inside = image.pixelColor( 5, image.height() - 5 );
                    INFO( cardTitle( card )->text().toStdString() );
                    REQUIRE( inside.name() == panel.name() );
                }
            }

            Theme::apply( Theme::defaultTheme() );
        }
    }
}
