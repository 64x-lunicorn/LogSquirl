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

// The field reader over a Log Format (#798): what a Field Term reads of a
// Log Line is what the Table View's cells show, the fields named as its
// column headers name them.

#include <catch2/catch_test_macros.hpp>

#include <memory>

#include <QHash>
#include <QString>
#include <QStringList>

#include "logformatdefinition.h"
#include "logformatfieldreader.h"
#include "regularexpression.h"
#include "tablerowcells.h"

namespace {

std::shared_ptr<const LogFormatDefinition> regexFormat( const QString& name = "reader_test" )
{
    auto format = std::make_shared<LogFormatDefinition>();
    format->setName( name );
    format->setTitle( "Field reader test" );
    QHash<QString, QString> regex;
    regex[ "basic" ] = R"(^(?<timestamp>\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3}) )"
                       R"((?<Protocol>\w+) (?<Source>\S+) (?<body>.*)$)";
    format->setRegexPatterns( regex );
    format->setTimestampField( "timestamp" );
    format->setBodyField( "body" );
    // A capture group is a field of the Log Format when declared a value.
    QHash<QString, LogFormatValueDef> values;
    values[ "Protocol" ] = LogFormatValueDef{ "string", false, false };
    values[ "Source" ] = LogFormatValueDef{ "string", false, false };
    format->setValueDefinitions( values );
    return format;
}

std::shared_ptr<const LogFormatDefinition> jsonFormat()
{
    auto format = std::make_shared<LogFormatDefinition>();
    format->setName( "reader_json_test" );
    format->setKind( LogFormatKind::Json );
    format->setTitle( "Field reader JSON test" );
    QHash<QString, LogFormatValueDef> values;
    values[ "Protocol" ] = LogFormatValueDef{ "string", false, false };
    values[ "Source" ] = LogFormatValueDef{ "string", false, false };
    format->setValueDefinitions( values );
    format->setBodyField( "msg" );
    return format;
}

const QString ParsedLine = "2026-01-01 10:00:00.000 DNS 10.0.0.5 query for example.org";
const QString UnparsedLine = "not a log line";

} // namespace

SCENARIO( "The Log Format field reader reads what the Table View's cells show",
          "[logformat][fieldterm]" )
{
    GIVEN( "a reader over a regex Log Format" )
    {
        const auto format = regexFormat();
        const LogFormatFieldReader reader( format );
        const TableRowCells cells( *format );

        THEN( "its fields are the Log Format's, in the order of their columns" )
        {
            REQUIRE( reader.fieldNames() == cells.fieldNames() );
            REQUIRE( reader.fieldIndex( "Protocol" ) == cells.fieldNames().indexOf( "Protocol" ) );
            REQUIRE( reader.fieldIndex( "Source" ).has_value() );
            REQUIRE_FALSE( reader.fieldIndex( "Port" ).has_value() );
            REQUIRE_FALSE( reader.fieldIndex( QString::fromUtf8( "Δt" ) ).has_value() );
        }

        THEN( "a parsed Log Line's fields are the cells' text" )
        {
            QStringList fields;
            REQUIRE( reader.readFields( ParsedLine, fields ) );
            const auto row = cells.rowOf( 0_lnum, ParsedLine,
                                          []( LineNumber ) { return std::optional<QDateTime>{}; } );
            REQUIRE( fields.size() == cells.fieldNames().size() );
            for ( int field = 0; field < fields.size(); ++field ) {
                INFO( "field " << cells.fieldNames()[ field ].toStdString() );
                REQUIRE( fields[ field ] == row.cells[ cells.columnOfField( field ) ] );
            }
            REQUIRE( fields[ *reader.fieldIndex( "Protocol" ) ] == "DNS" );
            REQUIRE( fields[ *reader.fieldIndex( "Source" ) ] == "10.0.0.5" );
        }

        THEN( "a Log Line the Log Format does not parse has no fields" )
        {
            QStringList fields;
            REQUIRE_FALSE( reader.readFields( UnparsedLine, fields ) );
        }

        THEN( "a field's span is where the Log Line shows it" )
        {
            const auto span = reader.fieldSpan( ParsedLine, *reader.fieldIndex( "Source" ) );
            REQUIRE( span.has_value() );
            REQUIRE( ParsedLine.mid( span->first, span->second - span->first ) == "10.0.0.5" );
            REQUIRE_FALSE( reader.fieldSpan( UnparsedLine, 0 ).has_value() );
            REQUIRE_FALSE( reader.fieldSpan( ParsedLine, 99 ).has_value() );
        }

        THEN( "a Field Term Search through it matches by field" )
        {
            RegularExpressionPattern pattern( R"(Protocol:"DNS" and not(Source:"10\.0\.0\.6"))",
                                              true, false, true, false );
            pattern.fieldReader = std::make_shared<LogFormatFieldReader>( format );
            const RegularExpression expression( pattern, RegexpEngine::QRegularExpression );
            REQUIRE( expression.isValid() );
            const auto matcher = expression.createMatcher();
            const auto utf8 = ParsedLine.toUtf8();
            REQUIRE( matcher->hasMatch(
                std::string_view( utf8.constData(), static_cast<size_t>( utf8.size() ) ) ) );
            REQUIRE_FALSE( matcher->hasMatch( "2026-01-01 10:00:00.000 TCP 10.0.0.5 DNS" ) );
        }
    }

    GIVEN( "a reader over a JSON Log Format" )
    {
        const auto format = jsonFormat();
        const LogFormatFieldReader reader( format );

        THEN( "it reads the members by name and places no field in the Log Line" )
        {
            QStringList fields;
            REQUIRE( reader.readFields( R"({"Protocol":"DNS","Source":"10.0.0.5","msg":"q"})",
                                        fields ) );
            REQUIRE( fields[ *reader.fieldIndex( "Protocol" ) ] == "DNS" );
            REQUIRE_FALSE(
                reader.fieldSpan( R"({"Protocol":"DNS"})", *reader.fieldIndex( "Protocol" ) )
                    .has_value() );
        }
    }

    GIVEN( "readers over two Log Formats, and over the same one twice" )
    {
        const auto a = regexFormat( "a" );
        const auto b = regexFormat( "b" );
        auto edited = std::make_shared<LogFormatDefinition>( *a );
        QHash<QString, QString> regex;
        regex[ "basic" ] = R"(^(?<Protocol>\w+) (?<body>.*)$)";
        edited->setRegexPatterns( regex );

        THEN( "the identity tells them apart, and is the same for the same Log Format" )
        {
            REQUIRE( LogFormatFieldReader( a ).identity() == LogFormatFieldReader( a ).identity() );
            REQUIRE( LogFormatFieldReader( a ).identity() != LogFormatFieldReader( b ).identity() );
            REQUIRE( LogFormatFieldReader( a ).identity()
                     != LogFormatFieldReader( edited ).identity() );
        }
    }
}
