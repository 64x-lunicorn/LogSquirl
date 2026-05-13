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

#include <catch2/catch.hpp>

#include "charttemplategenerator.h"
#include "logformatdefinition.h"

#include <QRegularExpression>

// ---------------------------------------------------------------------------
// Helper: build a LogFormatDefinition that resembles a typical Android logcat
// format for reuse across tests.
// ---------------------------------------------------------------------------
namespace {

LogFormatDefinition makeAndroidFormat()
{
    LogFormatDefinition fmt;
    fmt.setName( "android_log" );
    fmt.setTitle( "Android Log" );

    // A single regex pattern with named capture groups.
    QHash<QString, QString> patterns;
    patterns[ "basic" ]
        = R"(^(?<timestamp>\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\s+)"
          R"((?<pid>\d+)\s+(?<tid>\d+)\s+(?<level>[VDIWEFS])\s+)"
          R"((?<tag>[^:]+):\s+(?<body>.*)$)";
    fmt.setRegexPatterns( patterns );

    fmt.setTimestampField( "timestamp" );
    fmt.setTimestampFormats( { "%m-%d %H:%M:%S.%L" } );

    fmt.setLevelField( "level" );
    QHash<QString, QString> levels;
    levels[ "error" ] = "E";
    levels[ "warning" ] = "W";
    levels[ "info" ] = "I";
    levels[ "debug" ] = "D";
    levels[ "trace" ] = "V";
    fmt.setLevelMappings( levels );

    QHash<QString, LogFormatValueDef> valueDefs;
    valueDefs[ "timestamp" ] = { "string", false, false };
    valueDefs[ "pid" ] = { "integer", true, false };
    valueDefs[ "tid" ] = { "integer", true, false };
    valueDefs[ "level" ] = { "string", false, false };
    valueDefs[ "tag" ] = { "string", true, false };
    valueDefs[ "body" ] = { "string", false, false };
    fmt.setValueDefinitions( valueDefs );
    fmt.setValueFieldOrder( { "timestamp", "pid", "tid", "level", "tag", "body" } );

    return fmt;
}

LogFormatDefinition makeMinimalFormat()
{
    LogFormatDefinition fmt;
    fmt.setName( "minimal" );
    fmt.setTitle( "Minimal" );

    QHash<QString, QString> patterns;
    patterns[ "basic" ] = R"(^(?<body>.*)$)";
    fmt.setRegexPatterns( patterns );

    QHash<QString, LogFormatValueDef> valueDefs;
    valueDefs[ "body" ] = { "string", false, false };
    fmt.setValueDefinitions( valueDefs );
    fmt.setValueFieldOrder( { "body" } );

    return fmt;
}

} // namespace

// ===========================================================================
// strftimeToQtFormat
// ===========================================================================

