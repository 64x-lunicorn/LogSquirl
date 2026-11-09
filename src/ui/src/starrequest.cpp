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

#include <QSettings>

#include <algorithm>
#include <limits>

namespace {

// A group no other setting writes to: neither a change of other settings nor
// the settings migration of an update (PersistentInfo::UpdateSettings, which
// removes named keys and the top-level keys of old session settings) touches
// it.
const QString Group = QStringLiteral( "StarRequest" );
const QString FirstStartKey = QStringLiteral( "firstStart" );
const QString StartsKey = QStringLiteral( "starts" );
const QString DismissedKey = QStringLiteral( "dismissed" );

} // namespace

bool starRequestDue( const StarRequestUse& use, QDate today )
{
    if ( use.dismissed || !use.firstStart.isValid() || !today.isValid() ) {
        return false;
    }
    return use.firstStart.daysTo( today ) >= StarRequestMinDays
           && use.starts >= StarRequestMinStarts;
}

void StarRequest::countApplicationStart( QDate today )
{
    auto& request = getSynced();
    if ( !request.use_.firstStart.isValid() ) {
        request.use_.firstStart = today;
    }
    if ( request.use_.starts < std::numeric_limits<int>::max() ) {
        ++request.use_.starts;
    }
    request.save();
}

bool StarRequest::dueOn( QDate today )
{
    return starRequestDue( getSynced().use_, today );
}

void StarRequest::dismissForGood()
{
    auto& request = getSynced();
    request.use_.dismissed = true;
    request.save();
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
