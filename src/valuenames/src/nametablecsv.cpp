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

#include "nametablecsv.h"

#include "valuenamer.h"

#include <QHash>

#include <algorithm>
#include <array>
#include <utility>

namespace logsquirl::valuenames {

namespace {

constexpr auto Quote = QLatin1Char( '"' );
constexpr auto Comment = QLatin1Char( '#' );

bool isLineEnd( QChar c )
{
    return c == QLatin1Char( '\n' ) || c == QLatin1Char( '\r' );
}

// Reads a CSV text record by record, with the line each one starts on.
class CsvReader {
public:
    CsvReader( const QString& text, QChar separator )
        : text_{ text }
        , separator_{ separator }
    {
        // A byte order mark, as spreadsheets write one, is no part of the
        // first field.
        if ( text_.startsWith( QChar( QChar::ByteOrderMark ) ) ) {
            position_ = 1;
        }
    }

    // The next record, skipping empty and comment lines; false at the end.
    bool next( CsvRecord& record )
    {
        skipIgnoredLines();
        if ( position_ >= text_.size() ) {
            return false;
        }

        record.line = line_;
        record.fields.clear();
        for ( ;; ) {
            record.fields.append( field() );
            if ( position_ >= text_.size() ) {
                return true;
            }
            if ( text_[ position_ ] == separator_ ) {
                ++position_;
                continue;
            }
            skipLineEnd();
            return true;
        }
    }

private:
    // Empty lines, lines of blanks only and lines starting with '#'.
    void skipIgnoredLines()
    {
        while ( position_ < text_.size() ) {
            auto end = position_;
            while ( end < text_.size() && !isLineEnd( text_[ end ] ) ) {
                ++end;
            }
            const auto lineText = QStringView{ text_ }.mid( position_, end - position_ );
            if ( !lineText.startsWith( Comment ) && !lineText.trimmed().isEmpty() ) {
                return;
            }
            position_ = end;
            skipLineEnd();
        }
    }

    void skipLineEnd()
    {
        if ( position_ < text_.size() && text_[ position_ ] == QLatin1Char( '\r' ) ) {
            ++position_;
            if ( position_ < text_.size() && text_[ position_ ] == QLatin1Char( '\n' ) ) {
                ++position_;
            }
            ++line_;
        }
        else if ( position_ < text_.size() && text_[ position_ ] == QLatin1Char( '\n' ) ) {
            ++position_;
            ++line_;
        }
    }

    bool atFieldEnd() const
    {
        return position_ >= text_.size() || text_[ position_ ] == separator_
               || isLineEnd( text_[ position_ ] );
    }

    // One field, up to the separator or the line end after it. Blanks around
    // a field that is not quoted are dropped, those inside quotes kept.
    QString field()
    {
        const auto start = position_;
        while ( !atFieldEnd() && text_[ position_ ].isSpace() ) {
            ++position_;
        }
        if ( position_ >= text_.size() || text_[ position_ ] != Quote ) {
            position_ = start;
            while ( !atFieldEnd() ) {
                ++position_;
            }
            return QStringView{ text_ }.mid( start, position_ - start ).trimmed().toString();
        }

        ++position_;
        QString quoted;
        while ( position_ < text_.size() ) {
            const auto c = text_[ position_ ];
            if ( c == Quote ) {
                if ( position_ + 1 < text_.size() && text_[ position_ + 1 ] == Quote ) {
                    quoted.append( Quote );
                    position_ += 2;
                    continue;
                }
                ++position_;
                break;
            }
            if ( c == QLatin1Char( '\n' )
                 || ( c == QLatin1Char( '\r' )
                      && ( position_ + 1 >= text_.size()
                           || text_[ position_ + 1 ] != QLatin1Char( '\n' ) ) ) ) {
                ++line_;
            }
            quoted.append( c );
            ++position_;
        }
        // Whatever follows the closing quote belongs to the field as well.
        const auto rest = position_;
        while ( !atFieldEnd() ) {
            ++position_;
        }
        quoted.append( QStringView{ text_ }.mid( rest, position_ - rest ).trimmed() );
        return quoted;
    }

