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

// The keys the user guide names in its Keyboard commands section are default
// keys of the application (#599). DOCUMENTATION.md is the only source of the
// user documentation and the website shows it (docs/adr/0012), so a key it
// names that the application does not have is a public error.
//
// The first cell of each row of the section's table holds the keys, separated
// by `or` or a comma, written as in vi: a lowercase letter is the letter's key,
// an uppercase one the letter with Shift, `arrows` the four arrow keys. Any
// other key is written as the shortcut settings show it (`Ctrl+Shift+L`).

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include <QFile>
#include <QKeySequence>
#include <QRegularExpression>
#include <QStringList>

#include "shortcuts.h"

namespace {

QString keyboardSection( const QString& documentation )
{
    QString section;
    bool inSection = false;
    for ( const auto& line : documentation.split( '\n' ) ) {
        if ( line.startsWith( "## " ) ) {
            inSection = line.trimmed() == "## Keyboard commands";
            continue;
        }
        if ( inSection ) {
            section += line + '\n';
        }
    }
    return section;
}

// The keys of the first cell of every row of the section's table, as written.
QStringList documentedKeys( const QString& section )
{
    static const QRegularExpression separator( R"(\s+or\s+|,\s+)" );
    static const QRegularExpression escape( R"(\\(.))" );
    static const QRegularExpression ruler( R"(^[-: ]+$)" );

    QStringList keys;
    for ( const auto& line : section.split( '\n' ) ) {
        if ( !line.startsWith( '|' ) ) {
            continue;
        }
        const auto cell = line.section( '|', 1, 1 ).trimmed();
        if ( cell.isEmpty() || cell == "Keys" || ruler.match( cell ).hasMatch() ) {
            continue;
        }
        auto unescaped = cell;
        unescaped.replace( escape, "\\1" );
        keys << unescaped.split( separator, Qt::SkipEmptyParts );
    }
    return keys;
}

QList<QKeySequence> keySequences( const QString& key )
{
    if ( key == "arrows" ) {
        return { QKeySequence( Qt::Key_Up ), QKeySequence( Qt::Key_Down ),
                 QKeySequence( Qt::Key_Left ), QKeySequence( Qt::Key_Right ) };
    }
    if ( key.size() == 1 && key.at( 0 ).isLetter() ) {
        const auto letter = key.toUpper();
        return { QKeySequence( key.at( 0 ).isUpper() ? "Shift+" + letter : letter ) };
    }
    const auto sequence = QKeySequence::fromString( key, QKeySequence::PortableText );
    // What Qt cannot read comes back empty or as something else.
    if ( sequence.isEmpty()
         || sequence.toString( QKeySequence::PortableText ).compare( key, Qt::CaseInsensitive )
                != 0 ) {
        return {};
    }
    return { sequence };
}

QList<QKeySequence> defaultKeys()
{
    QList<QKeySequence> keys;
    for ( const auto& [ action, shortcut ] : ShortcutAction::defaultShortcutList() ) {
        for ( const auto& key : shortcut.keySequence ) {
            // As the application registers them.
            keys << QKeySequence( key );
        }
    }
    return keys;
}

// The keys the section names that are not a default key of the application.
QStringList keysWithoutDefaultBinding( const QString& documentation, qsizetype* checked )
{
    const auto defaults = defaultKeys();
    const auto keys = documentedKeys( keyboardSection( documentation ) );
    *checked = keys.size();

    QStringList missing;
    for ( const auto& key : keys ) {
        const auto sequences = keySequences( key );
        const auto bound = !sequences.isEmpty()
                           && std::all_of( sequences.cbegin(), sequences.cend(),
                                           [ &defaults ]( const QKeySequence& sequence ) {
                                               return defaults.contains( sequence );
                                           } );
        if ( !bound ) {
            missing << key;
        }
    }
    return missing;
}

} // namespace

TEST_CASE( "the user guide names only default keys", "[documentation]" )
{
    QFile file( LOGSQUIRL_DOCUMENTATION_FILE );
    REQUIRE( file.open( QIODevice::ReadOnly | QIODevice::Text ) );
    const auto documentation = QString::fromUtf8( file.readAll() );

    qsizetype checked = 0;
    const auto missing = keysWithoutDefaultBinding( documentation, &checked );

    INFO( "not a default key: " << missing.join( ", " ).toStdString() );
    CHECK( missing.isEmpty() );
    // The section and its table are still found.
    CHECK( checked >= 20 );
}

TEST_CASE( "a key without a default binding is found in the user guide", "[documentation]" )
{
    const auto documentation = QString( R"(# Guide

## Keyboard commands

|Keys            |Actions                              |
|----------------|-------------------------------------|
|j or k          |move the selection                   |
|\[number\] g    |jump to the line number              |
|                |or the first one                     |
|G               |jump to the first line               |
|Shift+G         |jump to the last line                |
|Alt+G           |show jump to line dialog             |
|\[ or \]        |jump to previous or next marked line |
|/ or ,          |search backward                      |

## Mouse navigation

|Keys            |Actions                              |
|----------------|-------------------------------------|
|Alt+Q           |not in the keyboard section          |
)" );

    qsizetype checked = 0;
    const auto missing = keysWithoutDefaultBinding( documentation, &checked );

    CHECK( checked == 10 );
    CHECK( missing == QStringList{ "[number] g", "Alt+G" } );
}
