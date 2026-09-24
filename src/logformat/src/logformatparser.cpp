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

#include "logformatparser.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <optional>

// Keys in the lnav JSON schema that are not format definitions
static bool isMetaKey( const QString& key )
{
    return key.startsWith( '$' );
}

namespace {

// QJsonObject sorts its members, but the order of the members of a Log
// Format's "value" section is the order of its columns. This reads that order
// from the text of the file: a minimal scanner that skips everything else.
class MemberOrderScanner {
public:
    explicit MemberOrderScanner( const QByteArray& text )
        : text_( text )
    {
    }

    // The keys, in file order, of the object "value" of the top-level member
    // formatName; empty when the text has no such object.
    QStringList valueKeys( const QString& formatName )
    {
        pos_ = 0;
        auto format = findMember( formatName );
        if ( !format ) {
            return {};
        }
        pos_ = *format;
        auto value = findMember( QStringLiteral( "value" ) );
        if ( !value ) {
            return {};
        }
        pos_ = *value;
        QStringList keys;
        forEachMember( [ &keys ]( const QString& key, qsizetype ) {
            keys << key;
            return false;
        } );
        return keys;
    }

private:
    // Calls visit( key, position of the value ) for each member of the object
    // at pos_ until it returns true; leaves pos_ after the object, or, when
    // stopped, wherever it was.
    template <typename Visitor>
    void forEachMember( Visitor visit )
    {
        skipSpace();
        if ( pos_ >= text_.size() || text_[ pos_ ] != '{' ) {
            return;
        }
        ++pos_;
        for ( ;; ) {
            skipSpace();
            if ( pos_ >= text_.size() ) {
                return;
            }
            if ( text_[ pos_ ] == '}' ) {
                ++pos_;
                return;
            }
            if ( text_[ pos_ ] == ',' ) {
                ++pos_;
                continue;
            }
            if ( text_[ pos_ ] != '"' ) {
                pos_ = text_.size();
                return;
            }
            const auto key = readString();
            skipSpace();
            if ( pos_ < text_.size() && text_[ pos_ ] == ':' ) {
                ++pos_;
            }
            skipSpace();
            const auto valuePos = pos_;
            if ( visit( key, valuePos ) ) {
                return;
            }
            pos_ = valuePos;
            skipValue();
        }
    }

    std::optional<qsizetype> findMember( const QString& name )
    {
        std::optional<qsizetype> found;
        forEachMember( [ &found, &name ]( const QString& key, qsizetype valuePos ) {
            if ( key == name ) {
                found = valuePos;
                return true;
            }
            return false;
        } );
        return found;
    }

    void skipSpace()
    {
        while ( pos_ < text_.size()
                && ( text_[ pos_ ] == ' ' || text_[ pos_ ] == '\t' || text_[ pos_ ] == '\n'
                     || text_[ pos_ ] == '\r' ) ) {
            ++pos_;
        }
    }

    // Reads the string at pos_ (which is on its opening quote), decoded.
    QString readString()
    {
        const auto start = pos_;
        ++pos_;
        while ( pos_ < text_.size() && text_[ pos_ ] != '"' ) {
            pos_ += text_[ pos_ ] == '\\' ? 2 : 1;
        }
        ++pos_;
        const auto raw = text_.mid( start, pos_ - start );
        if ( !raw.contains( '\\' ) ) {
            return QString::fromUtf8( raw.mid( 1, raw.size() - 2 ) );
        }
        const auto array = QJsonDocument::fromJson( "[" + raw + "]" ).array();
        return array.isEmpty() ? QString() : array.at( 0 ).toString();
    }

    void skipValue()
    {
        skipSpace();
        if ( pos_ >= text_.size() ) {
            return;
        }
        if ( text_[ pos_ ] == '"' ) {
            readString();
        }
        else if ( text_[ pos_ ] == '{' ) {
            forEachMember( []( const QString&, qsizetype ) { return false; } );
        }
        else if ( text_[ pos_ ] == '[' ) {
            ++pos_;
            for ( skipSpace(); pos_ < text_.size() && text_[ pos_ ] != ']'; skipSpace() ) {
                if ( text_[ pos_ ] == ',' ) {
                    ++pos_;
                    continue;
                }
                skipValue();
            }
            ++pos_;
        }
        else {
            while ( pos_ < text_.size() && text_[ pos_ ] != ',' && text_[ pos_ ] != '}'
                    && text_[ pos_ ] != ']' ) {
                ++pos_;
            }
        }
    }

