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

#include "viewstatecodec.h"

#include "log.h"
#include "settingspolicies.h"

#include <QJsonDocument>
#include <QRegularExpression>
#include <QVariantList>
#include <QVariantMap>

namespace {

// The format glogg wrote: "S<main>:<filtered>:IC<0|1>:AR<0|1>:FF<0|1>". It
// holds neither the regexp, inverse and combination flags, nor Marks or a
// chart.
ViewState decodeLegacy( const QString& string, bool useRegexpByPolicy )
{
    ViewState state;

    QRegularExpression regex( "S(\\d+):(\\d+)" );
    QRegularExpressionMatch match = regex.match( string );
    if ( match.hasMatch() ) {
        state.sizes = { match.captured( 1 ).toInt(), match.captured( 2 ).toInt() };
        LOG_DEBUG << "sizes: " << state.sizes[ 0 ] << " " << state.sizes[ 1 ];
    }
    else {
        LOG_WARNING << "Unrecognised view size: " << string.toLocal8Bit().data();

        // Default values;
        state.sizes = { 400, 100 };
    }

    QRegularExpression caseRefreshRegex( "IC(\\d+):AR(\\d+)" );
    match = caseRefreshRegex.match( string );
    if ( match.hasMatch() ) {
        state.ignoreCase = ( match.captured( 1 ).toInt() == 1 );
        state.autoRefresh = ( match.captured( 2 ).toInt() == 1 );

        LOG_DEBUG << "ignore_case: " << state.ignoreCase << " auto_refresh: " << state.autoRefresh;
    }
    else {
        LOG_WARNING << "Unrecognised case/refresh: " << string.toLocal8Bit().data();
    }

    QRegularExpression followRegex( "AR(\\d+):FF(\\d+)" );
    match = followRegex.match( string );
    if ( match.hasMatch() ) {
        state.followFile = ( match.captured( 2 ).toInt() == 1 );

        LOG_DEBUG << "follow_file: " << state.followFile;
    }
    else {
        LOG_WARNING << "Unrecognised follow file " << string.toLocal8Bit().data();
    }

    state.useRegexp = useRegexpByPolicy;
    return state;
}

// The current format: a JSON object, one key per field. Anything that does
// not parse reads as an empty object.
ViewState decodeJson( const QString& json, bool useRegexpByPolicy )
{
    ViewState state;

    const auto properties = QJsonDocument::fromJson( json.toLatin1() ).toVariant().toMap();

    if ( properties.contains( "S" ) ) {
        const auto sizes = properties.value( "S" ).toList();
        for ( const auto& s : sizes ) {
            state.sizes.append( s.toInt() );
        }
    }

    state.ignoreCase = properties.value( "IC" ).toBool();
    state.autoRefresh = properties.value( "AR" ).toBool();
    state.followFile = properties.value( "FF" ).toBool();
    if ( properties.contains( "RE" ) ) {
        state.useRegexp = properties.value( "RE" ).toBool();
    }
    else {
        state.useRegexp = useRegexpByPolicy;
    }

    state.inverseRegexp = properties.value( "IR" ).toBool();
    state.useBooleanCombination = properties.value( "BC" ).toBool();

    if ( properties.contains( "M" ) ) {
        const auto marks = properties.value( "M" ).toList();
        for ( const auto& m : marks ) {
            state.marks.append( m.toUInt() );
        }
    }

    // The chart series are a JSON array held as a string inside the object.
    if ( properties.contains( "CS" ) ) {
        state.chartSeries
            = QJsonDocument::fromJson( properties.value( "CS" ).toString().toUtf8() ).array();
    }
    state.chartVisible = properties.value( "CV" ).toBool();

    return state;
}

} // namespace

QString encodeViewState( const ViewState& state )
{
    const auto toVariantList = []( const auto& list ) -> QVariantList {
        QVariantList variantList;
        for ( const auto& item : list ) {
            variantList.append( static_cast<qulonglong>( item ) );
        }
        return variantList;
    };

    QVariantMap properties;

    properties[ "S" ] = toVariantList( state.sizes );
    properties[ "IC" ] = state.ignoreCase;
    properties[ "AR" ] = state.autoRefresh;
    properties[ "FF" ] = state.followFile;
    properties[ "RE" ] = state.useRegexp;
    properties[ "IR" ] = state.inverseRegexp;
    properties[ "BC" ] = state.useBooleanCombination;
    properties[ "M" ] = toVariantList( state.marks );

    if ( !state.chartSeries.isEmpty() ) {
        properties[ "CS" ] = QString::fromUtf8(
            QJsonDocument( state.chartSeries ).toJson( QJsonDocument::Compact ) );
    }
    properties[ "CV" ] = state.chartVisible;

    return QJsonDocument::fromVariant( properties ).toJson( QJsonDocument::Compact );
}

ViewState decodeViewState( const QString& encoded, const QuickFindPolicy& quickFindPolicy )
{
    const auto useRegexpByPolicy
        = quickFindPolicy.mainRegexpType == SearchRegexpType::ExtendedRegexp;

    if ( encoded.startsWith( '{' ) ) {
        return decodeJson( encoded, useRegexpByPolicy );
    }
    else {
        return decodeLegacy( encoded, useRegexpByPolicy );
    }
}
