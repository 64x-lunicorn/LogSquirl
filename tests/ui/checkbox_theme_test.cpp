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

// Check boxes show every state distinctly and at one size in every Theme
// (#259): a disabled check box keeps a visible check mark, an indeterminate one
// shows a dash, and the indicator has the same size in every Theme, so the
// Options Dialog's rows do not change height with the Theme.
//
// The indicators are read back with grab(): the indicator is what differs from
// the widget's background, its mark what differs from the indicator's fill.

#include <catch2/catch.hpp>

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

#include <QCheckBox>
#include <QCoreApplication>
#include <QImage>
#include <QTest>
#include <QTreeWidget>

#include "logformatcatalog.h"
#include "optionsdialog.h"
#include "recentfiles.h"
#include "savedsearches.h"
#include "theme.h"

namespace {

// The WCAG 2 contrast ratio of two opaque colors, from 1:1 to 21:1.
double contrastRatio( const QColor& a, const QColor& b )
{
    const auto luminance = []( const QColor& color ) {
        const auto linear = []( float value ) {
            const double channel = static_cast<double>( value );
            return channel <= 0.04045 ? channel / 12.92
                                      : std::pow( ( channel + 0.055 ) / 1.055, 2.4 );
        };
        return 0.2126 * linear( color.redF() ) + 0.7152 * linear( color.greenF() )
               + 0.0722 * linear( color.blueF() );
    };
    const auto first = luminance( a );
    const auto second = luminance( b );
    return ( std::max( first, second ) + 0.05 ) / ( std::min( first, second ) + 0.05 );
}

// The bounding box of the pixels of area whose contrast against background
// exceeds threshold; empty if there are none.
QRect boundsDifferingFrom( const QImage& image, const QRect& area, const QColor& background,
                           double threshold )
{
    QRect bounds;
    for ( int y = area.top(); y <= area.bottom(); ++y ) {
        for ( int x = area.left(); x <= area.right(); ++x ) {
            if ( contrastRatio( image.pixelColor( x, y ), background ) > threshold ) {
                bounds |= QRect( x, y, 1, 1 );
            }
        }
    }
    return bounds;
}

struct Indicator {
    QRect frame;
    QColor fill;
    QRect mark;
    double markContrast = 1.0;
};

// The one indicator drawn on image, which otherwise shows only background.
std::optional<Indicator> findIndicator( const QImage& image, const QColor& background )
{
    Indicator indicator;
    indicator.frame = boundsDifferingFrom( image, image.rect(), background, 1.0 );
    if ( indicator.frame.width() < 8 || indicator.frame.height() < 8 ) {
        return std::nullopt;
    }
    // Inside the 2px border and its rounded corners; the check mark and the
    // dash leave the corners of the icon free.
    const auto inside = indicator.frame.adjusted( 3, 3, -3, -3 );
    indicator.fill = image.pixelColor( inside.topLeft() );
    indicator.mark = boundsDifferingFrom( image, inside, indicator.fill, 1.3 );
    for ( int y = inside.top(); y <= inside.bottom(); ++y ) {
        for ( int x = inside.left(); x <= inside.right(); ++x ) {
            indicator.markContrast = std::max(
                indicator.markContrast, contrastRatio( image.pixelColor( x, y ), indicator.fill ) );
        }
    }
    return indicator;
}

std::optional<Indicator> checkBoxIndicator( Qt::CheckState state, bool enabled )
{
    QWidget window;
    auto* box = new QCheckBox( &window );
    box->setTristate( state == Qt::PartiallyChecked );
    box->setCheckState( state );
    box->setEnabled( enabled );
    box->setGeometry( 0, 0, 40, 40 );
    window.resize( 40, 40 );
    window.show();
    QTest::qWait( 20 );
    const auto image = box->grab().toImage();
    return findIndicator( image, image.pixelColor( image.width() - 1, image.height() - 1 ) );
}

std::optional<Indicator> treeItemIndicator( Qt::CheckState state )
{
    QTreeWidget tree;
    tree.setHeaderHidden( true );
    tree.setRootIsDecorated( false );
    tree.resize( 120, 80 );
    auto* item = new QTreeWidgetItem( &tree );
    item->setFlags( item->flags() | Qt::ItemIsUserCheckable );
    item->setCheckState( 0, state );
    // Neither selected nor current, so only the indicator is drawn on the row.
    tree.setFocusPolicy( Qt::NoFocus );
    tree.setSelectionMode( QAbstractItemView::NoSelection );
    tree.show();
    QTest::qWait( 20 );
    // The start of the row, where the indicator is drawn.
    const auto row = tree.visualItemRect( item );
    const auto image = tree.viewport()->grab().toImage().copy( row.left(), row.top(),
                                                               row.height() + 4, row.height() );
    return findIndicator( image, image.pixelColor( 0, 0 ) );
}

const QStringList& builtInThemes()
{
    static const QStringList themes{ Theme::LightKey, Theme::DarkKey, Theme::HighContrastKey,
                                     Theme::SmyckKey, Theme::SmyckLightKey };
    return themes;
}

} // namespace