    const QByteArray& text_;
    qsizetype pos_ = 0;
};

} // namespace

// Parse a single format definition from a JSON object.
// Returns true on success, filling 'def'. Returns false if the format has no
// "regex" section (a "file-type": "json" or "logfmt" format needs none).
static bool parseSingleFormat( const QString& name, const QJsonObject& obj,
                               LogFormatDefinition& def, const QStringList& valueKeyOrder )
{
    const auto fileType = obj.value( "file-type" ).toString();
    const bool isJson = fileType == QLatin1String( "json" );
    const bool isLogfmt = fileType == QLatin1String( "logfmt" ); // our own extension of the schema
    const bool isKeyed = isJson || isLogfmt;

    // "regex" is required for a regex format — without it we cannot match log lines
    if ( !isKeyed && ( !obj.contains( "regex" ) || !obj.value( "regex" ).isObject() ) ) {
        return false;
    }
    def.setKind( isJson     ? LogFormatKind::Json
                 : isLogfmt ? LogFormatKind::Logfmt
                            : LogFormatKind::Regex );

    def.setName( name );
    def.setTitle( obj.value( "title" ).toString() );
    def.setDescription( obj.value( "description" ).toString() );

    // Parse regex patterns
    QHash<QString, QString> patterns;
    const auto regexObj = isKeyed ? QJsonObject() : obj.value( "regex" ).toObject();
    for ( auto it = regexObj.begin(); it != regexObj.end(); ++it ) {
        if ( it.value().isObject() ) {
            const auto patternObj = it.value().toObject();
            const auto pattern = patternObj.value( "pattern" ).toString();
            if ( !pattern.isEmpty() ) {
                patterns.insert( it.key(), pattern );
            }
        }
    }

    if ( patterns.isEmpty() && !isKeyed ) {
        return false;
    }
    def.setRegexPatterns( patterns );

    // Parse special field names (with lnav defaults)
    if ( obj.contains( "timestamp-field" ) ) {
        def.setTimestampField( obj.value( "timestamp-field" ).toString() );
    }
    if ( obj.contains( "level-field" ) ) {
        def.setLevelField( obj.value( "level-field" ).toString() );
    }
    if ( obj.contains( "body-field" ) ) {
        def.setBodyField( obj.value( "body-field" ).toString() );
    }
    if ( obj.contains( "thread-id-field" ) ) {
        def.setThreadIdField( obj.value( "thread-id-field" ).toString() );
    }
    if ( obj.contains( "opid-field" ) ) {
        def.setOpidField( obj.value( "opid-field" ).toString() );
    }
    if ( obj.contains( "file-pattern" ) ) {
        def.setFilePattern( obj.value( "file-pattern" ).toString() );
    }
    if ( obj.contains( "ordered-by-time" ) ) {
        def.setOrderedByTime( obj.value( "ordered-by-time" ).toBool( true ) );
    }

    // Parse timestamp-format (can be string or array)
    if ( obj.contains( "timestamp-format" ) ) {
        const auto tsVal = obj.value( "timestamp-format" );
        QStringList formats;
        if ( tsVal.isArray() ) {
            for ( const auto& v : tsVal.toArray() ) {
                formats << v.toString();
            }
        }
        else if ( tsVal.isString() ) {
            formats << tsVal.toString();
        }
        def.setTimestampFormats( formats );
    }

    if ( obj.contains( "timestamp-divisor" ) ) {
        const auto divisor = obj.value( "timestamp-divisor" ).toDouble( 1.0 );
        if ( divisor > 0.0 ) {
            def.setTimestampDivisor( divisor );
        }
    }

    // Parse level mappings
    if ( obj.contains( "level" ) && obj.value( "level" ).isObject() ) {
        QHash<QString, QString> levels;
        const auto levelObj = obj.value( "level" ).toObject();
        for ( auto it = levelObj.begin(); it != levelObj.end(); ++it ) {
            levels.insert( it.key(), it.value().toString() );
        }
        def.setLevelMappings( levels );
    }

    // Parse value definitions (preserving field order from regex capture groups)
    if ( obj.contains( "value" ) && obj.value( "value" ).isObject() ) {
        QHash<QString, LogFormatValueDef> values;
        const auto valueObj = obj.value( "value" ).toObject();
        for ( auto it = valueObj.begin(); it != valueObj.end(); ++it ) {
            if ( it.value().isObject() ) {
                const auto valDef = it.value().toObject();
                LogFormatValueDef vd;
                vd.kind = valDef.value( "kind" ).toString( "string" );
                vd.identifier = valDef.value( "identifier" ).toBool( false );
                vd.hidden = valDef.value( "hidden" ).toBool( false );
                values.insert( it.key(), vd );
            }
        }
        def.setValueDefinitions( values );

        // Derive field order from named capture groups in the first regex pattern.
        // This preserves the order in which fields appear in the actual log lines.
        // Pick the pattern with the most named groups (e.g. "standard" over "dropped_data")
        // because QHash iteration order is non-deterministic.
        QStringList fieldOrder;
        if ( isKeyed ) {
            // The columns are the members of "value" in file order; the
            // timestamp field is always one of them.
            for ( const auto& key : valueKeyOrder ) {
                if ( values.contains( key ) && !fieldOrder.contains( key ) ) {
                    fieldOrder << key;
                }
            }
            if ( !fieldOrder.isEmpty() && !def.timestampField().isEmpty()
                 && !fieldOrder.contains( def.timestampField() ) ) {
                fieldOrder.prepend( def.timestampField() );
            }
        }
        else if ( !patterns.isEmpty() ) {
            static const QRegularExpression namedGroupRe( R"(\(\?<([a-zA-Z_]\w*)>)" );

            QString bestPattern;
            qsizetype bestGroupCount = -1;
            for ( auto it = patterns.begin(); it != patterns.end(); ++it ) {
                const auto count = it.value().count( QStringLiteral( "(?<" ) );
                if ( count > bestGroupCount ) {
                    bestGroupCount = count;
                    bestPattern = it.value();
                }
            }

            auto matchIter = namedGroupRe.globalMatch( bestPattern );
            while ( matchIter.hasNext() ) {
                const auto groupName = matchIter.next().captured( 1 );
                // Include ALL capture groups (value fields + special fields)
                // so columnNames() can reconstruct the original log column order.
                fieldOrder << groupName;
            }
        }
        def.setValueFieldOrder( fieldOrder );
    }

    // Parse sample lines
    if ( obj.contains( "sample" ) && obj.value( "sample" ).isArray() ) {
        QVector<LogFormatSample> samples;
        for ( const auto& sampleVal : obj.value( "sample" ).toArray() ) {
            if ( sampleVal.isObject() ) {
                const auto sampleObj = sampleVal.toObject();
                LogFormatSample sample;
                sample.line = sampleObj.value( "line" ).toString();
                sample.level = sampleObj.value( "level" ).toString();
                if ( !sample.line.isEmpty() ) {
                    samples.append( sample );
                }
            }
        }
        def.setSampleLines( samples );
    }

    return true;
}

