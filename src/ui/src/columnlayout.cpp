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

#include "columnlayout.h"

#include <algorithm>
#include <iterator>
#include <numeric>

#include <QSettings>

#include "logformatdefinition.h"
#include "persistentinfo.h"
#include "tablerowcells.h"

ColumnLayout ColumnLayout::defaultsFor( int columnCount )
{
    const auto count = static_cast<size_t>( std::max( columnCount, 0 ) );
    ColumnLayout layout;
    layout.order.resize( count );
    std::iota( layout.order.begin(), layout.order.end(), 0 );
    layout.hidden.assign( count, false );
    layout.widths.assign( count, 0 );
    return layout;
}

int ColumnLayout::columnCount() const
{
    return static_cast<int>( order.size() );
}

bool ColumnLayout::isHidden( int column ) const
{
    return column >= 0 && static_cast<size_t>( column ) < hidden.size()
           && hidden[ static_cast<size_t>( column ) ];
}

std::vector<int> ColumnLayout::shownColumns() const
{
    std::vector<int> shown;
    std::ranges::copy_if( order, std::back_inserter( shown ),
                          [ this ]( int column ) { return !isHidden( column ); } );
    return shown;
}

namespace {

// A Log Format name safe for use as a QSettings group key.
QString sanitizedFormatName( const QString& name )
{
    QString safe = name;
    safe.replace( '/', '_' );
    safe.replace( '\\', '_' );
    return safe;
}

constexpr QLatin1Char ListSeparator( '|' );

QString joined( const std::vector<int>& columns )
{
    QStringList texts;
    texts.reserve( static_cast<qsizetype>( columns.size() ) );
    for ( const auto column : columns ) {
        texts << QString::number( column );
    }
    return texts.join( ListSeparator );
}

// The columns a kept list names, each one of columnCount; none when the list
// names anything else.
std::optional<std::vector<int>> columnsNamed( const QString& text, int columnCount )
{
    std::vector<int> columns;
    if ( text.isEmpty() ) {
        return columns;
    }
    for ( const auto& part : text.split( ListSeparator ) ) {
        bool ok = false;
        const int column = part.toInt( &ok );
        if ( !ok || column < 0 || column >= columnCount ) {
            return std::nullopt;
        }
        columns.push_back( column );
    }
    return columns;
}

bool isPermutation( const std::vector<int>& order, int columnCount )
{
    if ( order.size() != static_cast<size_t>( columnCount ) ) {
        return false;
    }
    std::vector<bool> seen( order.size(), false );
    for ( const auto column : order ) {
        if ( seen[ static_cast<size_t>( column ) ] ) {
            return false;
        }
        seen[ static_cast<size_t>( column ) ] = true;
    }
    return true;
}

// Whether the settings of the current group were kept for columns named
// columnNames.
bool fingerprintMatches( const QSettings& settings, const QStringList& columnNames )
{
    return settings.value( "_columnCount", -1 ).toInt() == columnNames.size()
           && settings.value( "_columnNames" ).toString() == columnNames.join( ListSeparator );
}

// The settings group of a Log Format's layout: the one that held the widths
// before the layout joined them.
QString columnLayoutSettingsGroup( const LogFormatDefinition& format )
{
    return "logformat/columns/" + sanitizedFormatName( format.name() );
}

} // namespace

std::optional<ColumnLayout> loadColumnLayout( const LogFormatDefinition& format,
                                              const QStringList& columnNames )
{
    auto& settings = PersistentInfo::getSettings( app_settings{} );
    settings.beginGroup( columnLayoutSettingsGroup( format ) );
    std::optional<ColumnLayout> layout;
    if ( fingerprintMatches( settings, columnNames ) ) {
        const auto columnCount = static_cast<int>( columnNames.size() );
        layout = ColumnLayout::defaultsFor( columnCount );
        for ( int column = 0; column < columnCount; ++column ) {
            layout->widths[ static_cast<size_t>( column ) ]
                = std::max( 0, settings.value( QString::number( column ), 0 ).toInt() );
        }
        const auto order = columnsNamed( settings.value( "_order" ).toString(), columnCount );
        if ( order && isPermutation( *order, columnCount ) ) {
            layout->order = *order;
        }
        const auto hidden = columnsNamed( settings.value( "_hidden" ).toString(), columnCount );
        // One column always stays shown: a layout hiding every one hides none.
        if ( hidden && static_cast<int>( hidden->size() ) < columnCount ) {
            for ( const auto column : *hidden ) {
                layout->hidden[ static_cast<size_t>( column ) ] = true;
            }
        }
    }
    settings.endGroup();
    return layout;
}

void storeColumnLayout( const LogFormatDefinition& format, const QStringList& columnNames,
                        const ColumnLayout& layout )
{
    const auto columnCount = static_cast<int>( columnNames.size() );
    if ( layout.columnCount() != columnCount
         || layout.hidden.size() != static_cast<size_t>( columnCount )
         || layout.widths.size() != static_cast<size_t>( columnCount ) ) {
        return;
    }

    auto& settings = PersistentInfo::getSettings( app_settings{} );
    settings.beginGroup( columnLayoutSettingsGroup( format ) );
    // Widths kept for other columns of the Log Format are not its any more.
    if ( !fingerprintMatches( settings, columnNames ) ) {
        settings.remove( "" );
    }
    settings.setValue( "_columnCount", columnCount );
    settings.setValue( "_columnNames", columnNames.join( ListSeparator ) );
    std::vector<int> hidden;
    for ( int column = 0; column < columnCount; ++column ) {
        const auto index = static_cast<size_t>( column );
        if ( layout.widths[ index ] > 0 ) {
            settings.setValue( QString::number( column ), layout.widths[ index ] );
        }
        if ( layout.hidden[ index ] ) {
            hidden.push_back( column );
        }
    }
    settings.setValue( "_order", joined( layout.order ) );
    settings.setValue( "_hidden", joined( hidden ) );
    settings.endGroup();
}

void dropColumnLayout( const LogFormatDefinition& format )
{
    PersistentInfo::getSettings( app_settings{} ).remove( columnLayoutSettingsGroup( format ) );
}

ColumnLayout columnLayoutOf( const LogFormatDefinition& format )
{
    const auto columnNames = TableRowCells( format ).columnNames();
    return loadColumnLayout( format, columnNames )
        .value_or( ColumnLayout::defaultsFor( static_cast<int>( columnNames.size() ) ) );
}
