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

#include "starrequest.h"

#include <QPointer>
#include <QSettings>

#include <algorithm>
#include <limits>
#include <utility>
#include <vector>

namespace {

// A group no other setting writes to: neither a change of other settings nor
// the settings migration of an update (PersistentInfo::updateSettings, which
// removes named keys and the top-level keys of old session settings) touches
// it.
const QString Group = QStringLiteral( "StarRequest" );
const QString FirstStartKey = QStringLiteral( "firstStart" );
const QString StartsKey = QStringLiteral( "starts" );
const QString DismissedKey = QStringLiteral( "dismissed" );

QSettings& store()
{
    return PersistentInfo::getSettings( app_settings{} );
}

struct Hide {
    QPointer<QObject> context;
    std::function<void()> hide;
};

std::vector<Hide>& hides()
{
    static std::vector<Hide> list;
    return list;
}

} // namespace

bool starRequestDue( const StarRequestUse& use, QDate today )
{
    if ( use.dismissed || !use.firstStart.isValid() || !today.isValid() ) {
        return false;
    }
    return use.firstStart.daysTo( today ) >= StarRequestMinDays
           && use.starts >= StarRequestMinStarts;
}

void StarRequest::countStart( QDate today )
{
    if ( !use_.firstStart.isValid() || ( today.isValid() && use_.firstStart > today ) ) {
        use_.firstStart = today;
    }
    if ( use_.starts < std::numeric_limits<int>::max() ) {
        ++use_.starts;
    }

    auto& settings = store();
    settings.beginGroup( Group );
    if ( use_.firstStart.isValid() ) {
        settings.setValue( FirstStartKey, use_.firstStart.toString( Qt::ISODate ) );
    }
    settings.setValue( StartsKey, use_.starts );
    settings.endGroup();
}

void StarRequest::dismiss()
{
    use_.dismissed = true;

    auto& settings = store();
    settings.beginGroup( Group );
    settings.setValue( DismissedKey, true );
    settings.endGroup();
}

void StarRequest::countApplicationStart( QDate today )
{
    getSynced().countStart( today );
}

bool StarRequest::dueOn( QDate today )
{
    return starRequestDue( getSynced().use_, today );
}

void StarRequest::dismissForGood()
{
    getSynced().dismiss();

    auto& list = hides();
    list.erase( std::remove_if( list.begin(), list.end(),
                                []( const Hide& entry ) { return entry.context.isNull(); } ),
                list.end() );
    // A hide may register further hides or destroy other contexts.
    const auto current = list;
    for ( const auto& entry : current ) {
        if ( entry.context ) {
            entry.hide();
        }
    }
}

void StarRequest::whenDismissed( QObject* context, std::function<void()> hide )
{
    hides().push_back( { context, std::move( hide ) } );
}

void StarRequest::saveToStorage( QSettings& settings ) const
{
    settings.beginGroup( Group );
    if ( use_.firstStart.isValid() ) {
        settings.setValue( FirstStartKey, use_.firstStart.toString( Qt::ISODate ) );
    }
    else {
        settings.remove( FirstStartKey );
    }
    settings.setValue( StartsKey, use_.starts );
    settings.setValue( DismissedKey, use_.dismissed );
    settings.endGroup();
}

void StarRequest::retrieveFromStorage( QSettings& settings )
{
    settings.beginGroup( Group );
    use_ = StarRequestUse{
        .firstStart = QDate::fromString( settings.value( FirstStartKey ).toString(), Qt::ISODate ),
        .starts = std::max( 0, settings.value( StartsKey, 0 ).toInt() ),
        .dismissed = settings.value( DismissedKey, false ).toBool(),
    };
    settings.endGroup();
}