SCENARIO( "strftimeToQtFormat converts strftime specifiers to Qt format",
          "[charttemplategenerator]" )
{
    GIVEN( "A full date-time format string" )
    {
        THEN( "%Y-%m-%d %H:%M:%S.%L becomes yyyy-MM-dd HH:mm:ss.zzz" )
        {
            REQUIRE( ChartTemplateGenerator::strftimeToQtFormat( "%Y-%m-%d %H:%M:%S.%L" )
                     == "yyyy-MM-dd HH:mm:ss.zzz" );
        }
    }

    GIVEN( "A date-only format" )
    {
        THEN( "%Y-%m-%d becomes yyyy-MM-dd" )
        {
            REQUIRE( ChartTemplateGenerator::strftimeToQtFormat( "%Y-%m-%d" )
                     == "yyyy-MM-dd" );
        }
    }

    GIVEN( "A time-only format" )
    {
        THEN( "%H:%M:%S becomes HH:mm:ss" )
        {
            REQUIRE( ChartTemplateGenerator::strftimeToQtFormat( "%H:%M:%S" )
                     == "HH:mm:ss" );
        }
    }

    GIVEN( "A short year format" )
    {
        THEN( "%y-%m-%d becomes yy-MM-dd" )
        {
            REQUIRE( ChartTemplateGenerator::strftimeToQtFormat( "%y-%m-%d" )
                     == "yy-MM-dd" );
        }
    }

    GIVEN( "Milliseconds via %L" )
    {
        THEN( "%S.%L becomes ss.zzz" )
        {
            REQUIRE( ChartTemplateGenerator::strftimeToQtFormat( "%S.%L" )
                     == "ss.zzz" );
        }
    }

    GIVEN( "Microseconds via %f" )
    {
        THEN( "%S.%f becomes ss.zzz (precision loss accepted)" )
        {
            REQUIRE( ChartTemplateGenerator::strftimeToQtFormat( "%S.%f" )
                     == "ss.zzz" );
        }
    }

    GIVEN( "Month name via %b" )
    {
        THEN( "%b %d becomes MMM dd" )
        {
            REQUIRE( ChartTemplateGenerator::strftimeToQtFormat( "%b %d" )
                     == "MMM dd" );
        }
    }

    GIVEN( "Full month name via %B" )
    {
        THEN( "%B is also converted to MMM" )
        {
            REQUIRE( ChartTemplateGenerator::strftimeToQtFormat( "%B" ) == "MMM" );
        }
    }

    GIVEN( "AM/PM format with 12-hour clock" )
    {
        THEN( "%I:%M:%S %p becomes hh:mm:ss AP" )
        {
            REQUIRE( ChartTemplateGenerator::strftimeToQtFormat( "%I:%M:%S %p" )
                     == "hh:mm:ss AP" );
        }
    }

    GIVEN( "Space-padded day %e" )
    {
        THEN( "%e becomes d" )
        {
            REQUIRE( ChartTemplateGenerator::strftimeToQtFormat( "%e" ) == "d" );
        }
    }

    GIVEN( "Timezone offset %z" )
    {
        THEN( "%z becomes t" )
        {
            REQUIRE( ChartTemplateGenerator::strftimeToQtFormat( "%H:%M:%S%z" )
                     == "HH:mm:sst" );
        }
    }

    GIVEN( "Escaped percent %%" )
    {
        THEN( "%% becomes a literal %" )
        {
            REQUIRE( ChartTemplateGenerator::strftimeToQtFormat( "%H%%:%M" )
                     == "HH%:mm" );
        }
    }

    GIVEN( "Epoch-only format %s" )
    {
        THEN( "Returns empty string" )
        {
            REQUIRE( ChartTemplateGenerator::strftimeToQtFormat( "%s" ).isEmpty() );
        }
    }

    GIVEN( "Format containing %s (not epoch-only)" )
    {
        THEN( "Returns empty string because epoch cannot be parsed" )
        {
            REQUIRE(
                ChartTemplateGenerator::strftimeToQtFormat( "%Y-%m-%d %s" ).isEmpty() );
        }
    }

    GIVEN( "An empty format string" )
    {
        THEN( "Returns empty string" )
        {
            REQUIRE( ChartTemplateGenerator::strftimeToQtFormat( "" ).isEmpty() );
        }
    }

    GIVEN( "A format with no specifiers (literal text only)" )
    {
        THEN( "Returns the literal text unchanged" )
        {
            REQUIRE( ChartTemplateGenerator::strftimeToQtFormat( "hello" ) == "hello" );
        }
    }

    GIVEN( "Timezone name %Z" )
    {
        THEN( "%Z is silently skipped" )
        {
            REQUIRE( ChartTemplateGenerator::strftimeToQtFormat( "%H:%M %Z" )
                     == "HH:mm " );
        }
    }

    GIVEN( "An Android-style timestamp format" )
    {
        THEN( "%m-%d %H:%M:%S.%L converts correctly" )
        {
            REQUIRE( ChartTemplateGenerator::strftimeToQtFormat( "%m-%d %H:%M:%S.%L" )
                     == "MM-dd HH:mm:ss.zzz" );
        }
    }
}

// ===========================================================================
// patternContainingGroup
// ===========================================================================

