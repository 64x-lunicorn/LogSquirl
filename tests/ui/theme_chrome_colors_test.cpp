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

// Surfaces that painted fixed colors are drawn in the Tokens of the active
// Theme (#262): the pull-to-follow bar, the chart tooltip, and the marker of
// a conflicting shortcut in the Options Dialog. What they paint is read back
// with grab().

#include <catch2/catch.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>

#include <QCoreApplication>
#include <QImage>
#include <QMouseEvent>
#include <QTabWidget>
#include <QWheelEvent>

#include "abstractlogview.h"
#include "chartseries.h"
#include "chartwidget.h"
#include "configuration.h"
#include "fake_log_data.h"
#include "linemapping.h"
#include "logformatcatalog.h"
#include "optionsdialog.h"
#include "quickfindpattern.h"
#include "recentfiles.h"
#include "savedsearches.h"
#include "shortcuts.h"
#include "test_policies.h"
#include "textviewscrolling.h"
#include "theme.h"
#include "viewportlayout.h"

namespace {

double relativeLuminance( const QColor& color )
{
    const auto linear = []( int channel ) {
        const auto value = channel / 255.0;
        return value <= 0.04045 ? value / 12.92 : std::pow( ( value + 0.055 ) / 1.055, 2.4 );
    };
    return 0.2126 * linear( color.red() ) + 0.7152 * linear( color.green() )
           + 0.0722 * linear( color.blue() );
}

// The WCAG contrast ratio of two colors, from 1 to 21.
double contrast( const QColor& first, const QColor& second )
{
    const auto lighter = std::max( relativeLuminance( first ), relativeLuminance( second ) );
    const auto darker = std::min( relativeLuminance( first ), relativeLuminance( second ) );
    return ( lighter + 0.05 ) / ( darker + 0.05 );
}

// How many pixels of area have each color.
std::map<QRgb, int> colorCounts( const QImage& image, const QRect& area )
{
    std::map<QRgb, int> counts;
    const auto clipped = area.intersected( image.rect() );
    for ( int y = clipped.top(); y <= clipped.bottom(); ++y ) {
        for ( int x = clipped.left(); x <= clipped.right(); ++x ) {
            ++counts[ image.pixel( x, y ) & RGB_MASK ];
        }
    }
    return counts;
}

// The color most pixels of area have.
QColor mostFrequentColor( const QImage& image, const QRect& area )
{
    const auto counts = colorCounts( image, area );
    const auto most
        = std::max_element( counts.begin(), counts.end(),
                            []( const auto& a, const auto& b ) { return a.second < b.second; } );
    return most == counts.end() ? QColor{} : QColor( most->first );
}

// The highest contrast any pixel of area has against background.
double highestContrast( const QImage& image, const QRect& area, const QColor& background )
{
    double highest = 1.0;
    for ( const auto& [ rgb, count ] : colorCounts( image, area ) ) {
        highest = std::max( highest, contrast( QColor( rgb ), background ) );
    }
    return highest;
}

const auto BuiltInThemes = { QString( Theme::LightKey ), QString( Theme::DarkKey ),
                             QString( Theme::HighContrastKey ), QString( Theme::SmyckKey ) };

class PullLogView : public AbstractLogView {
public:
    PullLogView( const AbstractLogData* logData, const QuickFindPattern* quickFindPattern )
        : AbstractLogView( logData, std::make_unique<EveryLogLine>( logData ), quickFindPattern,
                           false )
    {
    }
};

// A view of a Log File shorter than the view, pulled past its bottom until the
// elastic hooks: the pull-to-follow bar is shown at the bottom of the view.
struct PulledView {
    PulledView()
        : logData( QStringList{ "10:00:00 INFO  one", "10:00:01 INFO  two" } )
        , view( &logData, &quickFindPattern )
    {
        view.setFrameShape( QFrame::NoFrame );
        view.setVerticalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
        view.setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
        view.resize( 400, 240 );
        view.show();
        QCoreApplication::processEvents();

        auto presentation = testSettingsPolicies().presentation;
        presentation.allowFollowOnScroll = true;
        view.setPresentationPolicy( presentation );
        view.allowFollowMode( true );
        view.updateData();
        QCoreApplication::processEvents();

        const auto pixels = TextViewScrolling::HookThreshold;
        QWheelEvent pull( QPointF( 200, 120 ), view.viewport()->mapToGlobal( QPointF( 200, 120 ) ),
                          QPoint( 0, -pixels ), QPoint( 0, -pixels ), Qt::NoButton, Qt::NoModifier,
                          Qt::ScrollBegin, false );
        QCoreApplication::sendEvent( view.viewport(), &pull );
        QCoreApplication::processEvents();
    }

