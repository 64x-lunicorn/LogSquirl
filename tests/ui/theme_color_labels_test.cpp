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

// The Color Labels follow the Theme (#353, ADR-0006): a Color Label whose
// colors are a built-in Theme's takes the colors of the Theme applied, a
// Color Label whose colors the user chose is kept, and a view open at the
// time paints a labelled word in the new colors at once.
//
// What a view paints is read back with grab(), as the other Theme tests do.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <memory>

#include <QColor>
#include <QCoreApplication>
#include <QImage>
#include <QObject>
#include <QTest>

#include "abstractlogview.h"
#include "fake_log_data.h"
#include "highlighterset.h"
#include "linemapping.h"
#include "logformatdefinition.h"
#include "logtableview.h"
#include "quickfindpattern.h"
#include "test_policies.h"
#include "theme.h"

namespace {

// The Color Labels, restored when this object goes: nothing a test colors
// leaks into the tests that run next, in this executable or the next.
class PinnedColorLabels {
public:
    PinnedColorLabels()
        : colorLabels_( HighlighterSetCollection::get().quickHighlighters() )
    {
    }

    ~PinnedColorLabels()
    {
        auto& collection = HighlighterSetCollection::get();
        collection.setQuickHighlighters( colorLabels_ );
        collection.save();
    }

    PinnedColorLabels( const PinnedColorLabels& ) = delete;
    PinnedColorLabels& operator=( const PinnedColorLabels& ) = delete;

private:
    QList<QuickHighlighter> colorLabels_;
};

// The WCAG 2 contrast ratio of two opaque colors, from 1:1 to 21:1.
double contrastRatio( const QColor& first, const QColor& second )
{
    const auto luminance = []( const QColor& color ) {
        const auto linear = []( int channel ) {
            const auto value = channel / 255.0;
            return value <= 0.04045 ? value / 12.92 : std::pow( ( value + 0.055 ) / 1.055, 2.4 );
        };
        return 0.2126 * linear( color.red() ) + 0.7152 * linear( color.green() )
               + 0.0722 * linear( color.blue() );
    };
    const auto lighter = std::max( luminance( first ), luminance( second ) );
    const auto darker = std::min( luminance( first ), luminance( second ) );
    return ( lighter + 0.05 ) / ( darker + 0.05 );
}

QList<QuickHighlighter> currentColorLabels()
{
    return HighlighterSetCollection::get().quickHighlighters();
}

class LabelledLogView : public AbstractLogView {
public:
    LabelledLogView( const AbstractLogData* logData, const QuickFindPattern* quickFindPattern )
        : AbstractLogView( logData, std::make_unique<EveryLogLine>( logData ), quickFindPattern,
                           false )
    {
    }
};

// A view showing Log Lines with the word "ERROR" in the first Color Label.
struct ViewWithLabelledWord {
    ViewWithLabelledWord()
        : logData( QStringList{ "10:00:00 ERROR disk full", "10:00:01 INFO  all is well",
                                "10:00:02 ERROR disk full" } )
        , view( &logData, &quickFindPattern )
    {
        view.setFrameShape( QFrame::NoFrame );
        view.setVerticalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
        view.setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
        view.resize( 400, 120 );
        view.show();
        QCoreApplication::processEvents();

        view.setPresentationPolicy( testSettingsPolicies().presentation );
        view.updateData();

        std::vector<AbstractLogView::QuickHighlighters> words( ColorLabelCount );
        words[ 0 ] = QStringList{ "ERROR" };
        view.setQuickHighlighters( words );
        QCoreApplication::processEvents();
    }

    QImage grab()
    {
        QCoreApplication::processEvents();
        return view.viewport()->grab().toImage().convertToFormat( QImage::Format_RGB32 );
    }

    FakeLogData logData;
    QuickFindPattern quickFindPattern;
    LabelledLogView view;
};

// The Log Format the Table View shows the same Log Lines through.
LogFormatDefinition labelledWordFormat()
{
    LogFormatDefinition format;
    format.setName( "theme_color_labels_test" );
    format.setTitle( "Color Labels under a Theme" );

    QHash<QString, QString> regex;
    regex[ "basic" ] = R"(^(?<timestamp>\d{2}:\d{2}:\d{2}) (?<body>.*)$)";
    format.setRegexPatterns( regex );
    format.setTimestampField( "timestamp" );
    format.setBodyField( "body" );

    return format;
}

// The same Log Lines in a Table View, with "ERROR" in the first Color Label.
struct TableViewWithLabelledWord {
    TableViewWithLabelledWord()
        : format( labelledWordFormat() )
        , logData( QStringList{ "10:00:00 ERROR disk full", "10:00:01 INFO  all is well",
                                "10:00:02 ERROR disk full" } )
    {
        view.resize( 600, 200 );
        view.setLogFormat( &format, &logData );
        view.updateData( nullptr, false );
        view.setActive( true );
        view.show();
        QTest::qWait( 20 );

        ColorLabelsManager::QuickHighlightersCollection words( ColorLabelCount );
        words[ 0 ] = QStringList{ "ERROR" };
        view.setColorLabels( words );
        QCoreApplication::processEvents();
    }