SCENARIO( "Check boxes show every state distinctly and at one size in every Theme", "[ui][theme]" )
{
    GIVEN( "each built-in Theme" )
    {
        THEN( "a disabled checked check box shows a check mark that stands out from its fill" )
        {
            for ( const auto& name : builtInThemes() ) {
                Theme::apply( name );
                INFO( name.toStdString() );
                const auto indicator = checkBoxIndicator( Qt::Checked, false );
                REQUIRE( indicator.has_value() );
                REQUIRE( indicator->mark.height() > 4 );
                REQUIRE( indicator->markContrast >= 3.0 );
            }
        }

        THEN( "an indeterminate check box shows a dash, flat unlike the check mark" )
        {
            for ( const auto& name : builtInThemes() ) {
                Theme::apply( name );
                for ( const auto enabled : { true, false } ) {
                    INFO( name.toStdString() << ( enabled ? " enabled" : " disabled" ) );
                    const auto checked = checkBoxIndicator( Qt::Checked, enabled );
                    const auto indeterminate = checkBoxIndicator( Qt::PartiallyChecked, enabled );
                    REQUIRE( checked.has_value() );
                    REQUIRE( indeterminate.has_value() );
                    REQUIRE( checked->mark.height() > 4 );
                    REQUIRE( indeterminate->mark.width() >= 6 );
                    REQUIRE( indeterminate->mark.height() <= 4 );
                    REQUIRE( indeterminate->markContrast >= 3.0 );
                }
            }
        }

        THEN( "a partly checked tree item shows a dash, flat unlike a checked one's check mark" )
        {
            for ( const auto& name : builtInThemes() ) {
                Theme::apply( name );
                INFO( name.toStdString() );
                const auto checked = treeItemIndicator( Qt::Checked );
                const auto indeterminate = treeItemIndicator( Qt::PartiallyChecked );
                REQUIRE( checked.has_value() );
                REQUIRE( indeterminate.has_value() );
                REQUIRE( checked->mark.height() > 4 );
                REQUIRE( indeterminate->mark.width() >= 6 );
                REQUIRE( indeterminate->mark.height() <= 4 );
            }
        }

        THEN( "check boxes and tree items draw their indicator at the Dark Theme's size" )
        {
            Theme::apply( Theme::DarkKey );
            const auto darkBox = checkBoxIndicator( Qt::Unchecked, true );
            const auto darkTree = treeItemIndicator( Qt::Unchecked );
            REQUIRE( darkBox.has_value() );
            REQUIRE( darkTree.has_value() );
            // 16px inside a 2px border.
            REQUIRE( darkBox->frame.size() == QSize( 20, 20 ) );

            for ( const auto& name : builtInThemes() ) {
                Theme::apply( name );
                INFO( name.toStdString() );
                const auto box = checkBoxIndicator( Qt::Unchecked, true );
                const auto tree = treeItemIndicator( Qt::Unchecked );
                REQUIRE( box.has_value() );
                REQUIRE( tree.has_value() );
                REQUIRE( box->frame.size() == darkBox->frame.size() );
                REQUIRE( tree->frame.size() == darkTree->frame.size() );
            }
        }
    }

    GIVEN( "an Options Dialog built under each built-in Theme" )
    {
        // The dialog reads these. Synced here, so the scenario does not depend
        // on another test having synced them first.
        SavedSearches::getSynced();
        RecentFiles::getSynced();
        LogFormatCatalog catalog;

        // High Contrast draws its frames 2px wide, which the dialog's group
        // boxes and tabs add to its height; its check box rows match.
        const auto checkBoxHeights = [ &catalog ]( const QString& name ) {
            Theme::apply( name );
            OptionsDialog dialog( catalog );
            std::vector<int> heights;
            for ( const auto* box : dialog.findChildren<QCheckBox*>() ) {
                heights.push_back( box->sizeHint().height() );
            }
            return heights;
        };

        THEN( "its check box rows have the same heights in every Theme" )
        {
            const auto dark = checkBoxHeights( Theme::DarkKey );
            REQUIRE_FALSE( dark.empty() );
            for ( const auto& name : builtInThemes() ) {
                INFO( name.toStdString() );
                REQUIRE( checkBoxHeights( name ) == dark );
            }
        }

        THEN( "it asks for the same height in the Light and the Dark Theme" )
        {
            Theme::apply( Theme::DarkKey );
            const auto darkHeight = OptionsDialog( catalog ).sizeHint().height();
            Theme::apply( Theme::LightKey );
            REQUIRE( OptionsDialog( catalog ).sizeHint().height() == darkHeight );
        }
    }

    Theme::apply( Theme::defaultTheme() );
}