    const QString& text_;
    QChar separator_;
    qsizetype position_ = 0;
    int line_ = 1;
};

constexpr std::array<QChar, 3> Separators{ QLatin1Char( '\t' ), QLatin1Char( ';' ),
                                           QLatin1Char( ',' ) };

// How many of the first records the separator is looked for in.
constexpr int SeparatorSampleLines = 20;

QString csvField( const QString& field, QChar separator, bool firstOfRecord )
{
    const bool needsQuotes
        = std::any_of( Separators.cbegin(), Separators.cend(),
                       [ &field ]( QChar c ) { return field.contains( c ); } )
          || field.contains( separator ) || field.contains( Quote )
          || field.contains( QLatin1Char( '\n' ) ) || field.contains( QLatin1Char( '\r' ) )
          || ( !field.isEmpty() && ( field.front().isSpace() || field.back().isSpace() ) )
          || ( firstOfRecord && ( field.isEmpty() || field.startsWith( Comment ) ) );
    if ( !needsQuotes ) {
        return field;
    }
    auto quoted = field;
    quoted.replace( Quote, QStringLiteral( "\"\"" ) );
    return Quote + quoted + Quote;
}

} // namespace

QChar detectCsvSeparator( const QString& text )
{
    // Each separator reads the first records as it would read them, so that
    // one inside a quoted field, or a stray quote inside a field, counts as
    // it would when importing.
    size_t best = Separators.size();
    int bestRecords = 0;
    for ( size_t s = 0; s < Separators.size(); ++s ) {
        CsvReader reader{ text, Separators[ s ] };
        CsvRecord record;
        int separated = 0;
        for ( int i = 0; i < SeparatorSampleLines && reader.next( record ); ++i ) {
            separated += record.fields.size() > 1 ? 1 : 0;
        }
        // On a tie the earlier of tab, ';' and ',' wins: a ';' file holds
        // commas in its fields far more often than the other way round.
        if ( separated > bestRecords ) {
            best = s;
            bestRecords = separated;
        }
    }
    return best == Separators.size() ? QLatin1Char( ',' ) : Separators[ best ];
}

QList<CsvRecord> csvRecords( const QString& text, QChar separator )
{
    QList<CsvRecord> records;
    CsvReader reader{ text, separator };
    CsvRecord record;
    while ( reader.next( record ) ) {
        records.append( record );
    }
    return records;
}

CsvImport importCsv( const QString& text, const CsvImportOptions& options )
{
    CsvImport imported;
    imported.separator = detectCsvSeparator( text );

    auto records = csvRecords( text, imported.separator );
    if ( options.hasHeader && !records.isEmpty() ) {
        records.removeFirst();
    }

    const auto keyColumn = options.keyColumn;
    const auto nameColumn = options.nameColumn;
    QHash<QString, int> firstLines;
    for ( const auto& record : std::as_const( records ) ) {
        const auto& fields = record.fields;
        const auto key
            = keyColumn >= 0 && keyColumn < fields.size() ? fields[ keyColumn ] : QString{};
        if ( key.isEmpty() || nameColumn < 0 || nameColumn >= fields.size() ) {
            imported.warnings.append(
                CsvImportWarning{ CsvImportWarning::Kind::MissingColumn, record.line, key, 0 } );
            continue;
        }

        const auto foldedKey = keyIdentity( key, options.caseSensitive );
        const auto firstLine = firstLines.constFind( foldedKey );
        if ( firstLine != firstLines.cend() ) {
            imported.warnings.append( CsvImportWarning{ CsvImportWarning::Kind::DuplicateKey,
                                                        record.line, key, *firstLine } );
            continue;
        }
        firstLines.insert( foldedKey, record.line );
        const auto& name = fields[ nameColumn ];
        if ( name.isEmpty() ) {
            imported.warnings.append(
                CsvImportWarning{ CsvImportWarning::Kind::EmptyName, record.line, key, 0 } );
        }
        else if ( hasControlCharacters( name ) ) {
            imported.warnings.append( CsvImportWarning{
                CsvImportWarning::Kind::ControlCharacterInName, record.line, key, 0 } );
        }
        imported.rows.append( NameRow{ key, name } );
    }

    return imported;
}

QString exportCsv( const QList<NameRow>& rows, QChar separator, const QStringList& header )
{
    QString csv;
    const auto appendRecord = [ &csv, separator ]( const QStringList& fields ) {
        for ( qsizetype i = 0; i < fields.size(); ++i ) {
            if ( i > 0 ) {
                csv.append( separator );
            }
            csv.append( csvField( fields[ i ], separator, i == 0 ) );
        }
        csv.append( QLatin1Char( '\n' ) );
    };

    if ( !header.isEmpty() ) {
        appendRecord( header );
    }
    for ( const auto& row : rows ) {
        appendRecord( { row.key, row.name } );
    }
    return csv;
}

} // namespace logsquirl::valuenames