    QImage grab()
    {
        return view.viewport()->grab().toImage().convertToFormat( QImage::Format_RGB32 );
    }

    // Where the bar is drawn: the bottom rows of the viewport.
    QRect bar() const
    {
        const auto height = view.viewport()->height();
        return QRect( 0, height - ViewportLayout::PullToFollowHookedHeight,
                      view.viewport()->width(), ViewportLayout::PullToFollowHookedHeight );
    }

    FakeLogData logData;
    QuickFindPattern quickFindPattern;
    PullLogView view;
};

// Requires that the pull-to-follow bar in image is striped in theme's Tokens.
void requireBarInTokens( const QImage& image, const QRect& bar, const Theme& theme )
{
    const auto counts = colorCounts( image, bar );
    const auto stripe = theme.color( ColorToken::PullToFollowStripe ).rgb() & RGB_MASK;
    const auto background = theme.color( ColorToken::Window ).rgb() & RGB_MASK;

    THEN( "the bar is striped in the Theme's stripe color on its window color" )
    {
        REQUIRE( counts.contains( stripe ) );
        REQUIRE( counts.contains( background ) );
        REQUIRE( contrast( QColor( stripe ), QColor( background ) ) >= 3.0 );
    }

    THEN( "no light yellow is left in it" )
    {
        REQUIRE_FALSE( counts.contains( QColor( "lightyellow" ).rgb() & RGB_MASK ) );
    }
}

} // namespace

SCENARIO( "The pull-to-follow bar is drawn in the Tokens of every Theme", "[ui][theme][viewport]" )
{
    for ( const auto& name : BuiltInThemes ) {
        GIVEN( "a view pulled to follow under the " << name.toStdString() << " Theme" )
        {
            Theme::apply( name );
            PulledView pulled;
            REQUIRE( pulled.view.viewportLayout().input().viewportHeightPx > 0 );

            requireBarInTokens( pulled.grab(), pulled.bar(), Theme::active() );
        }
    }

    Theme::apply( Theme::defaultTheme() );
}

SCENARIO( "The pull-to-follow bar follows a Theme switch", "[ui][theme][viewport]" )
{
    GIVEN( "a view pulled to follow under the Light Theme" )
    {
        Theme::apply( Theme::LightKey );
        PulledView pulled;
        const auto lightStripe
            = Theme::active().color( ColorToken::PullToFollowStripe ).rgb() & RGB_MASK;
        REQUIRE( colorCounts( pulled.grab(), pulled.bar() ).contains( lightStripe ) );

        for ( const auto& name : { QString( Theme::DarkKey ), QString( Theme::HighContrastKey ),
                                   QString( Theme::SmyckKey ) } ) {
            WHEN( "the " << name.toStdString() << " Theme is applied" )
            {
                Theme::apply( name );
                QCoreApplication::processEvents();

                requireBarInTokens( pulled.grab(), pulled.bar(), Theme::active() );
            }
        }

        Theme::apply( Theme::defaultTheme() );
    }
}

SCENARIO( "The chart tooltip is drawn in the tooltip Tokens of every Theme", "[ui][theme][chart]" )
{
    for ( const auto& name : BuiltInThemes ) {
        GIVEN( "a chart with one point under the " << name.toStdString() << " Theme" )
        {
            Theme::apply( name );
            const auto& theme = Theme::active();

            ChartWidget chart;
            ChartSeriesDefinition series;
            series.name = "Requests";
            series.color = QColor( "#2196F3" );
            series.points.append( ChartPoint{ LineNumber( 4 ), 100.0, 50.0, {} } );
            chart.setSeriesList( { series }, ChartWidget::Change::Series );
            chart.resize( 400, 300 );
            chart.show();
            QCoreApplication::processEvents();

            WHEN( "the pointer hovers the point" )
            {
                // A single point is fitted into the middle of the plot area,
                // which leaves the chart's axis margins free.
                const QRect plot( 60, 10, chart.width() - 60 - 10, chart.height() - 10 - 30 );
                const auto middle = plot.center();
                QMouseEvent move( QEvent::MouseMove, QPointF( middle ),
                                  chart.mapToGlobal( QPointF( middle ) ), Qt::NoButton,
                                  Qt::NoButton, Qt::NoModifier );
                QCoreApplication::sendEvent( &chart, &move );
                QCoreApplication::processEvents();

                const auto image = chart.grab().toImage().convertToFormat( QImage::Format_RGB32 );
                // The tooltip is drawn above and to the right of the point.
                const QRect tooltip( middle.x() + 12, middle.y() - 26, 40, 18 );

                THEN( "its background is the tooltip base color" )
                {
                    REQUIRE( mostFrequentColor( image, tooltip ).rgb()
                             == theme.color( ColorToken::ToolTipBase ).rgb() );
                }

                THEN( "its text reads against that background" )
                {
                    REQUIRE(
                        highestContrast( image, tooltip, theme.color( ColorToken::ToolTipBase ) )
                        >= 4.5 );
                }
            }
        }
    }

    Theme::apply( Theme::defaultTheme() );
}