SCENARIO( "patternContainingGroup finds the correct regex pattern",
          "[charttemplategenerator]" )
{
    GIVEN( "A format with a pattern containing the requested named group" )
    {
        const auto fmt = makeAndroidFormat();

        THEN( "The pattern containing 'timestamp' is returned" )
        {
            const auto result
                = ChartTemplateGenerator::patternContainingGroup( fmt, "timestamp" );
            REQUIRE_FALSE( result.isEmpty() );
            REQUIRE( result.contains( "(?<timestamp>" ) );
        }

        THEN( "The pattern containing 'pid' is returned" )
        {
            const auto result
                = ChartTemplateGenerator::patternContainingGroup( fmt, "pid" );
            REQUIRE_FALSE( result.isEmpty() );
            REQUIRE( result.contains( "(?<pid>" ) );
        }
    }

    GIVEN( "A format that does not contain the requested group" )
    {
        const auto fmt = makeAndroidFormat();

        THEN( "Empty string is returned for a non-existent group" )
        {
            REQUIRE(
                ChartTemplateGenerator::patternContainingGroup( fmt, "nonexistent" )
                    .isEmpty() );
        }
    }

    GIVEN( "A format with empty regex patterns" )
    {
        LogFormatDefinition emptyFmt;
        emptyFmt.setRegexPatterns( {} );

        THEN( "Empty string is returned" )
        {
            REQUIRE(
                ChartTemplateGenerator::patternContainingGroup( emptyFmt, "body" )
                    .isEmpty() );
        }
    }

    GIVEN( "A format with multiple patterns, only one containing the group" )
    {
        LogFormatDefinition fmt;
        QHash<QString, QString> patterns;
        patterns[ "first" ] = R"(^(?<fieldA>\d+)$)";
        patterns[ "second" ] = R"(^(?<fieldA>\d+)\s+(?<fieldB>\w+)$)";
        fmt.setRegexPatterns( patterns );

        THEN( "A pattern containing 'fieldB' is found" )
        {
            const auto result
                = ChartTemplateGenerator::patternContainingGroup( fmt, "fieldB" );
            REQUIRE_FALSE( result.isEmpty() );
            REQUIRE( result.contains( "(?<fieldB>" ) );
        }
    }
}

// ===========================================================================
// namedGroupIndex
// ===========================================================================

SCENARIO( "namedGroupIndex resolves named capture group to numeric index",
          "[charttemplategenerator]" )
{
    GIVEN( "A pattern with multiple named groups" )
    {
        const QString pattern
            = R"(^(?<timestamp>\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\s+)"
              R"((?<pid>\d+)\s+(?<tid>\d+)\s+(?<level>[VDIWEFS]))";

        THEN( "timestamp is group 1" )
        {
            REQUIRE( ChartTemplateGenerator::namedGroupIndex( pattern, "timestamp" )
                     == 1 );
        }

        THEN( "pid is group 2" )
        {
            REQUIRE( ChartTemplateGenerator::namedGroupIndex( pattern, "pid" ) == 2 );
        }

        THEN( "tid is group 3" )
        {
            REQUIRE( ChartTemplateGenerator::namedGroupIndex( pattern, "tid" ) == 3 );
        }

        THEN( "level is group 4" )
        {
            REQUIRE( ChartTemplateGenerator::namedGroupIndex( pattern, "level" )
                     == 4 );
        }
    }

    GIVEN( "A named group that does not exist in the pattern" )
    {
        const QString pattern = R"(^(?<timestamp>\d+)$)";

        THEN( "-1 is returned" )
        {
            REQUIRE( ChartTemplateGenerator::namedGroupIndex( pattern, "missing" )
                     == -1 );
        }
    }

    GIVEN( "An invalid regex pattern" )
    {
        const QString pattern = R"([invalid()";

        THEN( "-1 is returned" )
        {
            REQUIRE( ChartTemplateGenerator::namedGroupIndex( pattern, "anything" )
                     == -1 );
        }
    }

    GIVEN( "A pattern with no named groups" )
    {
        const QString pattern = R"((\d+)\s+(\w+))";

        THEN( "-1 is returned for any group name" )
        {
            REQUIRE( ChartTemplateGenerator::namedGroupIndex( pattern, "timestamp" )
                     == -1 );
        }
    }
}

// ===========================================================================
// levelFrequencyTemplates
// ===========================================================================