QVector<LogFormatDefinition> LogFormatParser::parseJsonString( const char* jsonString )
{
    QVector<LogFormatDefinition> results;

    if ( !jsonString || jsonString[ 0 ] == '\0' ) {
        return results;
    }

    QJsonParseError error;
    auto doc = QJsonDocument::fromJson( QByteArray( jsonString ), &error );

    if ( error.error != QJsonParseError::NoError || !doc.isObject() ) {
        return results;
    }

    const QByteArray text( jsonString );
    const auto rootObj = doc.object();
    for ( auto it = rootObj.begin(); it != rootObj.end(); ++it ) {
        if ( isMetaKey( it.key() ) ) {
            continue;
        }
        if ( !it.value().isObject() ) {
            continue;
        }

        const auto formatObj = it.value().toObject();
        QStringList valueKeyOrder;
        const auto fileType = formatObj.value( "file-type" ).toString();
        if ( ( fileType == QLatin1String( "json" ) || fileType == QLatin1String( "logfmt" ) )
             && formatObj.value( "value" ).isObject() ) {
            valueKeyOrder = MemberOrderScanner( text ).valueKeys( it.key() );
        }

        LogFormatDefinition def;
        if ( parseSingleFormat( it.key(), formatObj, def, valueKeyOrder ) ) {
            results.append( std::move( def ) );
        }
    }

    return results;
}

QVector<LogFormatDefinition> LogFormatParser::parseFile( const QString& filePath )
{
    QFile file( filePath );
    if ( !file.open( QIODevice::ReadOnly | QIODevice::Text ) ) {
        return {};
    }

    const auto content = file.readAll();
    return parseJsonString( content.constData() );
}
