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

#include <QString>

#include <catch2/catch_test_macros.hpp>

// A menu entry's text reads outside its menu without its mnemonic, in every
// way a translation writes one (#937).

using logsquirl::menuentry::commandName;
using logsquirl::menuentry::withoutMnemonic;

TEST_CASE( "A menu entry's text without its mnemonic", "[ui][menuentry][palette]" )
{
    CHECK( withoutMnemonic( QStringLiteral( "&Open..." ) ) == QStringLiteral( "Open..." ) );
    // As a Chinese entry writes its mnemonic, after the name.
    CHECK( withoutMnemonic( QStringLiteral( "关闭(&C)" ) ) == QStringLiteral( "关闭" ) );
    CHECK( withoutMnemonic( QStringLiteral( "打开(&O)..." ) ) == QStringLiteral( "打开..." ) );
    // A doubled '&' is the character itself.
    CHECK( withoutMnemonic( QStringLiteral( "Save && &Reload" ) )
           == QStringLiteral( "Save & Reload" ) );
    CHECK( withoutMnemonic( QStringLiteral( "Copy (&&C)" ) ) == QStringLiteral( "Copy (&C)" ) );
    CHECK( withoutMnemonic( QStringLiteral( "Plain text" ) ) == QStringLiteral( "Plain text" ) );
    CHECK( withoutMnemonic( QString{} ).isEmpty() );
}

TEST_CASE( "A menu entry's command name", "[ui][menuentry][palette]" )
{
    CHECK( commandName( QStringLiteral( "&Open..." ) ) == QStringLiteral( "Open" ) );
    CHECK( commandName( QStringLiteral( "&Find…" ) ) == QStringLiteral( "Find" ) );
    CHECK( commandName( QStringLiteral( "关闭(&C)" ) ) == QStringLiteral( "关闭" ) );
    CHECK( commandName( QStringLiteral( "打开(&O)..." ) ) == QStringLiteral( "打开" ) );
    CHECK( commandName( QStringLiteral( "Save && &Reload" ) )
           == QStringLiteral( "Save & Reload" ) );
    CHECK( commandName( QStringLiteral( "Plain text" ) ) == QStringLiteral( "Plain text" ) );
    // Only trailing dots are those of a dialog.
    CHECK( commandName( QStringLiteral( "Wait... and see" ) )
           == QStringLiteral( "Wait... and see" ) );
}