SCENARIO( "levelFrequencyTemplates generates per-level count series",
          "[charttemplategenerator]" )
{
    GIVEN( "An Android format with 5 level mappings" )
    {
        const auto fmt = makeAndroidFormat();

        WHEN( "Templates are generated with default bucket" )
        {
            const auto templates
                = ChartTemplateGenerator::levelFrequencyTemplates( fmt );

            THEN( "One series per level is produced" )
            {
                REQUIRE( templates.size() == 5 );
            }

            THEN( "Each series has a unique non-empty id" )
            {
                QSet<QString> ids;
                for ( const auto& s : templates ) {
                    REQUIRE_FALSE( s.id.isEmpty() );
                    ids.insert( s.id );
                }
                REQUIRE( ids.size() == 5 );
            }

            THEN( "Each series is in count mode (captureGroup == 0)" )
            {
                for ( const auto& s : templates ) {
                    REQUIRE( s.captureGroup == 0 );
                }
            }

            THEN( "Each series has a valid compiled regex" )
            {
                for ( const auto& s : templates ) {
                    REQUIRE( s.compiledRegex.isValid() );
                }
            }

            THEN( "Canonical levels appear in priority order" )
            {
                // error, warning, info, debug, trace  (canonical order filtered)
                REQUIRE( templates[ 0 ].name.contains( "error" ) );
                REQUIRE( templates[ 1 ].name.contains( "warning" ) );
                REQUIRE( templates[ 2 ].name.contains( "info" ) );
                REQUIRE( templates[ 3 ].name.contains( "debug" ) );
                REQUIRE( templates[ 4 ].name.contains( "trace" ) );
            }

            THEN( "Timestamp X-axis is configured" )
            {
                for ( const auto& s : templates ) {
                    REQUIRE( s.hasCustomXAxis() );
                    REQUIRE( s.isTimestampXAxis() );
                    REQUIRE( s.isBucketed() );
                    REQUIRE( s.bucketSizeMs == 1000 );
                }
            }
        }

        WHEN( "Templates are generated with a custom bucket size" )
        {
            const auto templates
                = ChartTemplateGenerator::levelFrequencyTemplates( fmt, 5000 );

            THEN( "The bucket size is applied to all series" )
            {
                for ( const auto& s : templates ) {
                    REQUIRE( s.bucketSizeMs == 5000 );
                }
            }
        }
    }

    GIVEN( "A format with a custom level not in the canonical list" )
    {
        auto fmt = makeAndroidFormat();
        auto levels = fmt.levelMappings();
        levels[ "custom_level" ] = "X";
        fmt.setLevelMappings( levels );

        WHEN( "Templates are generated" )
        {
            const auto templates
                = ChartTemplateGenerator::levelFrequencyTemplates( fmt );

            THEN( "The custom level is included after canonical levels" )
            {
                REQUIRE( templates.size() == 6 );
                REQUIRE( templates.last().name.contains( "custom_level" ) );
            }
        }
    }

    GIVEN( "A format with no level mappings" )
    {
        LogFormatDefinition emptyFmt;

        WHEN( "Templates are generated" )
        {
            const auto templates
                = ChartTemplateGenerator::levelFrequencyTemplates( emptyFmt );

            THEN( "The result is empty" )
            {
                REQUIRE( templates.isEmpty() );
            }
        }
    }

    GIVEN( "A format without timestamp (no timestamp field)" )
    {
        LogFormatDefinition fmt;
        QHash<QString, QString> levels;
        levels[ "error" ] = "ERR";
        fmt.setLevelMappings( levels );

        WHEN( "Templates are generated" )
        {
            const auto templates
                = ChartTemplateGenerator::levelFrequencyTemplates( fmt );

            THEN( "Series are produced but without timestamp X-axis" )
            {
                REQUIRE( templates.size() == 1 );
                REQUIRE_FALSE( templates[ 0 ].hasCustomXAxis() );
                REQUIRE_FALSE( templates[ 0 ].isBucketed() );
            }
        }
    }
}

// ===========================================================================
// messageRateTemplates
// ===========================================================================

