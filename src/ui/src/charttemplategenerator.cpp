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

#include "charttemplategenerator.h"

#include <QRegularExpression>
#include <QUuid>

#include "logformatdefinition.h"

namespace {

// Predefined palette for auto-assigned series colors (same as in ChartPanel).
const QColor kPalette[] = {
    QColor( "#e6194b" ), QColor( "#3cb44b" ), QColor( "#4363d8" ),
    QColor( "#f58231" ), QColor( "#911eb4" ), QColor( "#42d4f4" ),
    QColor( "#f032e6" ), QColor( "#bfef45" ), QColor( "#fabebe" ),
    QColor( "#469990" ),
};
constexpr int kPaletteSize = sizeof( kPalette ) / sizeof( kPalette[ 0 ] );

// Assign a color from the palette based on a running index.
QColor colorForIndex( int idx )
{
    return kPalette[ idx % kPaletteSize ];
}

// Build the X-axis fields for a series definition using the format's
// timestamp.  Modifies |def| in place.  Returns true if timestamp
// X-axis was configured successfully.
bool configureTimestampXAxis( ChartSeriesDefinition& def,
                              const LogFormatDefinition& format,
                              qint64 bucketMs )
{
    const auto& tsField = format.timestampField();
    if ( tsField.isEmpty() ) {
        return false;
    }

    const auto& tsFormats = format.timestampFormats();
    if ( tsFormats.isEmpty() ) {
        return false;
    }

    // Find a Qt-compatible timestamp format (skip epoch-only formats).
    QString qtFmt;
    for ( const auto& fmt : tsFormats ) {
        qtFmt = ChartTemplateGenerator::strftimeToQtFormat( fmt );
        if ( !qtFmt.isEmpty() ) {
            break;
        }
    }
    if ( qtFmt.isEmpty() ) {
        return false;
    }

    // Find a regex pattern containing the timestamp group.
    const auto xPattern
        = ChartTemplateGenerator::patternContainingGroup( format, tsField );
    if ( xPattern.isEmpty() ) {
        return false;
    }

    const int tsGroupIdx
        = ChartTemplateGenerator::namedGroupIndex( xPattern, tsField );
    if ( tsGroupIdx < 1 ) {
        return false;
    }

    def.xPattern = xPattern;
    def.xCaptureGroup = tsGroupIdx;
    def.xTimestampFormat = qtFmt;
    def.bucketSizeMs = bucketMs;
    return true;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

QString ChartTemplateGenerator::strftimeToQtFormat( const QString& strftimeFmt )
{
    // Epoch-only format — cannot be parsed by QDateTime::fromString.
    if ( strftimeFmt.trimmed() == "%s" ) {
        return {};
    }

    QString result;
    result.reserve( strftimeFmt.size() * 2 );

    for ( int i = 0; i < strftimeFmt.size(); ++i ) {
        if ( strftimeFmt[ i ] == '%' && i + 1 < strftimeFmt.size() ) {
            const QChar spec = strftimeFmt[ i + 1 ];
            ++i; // skip the specifier character

            if ( spec == 'Y' ) {
                result += "yyyy";
            }
            else if ( spec == 'y' ) {
                result += "yy";
            }
            else if ( spec == 'm' ) {
                result += "MM";
            }
            else if ( spec == 'd' ) {
                result += "dd";
            }
            else if ( spec == 'e' ) {
                // %e = day of month, space-padded (Qt has no direct equivalent;
                // use 'd' which is unpadded for single digits).
                result += "d";
            }
            else if ( spec == 'H' ) {
                result += "HH";
            }
            else if ( spec == 'M' ) {
                result += "mm";
            }
            else if ( spec == 'S' ) {
                result += "ss";
            }
            else if ( spec == 'L' ) {
                // lnav extension: milliseconds
                result += "zzz";
            }
            else if ( spec == 'f' ) {
                // microseconds — Qt only supports milliseconds (zzz).
                // Use zzz and accept the loss of precision.
                result += "zzz";
            }
            else if ( spec == 'b' || spec == 'B' ) {
                result += "MMM";
            }
            else if ( spec == 'p' ) {
                result += "AP";
            }
            else if ( spec == 'I' ) {
                result += "hh";
            }
            else if ( spec == 'z' ) {
                // Timezone offset, e.g. +0200
                result += "t";
            }
            else if ( spec == 'Z' ) {
                // Timezone name — no portable Qt equivalent; skip.
            }
            else if ( spec == 's' ) {
                // Unix epoch seconds — cannot be handled by QDateTime format.
                return {};
            }
            else if ( spec == '%' ) {
                result += '%';
            }
            // Unknown specifiers are silently skipped.
        }
        else {
            result += strftimeFmt[ i ];
        }
    }

    return result;
}

QString ChartTemplateGenerator::patternContainingGroup(
    const LogFormatDefinition& format, const QString& groupName )
{
    const auto& patterns = format.regexPatterns();
    const QString needle = QString( "(?<%1>" ).arg( groupName );
    for ( auto it = patterns.cbegin(); it != patterns.cend(); ++it ) {
        if ( it.value().contains( needle ) ) {
            return it.value();
        }
    }
    return {};
}

int ChartTemplateGenerator::namedGroupIndex( const QString& pattern,
                                             const QString& groupName )
{
    const QRegularExpression re( pattern );
    if ( !re.isValid() ) {
        return -1;
    }
    const auto groups = re.namedCaptureGroups();
    for ( int i = 1; i < groups.size(); ++i ) {
        if ( groups[ i ] == groupName ) {
            return i;
        }
    }
    return -1;
}

QVector<ChartSeriesDefinition> ChartTemplateGenerator::levelFrequencyTemplates(
    const LogFormatDefinition& format, qint64 bucketMs )
{
    const auto& levelMappings = format.levelMappings();
    if ( levelMappings.isEmpty() ) {
        return {};
    }

    // Canonical level order for consistent display.
    static const QStringList kLevelOrder
        = { "fatal", "critical", "error", "warning", "notice", "info", "debug", "trace" };

    // Specific colors for well-known levels.
    static const QHash<QString, QColor> kLevelColors = {
        { "fatal", QColor( "#b71c1c" ) },    { "critical", QColor( "#c62828" ) },
        { "error", QColor( "#e6194b" ) },     { "warning", QColor( "#f58231" ) },
        { "notice", QColor( "#4363d8" ) },    { "info", QColor( "#3cb44b" ) },
        { "debug", QColor( "#42d4f4" ) },     { "trace", QColor( "#9e9e9e" ) },
    };

    QVector<ChartSeriesDefinition> result;
    int colorIdx = 0;

    for ( const auto& level : kLevelOrder ) {
        if ( !levelMappings.contains( level ) ) {
            continue;
        }
        const auto& levelRegex = levelMappings.value( level );

        ChartSeriesDefinition def;
        def.id = QUuid::createUuid().toString( QUuid::WithoutBraces );
        def.name = QObject::tr( "Level: %1" ).arg( level );
        def.color = kLevelColors.value( level, colorForIndex( colorIdx ) );
        def.pattern = levelRegex;
        def.captureGroup = 0; // count mode
        configureTimestampXAxis( def, format, bucketMs );
        def.compilePattern();

        if ( def.compiledRegex.isValid() ) {
            result.append( def );
        }
        ++colorIdx;
    }

    // Include any custom levels not in the canonical list.
    for ( auto it = levelMappings.cbegin(); it != levelMappings.cend(); ++it ) {
        if ( kLevelOrder.contains( it.key() ) ) {
            continue;
        }
        ChartSeriesDefinition def;
        def.id = QUuid::createUuid().toString( QUuid::WithoutBraces );
        def.name = QObject::tr( "Level: %1" ).arg( it.key() );
        def.color = colorForIndex( colorIdx );
        def.pattern = it.value();
        def.captureGroup = 0;
        configureTimestampXAxis( def, format, bucketMs );
        def.compilePattern();

        if ( def.compiledRegex.isValid() ) {
            result.append( def );
        }
        ++colorIdx;
    }

    return result;
}

QVector<ChartSeriesDefinition> ChartTemplateGenerator::messageRateTemplates(
    const LogFormatDefinition& format, qint64 bucketMs )
{
    // Use the first regex pattern that contains the body or timestamp field.
    const auto& patterns = format.regexPatterns();
    if ( patterns.isEmpty() ) {
        return {};
    }

    // Pick the first pattern (any line that matches the format counts).
    const auto firstPattern = patterns.cbegin().value();

    ChartSeriesDefinition def;
    def.id = QUuid::createUuid().toString( QUuid::WithoutBraces );
    def.name = QObject::tr( "Message Rate" );
    def.color = QColor( "#2196F3" );
    def.pattern = firstPattern;
    def.captureGroup = 0; // count mode
    configureTimestampXAxis( def, format, bucketMs );
    def.compilePattern();

    if ( !def.compiledRegex.isValid() ) {
        return {};
    }

    return { def };
}

QVector<ChartSeriesDefinition> ChartTemplateGenerator::numericFieldTemplates(
    const LogFormatDefinition& format )
{
    const auto& valueDefs = format.valueDefinitions();
    const auto& fieldOrder = format.valueFieldOrder();

    QVector<ChartSeriesDefinition> result;
    int colorIdx = 0;

    for ( const auto& field : fieldOrder ) {
        if ( !valueDefs.contains( field ) ) {
            continue;
        }
        const auto& vdef = valueDefs.value( field );
        if ( vdef.kind != "integer" && vdef.kind != "float" ) {
            continue;
        }
        if ( vdef.hidden ) {
            continue;
        }

        // Find a pattern containing this field as a named group.
        const auto pat = patternContainingGroup( format, field );
        if ( pat.isEmpty() ) {
            continue;
        }

        const int groupIdx = namedGroupIndex( pat, field );
        if ( groupIdx < 1 ) {
            continue;
        }

        ChartSeriesDefinition def;
        def.id = QUuid::createUuid().toString( QUuid::WithoutBraces );
        def.name = QObject::tr( "%1 (value)" ).arg( field );
        def.color = colorForIndex( colorIdx );
        def.pattern = pat;
        def.captureGroup = groupIdx;
        configureTimestampXAxis( def, format, 0 ); // no bucketing for raw values
        def.compilePattern();

        if ( def.compiledRegex.isValid() ) {
            result.append( def );
        }
        ++colorIdx;
    }

    return result;
}

QVector<ChartSeriesDefinition> ChartTemplateGenerator::fieldOccurrenceTemplates(
    const LogFormatDefinition& format, qint64 bucketMs )
{
    const auto& valueDefs = format.valueDefinitions();
    const auto& fieldOrder = format.valueFieldOrder();

    // Skip common fields that are not interesting for occurrence counting.
    static const QStringList kSkipFields
        = { "body", "timestamp", "level" };

    QVector<ChartSeriesDefinition> result;
    int colorIdx = 0;

    for ( const auto& field : fieldOrder ) {
        if ( kSkipFields.contains( field ) ) {
            continue;
        }
        if ( !valueDefs.contains( field ) ) {
            continue;
        }
        const auto& vdef = valueDefs.value( field );
        if ( vdef.hidden ) {
            continue;
        }

        // Use the format's regex pattern that captures this field.
        const auto pat = patternContainingGroup( format, field );
        if ( pat.isEmpty() ) {
            continue;
        }

        ChartSeriesDefinition def;
        def.id = QUuid::createUuid().toString( QUuid::WithoutBraces );
        def.name = QObject::tr( "Field: %1" ).arg( field );
        def.color = colorForIndex( colorIdx );
        def.pattern = pat;
        def.captureGroup = 0; // count mode
        configureTimestampXAxis( def, format, bucketMs );
        def.compilePattern();

        if ( def.compiledRegex.isValid() ) {
            result.append( def );
        }
        ++colorIdx;
    }

    return result;
}
