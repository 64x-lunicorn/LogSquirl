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

#include "menuentryname.h"

#include <QChar>
#include <QRegularExpression>

namespace logsquirl::menuentry {

QString withoutMnemonic( const QString& text )
{
    // Written after the name with the space before it, if any: "(&O)".
    static const QRegularExpression mnemonicAfter( QStringLiteral( "\\s*\\(&[^&]\\)" ) );
    auto result = text;
    result.remove( mnemonicAfter );
    // A doubled '&' is the character itself.
    result.replace( QStringLiteral( "&&" ), QStringLiteral( "\x01" ) );
    result.remove( u'&' );
    result.replace( u'\x01', u'&' );
    return result;
}

QString commandName( const QString& text )
{
    auto name = withoutMnemonic( text ).trimmed();
    if ( name.endsWith( QStringLiteral( "..." ) ) ) {
        name.chop( 3 );
    }
    else if ( name.endsWith( QChar( 0x2026 ) ) ) {
        name.chop( 1 );
    }
    return name.trimmed();
}

} // namespace logsquirl::menuentry