SCENARIO( "messageRateTemplates generates a single message-rate series",
          "[charttemplategenerator]" )
{
    GIVEN( "A valid Android format" )
    {
        const auto fmt = makeAndroidFormat();

        WHEN( "Templates are generated" )
        {
            const auto templates
                = ChartTemplateGenerator::messageRateTemplates( fmt );

            THEN( "Exactly one series is returned" )
            {
                REQUIRE( templates.size() == 1 );
            }

            THEN( "The series is in count mode" )
            {
                REQUIRE( templates[ 0 ].captureGroup == 0 );
            }

            THEN( "The series name contains 'Message Rate'" )
            {
                REQUIRE( templates[ 0 ].name.contains( "Message Rate" ) );
            }

            THEN( "The series has a valid compiled regex" )
            {
                REQUIRE( templates[ 0 ].compiledRegex.isValid() );
            }

            THEN( "Timestamp X-axis is configured" )
            {
                REQUIRE( templates[ 0 ].hasCustomXAxis() );
                REQUIRE( templates[ 0 ].isTimestampXAxis() );
            }

            THEN( "Default bucket is 1 second" )
            {
                REQUIRE( templates[ 0 ].bucketSizeMs == 1000 );
            }
        }

        WHEN( "A custom bucket size is specified" )
        {
            const auto templates
                = ChartTemplateGenerator::messageRateTemplates( fmt, 10000 );

            THEN( "The bucket size is applied" )
            {
                REQUIRE( templates[ 0 ].bucketSizeMs == 10000 );
            }
        }
    }

    GIVEN( "A format with no regex patterns" )
    {
        LogFormatDefinition emptyFmt;

        WHEN( "Templates are generated" )
        {
            const auto templates
                = ChartTemplateGenerator::messageRateTemplates( emptyFmt );

            THEN( "The result is empty" )
            {
                REQUIRE( templates.isEmpty() );
            }
        }
    }
}

// ===========================================================================
// numericFieldTemplates
// ===========================================================================

SCENARIO( "numericFieldTemplates generates series for integer and float fields",
          "[charttemplategenerator]" )
{
    GIVEN( "An Android format with pid (integer) and tid (integer)" )
    {
        const auto fmt = makeAndroidFormat();

        WHEN( "Templates are generated" )
        {
            const auto templates
                = ChartTemplateGenerator::numericFieldTemplates( fmt );

            THEN( "Two series are produced (pid, tid)" )
            {
                REQUIRE( templates.size() == 2 );
            }

            THEN( "Each series captures the numeric value (captureGroup > 0)" )
            {
                for ( const auto& s : templates ) {
                    REQUIRE( s.captureGroup > 0 );
                }
            }

            THEN( "Each series has a valid compiled regex" )
            {
                for ( const auto& s : templates ) {
                    REQUIRE( s.compiledRegex.isValid() );
                }
            }

            THEN( "Series names contain the field name" )
            {
                bool hasPid = false;
                bool hasTid = false;
                for ( const auto& s : templates ) {
                    if ( s.name.contains( "pid" ) ) {
                        hasPid = true;
                    }
                    if ( s.name.contains( "tid" ) ) {
                        hasTid = true;
                    }
                }
                REQUIRE( hasPid );
                REQUIRE( hasTid );
            }

            THEN( "Bucketing is disabled for raw numeric values" )
            {
                for ( const auto& s : templates ) {
                    REQUIRE_FALSE( s.isBucketed() );
                }
            }
        }
    }

    GIVEN( "A format with a float field" )
    {
        auto fmt = makeAndroidFormat();
        auto defs = fmt.valueDefinitions();
        defs[ "duration" ] = { "float", false, false };
        fmt.setValueDefinitions( defs );

        // Need a pattern with the named group.
        auto patterns = fmt.regexPatterns();
        patterns[ "extended" ]
            = R"(^(?<timestamp>\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})\s+)"
              R"((?<duration>\d+\.\d+)ms$)";
        fmt.setRegexPatterns( patterns );

        auto order = fmt.valueFieldOrder();
        order.append( "duration" );
        fmt.setValueFieldOrder( order );

        WHEN( "Templates are generated" )
        {
            const auto templates
                = ChartTemplateGenerator::numericFieldTemplates( fmt );

            THEN( "The duration field is included" )
            {
                bool found = false;
                for ( const auto& s : templates ) {
                    if ( s.name.contains( "duration" ) ) {
                        found = true;
                    }
                }
                REQUIRE( found );
            }
        }
    }

    GIVEN( "A format with only string fields" )
    {
        const auto fmt = makeMinimalFormat();

        WHEN( "Templates are generated" )
        {
            const auto templates
                = ChartTemplateGenerator::numericFieldTemplates( fmt );

            THEN( "The result is empty" )
            {
                REQUIRE( templates.isEmpty() );
            }
        }
    }

    GIVEN( "A format with a hidden numeric field" )
    {
        auto fmt = makeAndroidFormat();
        auto defs = fmt.valueDefinitions();
        defs[ "pid" ] = { "integer", true, true }; // hidden
        fmt.setValueDefinitions( defs );

        WHEN( "Templates are generated" )
        {
            const auto templates
                = ChartTemplateGenerator::numericFieldTemplates( fmt );

            THEN( "The hidden field is excluded" )
            {
                for ( const auto& s : templates ) {
                    REQUIRE_FALSE( s.name.contains( "pid" ) );
                }
            }
        }
    }
}