    QImage grab()
    {
        QCoreApplication::processEvents();
        return view.viewport()->grab().toImage().convertToFormat( QImage::Format_RGB32 );
    }

    LogFormatDefinition format;
    FakeLogData logData;
    LogTableView view;
};

int pixelsOf( const QImage& image, const QColor& color )
{
    int count = 0;
    for ( int y = 0; y < image.height(); ++y ) {
        for ( int x = 0; x < image.width(); ++x ) {
            if ( image.pixelColor( x, y ).rgb() == color.rgb() ) {
                ++count;
            }
        }
    }
    return count;
}

std::array<ColorLabelColors, ColorLabelCount> colorLabelsOf( QLatin1String themeName )
{
    return Theme::fromName( themeName, Qt::ColorScheme::Light ).colorLabels();
}

// Requires that every Color Label has the colors themeName gives it.
void requireColorLabelsOf( QLatin1String themeName )
{
    const auto expected = colorLabelsOf( themeName );
    const auto labels = currentColorLabels();
    REQUIRE( static_cast<std::size_t>( labels.size() ) == ColorLabelCount );
    for ( std::size_t slot = 0; slot < ColorLabelCount; ++slot ) {
        INFO( "Color label " << slot + 1 << " of " << QString( themeName ).toStdString() );
        const auto& color = labels[ static_cast<int>( slot ) ].color;
        REQUIRE( Theme::sameColorLabelColor( color.foreColor, expected[ slot ].foreColor ) );
        REQUIRE( Theme::sameColorLabelColor( color.backColor, expected[ slot ].backColor ) );
    }
}

} // namespace

SCENARIO( "The Color Labels follow the Theme", "[ui][theme][decoration]" )
{
    const PinnedColorLabels pinned;
    QObject context;
    HighlighterSetCollection::followTheme( &context );

    GIVEN( "the Dark Theme, whose Color Labels are the ones LogSquirl has always had" )
    {
        Theme::apply( Theme::DarkKey );
        requireColorLabelsOf( Theme::DarkKey );

        WHEN( "the Smyck Theme is applied" )
        {
            Theme::apply( Theme::SmyckKey );

            THEN( "every Color Label has Smyck's colors" )
            {
                requireColorLabelsOf( Theme::SmyckKey );
            }

            AND_WHEN( "the Light Theme is applied" )
            {
                Theme::apply( Theme::LightKey );

                THEN( "the Color Labels are the ones they were before" )
                {
                    requireColorLabelsOf( Theme::LightKey );
                }
            }

            AND_WHEN( "the Smyck Light Theme is applied" )
            {
                Theme::apply( Theme::SmyckLightKey );

                THEN( "the Color Labels stay Smyck's: both Smyck Themes carry them" )
                {
                    requireColorLabelsOf( Theme::SmyckLightKey );
                    requireColorLabelsOf( Theme::SmyckKey );
                }
            }
        }
    }

    Theme::apply( Theme::defaultTheme() );
}

SCENARIO( "A Color Label the user colored is not touched by a Theme", "[ui][theme][decoration]" )
{
    const PinnedColorLabels pinned;
    QObject context;
    HighlighterSetCollection::followTheme( &context );
    Theme::apply( Theme::DarkKey );

    GIVEN( "a Color Label colored by the user" )
    {
        const HighlightColor chosen{ QColor( "#102030" ), QColor( "#a0b0c0" ) };
        auto& collection = HighlighterSetCollection::getSynced();
        auto labels = collection.quickHighlighters();
        labels[ 2 ].color = chosen;
        collection.setQuickHighlighters( labels );
        collection.save();

        WHEN( "the Smyck Theme is applied" )
        {
            Theme::apply( Theme::SmyckKey );

            THEN( "that Color Label keeps its colors" )
            {
                const auto current = currentColorLabels()[ 2 ].color;
                REQUIRE( current.foreColor == chosen.foreColor );
                REQUIRE( current.backColor == chosen.backColor );
            }

            THEN( "every other Color Label has Smyck's colors" )
            {
                const auto expected = colorLabelsOf( Theme::SmyckKey );
                const auto labelsNow = currentColorLabels();
                for ( std::size_t slot = 0; slot < ColorLabelCount; ++slot ) {
                    if ( slot == 2 ) {
                        continue;
                    }
                    INFO( "Color label " << slot + 1 );
                    REQUIRE( labelsNow[ static_cast<int>( slot ) ].color.backColor
                             == expected[ slot ].backColor );
                }
            }
        }
    }

    Theme::apply( Theme::defaultTheme() );
}