SCENARIO( "A conflicting shortcut is marked in the Theme's error colors", "[ui][theme][shortcuts]" )
{
    // The dialog reads these. Synced here, so the scenario does not depend on
    // another test having synced them first.
    SavedSearches::getSynced();
    RecentFiles::getSynced();
    auto& config = Configuration::get();
    const auto storedShortcuts = config.shortcuts();

    for ( const auto& name : BuiltInThemes ) {
        GIVEN( "two actions given the same shortcut, under the " << name.toStdString() << " Theme" )
        {
            Theme::apply( name );
            const auto& theme = Theme::active();

            auto shortcuts = storedShortcuts;
            const QStringList sameKey{ "Ctrl+Alt+Shift+F12" };
            shortcuts[ ShortcutAction::CrawlerEnableRegex ] = sameKey;
            shortcuts[ ShortcutAction::CrawlerEnableInverseMatching ] = sameKey;
            config.setShortcuts( shortcuts );

            WHEN( "the Options Dialog shows its shortcuts" )
            {
                LogFormatCatalog catalog;
                OptionsDialog dialog( catalog );
                auto* table = dialog.shortcutsTable;
                for ( auto* tabs = table->parentWidget(); tabs; tabs = tabs->parentWidget() ) {
                    if ( auto* tabWidget = qobject_cast<QTabWidget*>( tabs->parentWidget() ) ) {
                        tabWidget->setCurrentWidget( tabs );
                        break;
                    }
                }
                dialog.resize( 900, 700 );
                dialog.show();
                QCoreApplication::processEvents();

                std::vector<int> conflictingRows;
                for ( int row = 0; row < table->rowCount(); ++row ) {
                    const auto action
                        = table->item( row, 0 )->data( Qt::UserRole ).toString().toStdString();
                    if ( action == ShortcutAction::CrawlerEnableRegex
                         || action == ShortcutAction::CrawlerEnableInverseMatching ) {
                        conflictingRows.push_back( row );
                    }
                }
                REQUIRE( conflictingRows.size() == 2 );

                THEN( "both conflicting items have the error background and text" )
                {
                    for ( const auto row : conflictingRows ) {
                        INFO( "row " << row );
                        const auto* item = table->item( row, 1 );
                        REQUIRE( item->background().color()
                                 == theme.color( ColorToken::ErrorBackground ) );
                        REQUIRE( item->foreground().color()
                                 == theme.color( ColorToken::ErrorText ) );
                    }
                }

                THEN( "the marked cell shows the error background, and its shortcut reads "
                      "against it" )
                {
                    const auto row = conflictingRows.front();
                    table->scrollToItem( table->item( row, 1 ) );
                    QCoreApplication::processEvents();
                    const auto image = table->viewport()->grab().toImage().convertToFormat(
                        QImage::Format_RGB32 );
                    // The shortcut is shown at the start of the cell.
                    auto cell = table->visualItemRect( table->item( row, 1 ) );
                    cell.setWidth( std::min( cell.width() / 2, 120 ) );
                    const auto errorBackground = theme.color( ColorToken::ErrorBackground );
                    REQUIRE( mostFrequentColor( image, cell ).rgb() == errorBackground.rgb() );
                    REQUIRE( highestContrast( image, cell, errorBackground ) >= 4.5 );
                }
            }
        }
    }

    config.setShortcuts( storedShortcuts );
    Theme::apply( Theme::defaultTheme() );
}