// ===========================================================================
// fieldOccurrenceTemplates
// ===========================================================================

SCENARIO( "fieldOccurrenceTemplates generates count-mode series per visible field",
          "[charttemplategenerator]" )
{
    GIVEN( "An Android format" )
    {
        const auto fmt = makeAndroidFormat();

        WHEN( "Templates are generated" )
        {
            const auto templates
                = ChartTemplateGenerator::fieldOccurrenceTemplates( fmt );

            THEN( "body, timestamp, and level fields are skipped" )
            {
                for ( const auto& s : templates ) {
                    REQUIRE_FALSE( s.name.contains( "body" ) );
                    REQUIRE_FALSE( s.name.contains( "timestamp" ) );
                    REQUIRE_FALSE( s.name.contains( "level" ) );
                }
            }

            THEN( "All produced series are count-mode" )
            {
                for ( const auto& s : templates ) {
                    REQUIRE( s.captureGroup == 0 );
                }
            }

            THEN( "pid, tid, and tag fields are included" )
            {
                QStringList names;
                for ( const auto& s : templates ) {
                    names.append( s.name );
                }
                bool hasPid = false;
                bool hasTid = false;
                bool hasTag = false;
                for ( const auto& n : names ) {
                    if ( n.contains( "pid" ) ) {
                        hasPid = true;
                    }
                    if ( n.contains( "tid" ) ) {
                        hasTid = true;
                    }
                    if ( n.contains( "tag" ) ) {
                        hasTag = true;
                    }
                }
                REQUIRE( hasPid );
                REQUIRE( hasTid );
                REQUIRE( hasTag );
            }

            THEN( "Timestamp X-axis is configured with bucketing" )
            {
                for ( const auto& s : templates ) {
                    REQUIRE( s.hasCustomXAxis() );
                    REQUIRE( s.isTimestampXAxis() );
                    REQUIRE( s.isBucketed() );
                }
            }
        }

        WHEN( "A custom bucket size is specified" )
        {
            const auto templates
                = ChartTemplateGenerator::fieldOccurrenceTemplates( fmt, 30000 );

            THEN( "The bucket size is applied" )
            {
                for ( const auto& s : templates ) {
                    REQUIRE( s.bucketSizeMs == 30000 );
                }
            }
        }
    }

    GIVEN( "A format with a hidden field" )
    {
        auto fmt = makeAndroidFormat();
        auto defs = fmt.valueDefinitions();
        defs[ "tag" ] = { "string", true, true }; // hidden
        fmt.setValueDefinitions( defs );

        WHEN( "Templates are generated" )
        {
            const auto templates
                = ChartTemplateGenerator::fieldOccurrenceTemplates( fmt );

            THEN( "The hidden field is excluded" )
            {
                for ( const auto& s : templates ) {
                    REQUIRE_FALSE( s.name.contains( "tag" ) );
                }
            }
        }
    }

    GIVEN( "A format with only body field" )
    {
        const auto fmt = makeMinimalFormat();

        WHEN( "Templates are generated" )
        {
            const auto templates
                = ChartTemplateGenerator::fieldOccurrenceTemplates( fmt );

            THEN( "The result is empty (body is skipped)" )
            {
                REQUIRE( templates.isEmpty() );
            }
        }
    }
}

// ===========================================================================
// Integration: generated templates match real log lines
// ===========================================================================