SCENARIO( "Smyck's Color Labels are legible", "[ui][theme][decoration]" )
{
    // Both Smyck Themes carry them, and a Color Label is painted on its own
    // background in either.
    for ( const auto& themeName : { Theme::SmyckKey, Theme::SmyckLightKey } ) {
        INFO( QString( themeName ).toStdString() );
        const auto labels = colorLabelsOf( themeName );
        for ( std::size_t slot = 0; slot < ColorLabelCount; ++slot ) {
            INFO( "Color label " << slot + 1 );
            REQUIRE( labels[ slot ].foreColor.isValid() );
            REQUIRE( labels[ slot ].backColor.isValid() );
            REQUIRE( contrastRatio( labels[ slot ].foreColor, labels[ slot ].backColor ) >= 4.5 );
        }
    }
}

SCENARIO( "A Table View open while the Theme changes paints the new Color Label colors",
          "[ui][theme][decoration]" )
{
    const PinnedColorLabels pinned;
    QObject context;
    HighlighterSetCollection::followTheme( &context );

    GIVEN( "a Table View under the Dark Theme, with a word in the first Color Label" )
    {
        Theme::apply( Theme::DarkKey );
        TableViewWithLabelledWord shown;

        const auto darkLabel = colorLabelsOf( Theme::DarkKey )[ 0 ].backColor;
        const auto smyckLabel = colorLabelsOf( Theme::SmyckKey )[ 0 ].backColor;
        REQUIRE( pixelsOf( shown.grab(), darkLabel ) > 0 );

        WHEN( "the Smyck Theme is applied" )
        {
            Theme::apply( Theme::SmyckKey );

            THEN( "the cells show Smyck's color, without the words being set again" )
            {
                const auto image = shown.grab();
                REQUIRE( pixelsOf( image, smyckLabel ) > 0 );
                REQUIRE( pixelsOf( image, darkLabel ) == 0 );
            }
        }
    }

    Theme::apply( Theme::defaultTheme() );
}

SCENARIO( "A Color Label with no text color of its own keeps it in the settings store",
          "[ui][theme][decoration]" )
{
    const PinnedColorLabels pinned;

    GIVEN( "a Color Label that leaves the Log Line's own text color in place" )
    {
        auto& collection = HighlighterSetCollection::getSynced();
        auto labels = collection.quickHighlighters();
        labels[ 8 ].color = HighlightColor{ QColor{}, QColor( Qt::gray ) };
        collection.setQuickHighlighters( labels );

        WHEN( "it is saved and read again" )
        {
            collection.save();
            const auto stored = HighlighterSetCollection::getSynced().quickHighlighters();

            THEN( "it has no text color of its own, rather than black" )
            {
                REQUIRE_FALSE( stored[ 8 ].color.foreColor.isValid() );
                REQUIRE( stored[ 8 ].color.backColor == QColor( Qt::gray ) );
            }
        }
    }
}

SCENARIO( "A view open while the Theme changes paints the new Color Label colors",
          "[ui][theme][decoration]" )
{
    const PinnedColorLabels pinned;
    QObject context;
    HighlighterSetCollection::followTheme( &context );

    GIVEN( "a view under the Dark Theme, with a word in the first Color Label" )
    {
        Theme::apply( Theme::DarkKey );
        ViewWithLabelledWord shown;

        const auto darkLabel = colorLabelsOf( Theme::DarkKey )[ 0 ].backColor;
        const auto smyckLabel = colorLabelsOf( Theme::SmyckKey )[ 0 ].backColor;
        REQUIRE( pixelsOf( shown.grab(), darkLabel ) > 0 );

        WHEN( "the Smyck Theme is applied" )
        {
            Theme::apply( Theme::SmyckKey );

            THEN( "the labelled word is painted in Smyck's color, without reopening the file" )
            {
                const auto image = shown.grab();
                REQUIRE( pixelsOf( image, smyckLabel ) > 0 );
                REQUIRE( pixelsOf( image, darkLabel ) == 0 );
            }
        }
    }

    Theme::apply( Theme::defaultTheme() );
}