SCENARIO( "Generated level templates match actual Android log lines",
          "[charttemplategenerator]" )
{
    GIVEN( "An Android format and its level frequency templates" )
    {
        const auto fmt = makeAndroidFormat();
        const auto templates
            = ChartTemplateGenerator::levelFrequencyTemplates( fmt, 0 );

        const QString errorLine
            = "04-08 20:02:49.780  3814  6380 E SomeTag: crash happened";
        const QString infoLine
            = "04-08 20:02:50.100  3814  6380 I SomeTag: started ok";
        const QString debugLine
            = "04-08 20:02:50.200  3814  6380 D SomeTag: checking state";

        WHEN( "The error template is applied" )
        {
            // Find the error series.
            const ChartSeriesDefinition* errSeries = nullptr;
            for ( const auto& s : templates ) {
                if ( s.name.contains( "error" ) ) {
                    errSeries = &s;
                    break;
                }
            }
            REQUIRE( errSeries != nullptr );

            THEN( "It matches the error line" )
            {
                REQUIRE( errSeries->compiledRegex.match( errorLine ).hasMatch() );
            }

            THEN( "It does not match the info line" )
            {
                REQUIRE_FALSE( errSeries->compiledRegex.match( infoLine ).hasMatch() );
            }
        }

        WHEN( "The info template is applied" )
        {
            const ChartSeriesDefinition* infoSeries = nullptr;
            for ( const auto& s : templates ) {
                if ( s.name.contains( "info" ) ) {
                    infoSeries = &s;
                    break;
                }
            }
            REQUIRE( infoSeries != nullptr );

            THEN( "It matches the info line" )
            {
                REQUIRE( infoSeries->compiledRegex.match( infoLine ).hasMatch() );
            }

            THEN( "It does not match the error line" )
            {
                REQUIRE_FALSE(
                    infoSeries->compiledRegex.match( errorLine ).hasMatch() );
            }
        }

        WHEN( "The timestamp X-axis regex is applied to any line" )
        {
            const auto& s = templates[ 0 ];
            REQUIRE( s.compiledXRegex.isValid() );
            const auto xMatch = s.compiledXRegex.match( errorLine );

            THEN( "The timestamp is extracted" )
            {
                REQUIRE( xMatch.hasMatch() );
                REQUIRE( xMatch.lastCapturedIndex() >= s.xCaptureGroup );
                const auto captured = xMatch.captured( s.xCaptureGroup );
                REQUIRE( captured == "04-08 20:02:49.780" );
            }
        }
    }
}

SCENARIO( "Generated numeric templates extract values from log lines",
          "[charttemplategenerator]" )
{
    GIVEN( "An Android format and its numeric field templates" )
    {
        const auto fmt = makeAndroidFormat();
        const auto templates
            = ChartTemplateGenerator::numericFieldTemplates( fmt );

        const QString line
            = "04-08 20:02:49.780  3814  6380 I SomeTag: hello world";

        WHEN( "The pid template is applied" )
        {
            const ChartSeriesDefinition* pidSeries = nullptr;
            for ( const auto& s : templates ) {
                if ( s.name.contains( "pid" ) ) {
                    pidSeries = &s;
                    break;
                }
            }
            REQUIRE( pidSeries != nullptr );

            THEN( "The pid value is extracted" )
            {
                const auto match = pidSeries->compiledRegex.match( line );
                REQUIRE( match.hasMatch() );
                REQUIRE( match.lastCapturedIndex() >= pidSeries->captureGroup );
                bool ok = false;
                const double val
                    = match.captured( pidSeries->captureGroup ).toDouble( &ok );
                REQUIRE( ok );
                REQUIRE( val == Approx( 3814.0 ) );
            }
        }

        WHEN( "The tid template is applied" )
        {
            const ChartSeriesDefinition* tidSeries = nullptr;
            for ( const auto& s : templates ) {
                if ( s.name.contains( "tid" ) ) {
                    tidSeries = &s;
                    break;
                }
            }
            REQUIRE( tidSeries != nullptr );

            THEN( "The tid value is extracted" )
            {
                const auto match = tidSeries->compiledRegex.match( line );
                REQUIRE( match.hasMatch() );
                bool ok = false;
                const double val
                    = match.captured( tidSeries->captureGroup ).toDouble( &ok );
                REQUIRE( ok );
                REQUIRE( val == Approx( 6380.0 ) );
            }
        }
    }
}
