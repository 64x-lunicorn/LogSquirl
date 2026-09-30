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

#include "valuenamer.h"

#include <QHash>
#include <QRegularExpression>

#include <algorithm>
#include <optional>
#include <utility>
#include <vector>

namespace logsquirl::valuenames {

namespace {

// Rule and key regexes read \\w, \\d and case as the Highlighters and the
// Predefined Filters do: by Unicode properties.
QRegularExpression ruleRegex( const QString& pattern )
{
    return QRegularExpression{ pattern, QRegularExpression::UseUnicodePropertiesOption };
}

QRegularExpression keyRegex( const QString& key, bool caseSensitive )
{
    auto options
        = QRegularExpression::PatternOptions{ QRegularExpression::UseUnicodePropertiesOption };
    if ( !caseSensitive ) {
        options |= QRegularExpression::CaseInsensitiveOption;
    }
    return QRegularExpression{ QRegularExpression::anchoredPattern( key ), options };
}

QString folded( const QString& key, bool caseSensitive )
{
    return caseSensitive ? key : key.toCaseFolded();
}

bool isControlCharacter( QChar c )
{
    const auto category = c.category();
    return category == QChar::Other_Control || category == QChar::Separator_Line
           || category == QChar::Separator_Paragraph;
}

// The capture group of the rule's regex a group reference gives: its number,
// or the name of a named group. 0, the whole match, only for a regex without
// capture groups. -1 when the regex has no such group.
int captureGroupIndex( const QRegularExpression& regex, const QString& group )
{
    const auto captureCount = regex.captureCount();
    bool isNumber = false;
    const auto number = group.toInt( &isNumber );
    if ( isNumber ) {
        if ( number == 0 ) {
            return captureCount == 0 ? 0 : -1;
        }
        return number > 0 && number <= captureCount ? number : -1;
    }
    if ( group.isEmpty() ) {
        return -1;
    }
    const auto index = regex.namedCaptureGroups().indexOf( group );
    return index > 0 ? static_cast<int>( index ) : -1;
}

// Calls found( n, start, length ) for every {n} in name, n being all digits.
template <typename Found>
void forEachGroupReference( const QString& name, Found&& found )
{
    for ( qsizetype i = 0; i < name.size(); ++i ) {
        if ( name[ i ] != QLatin1Char( '{' ) ) {
            continue;
        }
        auto end = i + 1;
        while ( end < name.size() && name[ end ].isDigit() && name[ end ].unicode() < 0x80 ) {
            ++end;
        }
        if ( end == i + 1 || end >= name.size() || name[ end ] != QLatin1Char( '}' ) ) {
            continue;
        }
        bool isNumber = false;
        const auto number = QStringView{ name }.mid( i + 1, end - i - 1 ).toInt( &isNumber );
        found( isNumber ? number : -1, i, end + 1 - i );
        i = end;
    }
}

} // namespace

bool isLiteralKey( const QString& key )
{
    static const QString metacharacters = QStringLiteral( "\\^$.|?*+()[]{}#" );
    return std::all_of( key.cbegin(), key.cend(), [ & ]( QChar c ) {
        return c.unicode() > 0x20 && c.unicode() < 0x7f && !metacharacters.contains( c );
    } );
}

QString keyIdentity( const QString& key, bool caseSensitive )
{
    return isLiteralKey( key ) ? folded( key, caseSensitive ) : key;
}

bool hasControlCharacters( QStringView text )
{
    return std::any_of( text.cbegin(), text.cend(), isControlCharacter );
}

QString withoutControlCharacters( QString text )
{
    if ( !hasControlCharacters( text ) ) {
        return text;
    }
    for ( auto& c : text ) {
        if ( isControlCharacter( c ) ) {
            c = QLatin1Char( ' ' );
        }
    }
    return text;
}

// A Name Table compiled: its rows' key regexes, or a hash of its keys when
// all of them are literals. The first row whose key matches wins either way.
struct CompiledTable {
    QString name;
    bool caseSensitive = false;
    bool byHash = false;
    QHash<QString, QString> namesByKey;
    struct Row {
        QRegularExpression key;
        QString name;
    };
    std::vector<Row> rows;

    explicit CompiledTable( const NameTable& table )
        : name{ table.name }
        , caseSensitive{ table.caseSensitive }
    {
        byHash = std::all_of( table.rows.cbegin(), table.rows.cend(),
                              []( const NameRow& row ) { return isLiteralKey( row.key ); } );
        for ( const auto& row : table.rows ) {
            if ( byHash ) {
                auto key = folded( row.key, caseSensitive );
                if ( !namesByKey.contains( key ) ) {
                    namesByKey.insert( std::move( key ), row.name );
                }
                continue;
            }
            auto key = keyRegex( row.key, caseSensitive );
            if ( key.isValid() ) {
                key.optimize();
                rows.push_back( { std::move( key ), row.name } );
            }
        }
    }

    std::optional<QString> nameFor( const QString& value ) const
    {
        if ( byHash ) {
            const auto found = namesByKey.constFind( folded( value, caseSensitive ) );
            if ( found == namesByKey.cend() ) {
                return std::nullopt;
            }
            return withoutControlCharacters( nameWithKeyGroups( *found, { value } ) );
        }
        for ( const auto& row : rows ) {
            const auto match = row.key.match( value );
            if ( !match.hasMatch() ) {
                continue;
            }
            // capturedTexts() ends at the last group that took part: a group
            // after it has to become empty, not stay a reference.
            QStringList keyGroups;
            const auto groupCount = row.key.captureCount();
            keyGroups.reserve( groupCount + 1 );
            for ( int i = 0; i <= groupCount; ++i ) {
                keyGroups.append( match.captured( i ) );
            }
            return withoutControlCharacters( nameWithKeyGroups( row.name, keyGroups ) );
        }
        return std::nullopt;
    }

    // Whether no value could ever get a name from it.
    bool namesNothing() const
    {
        return byHash ? namesByKey.isEmpty() : rows.empty();
    }
};

// A Naming Rule compiled: its regex and, in the order of the capture groups,
// the tables they use.
struct CompiledRule {
    QRegularExpression regex;
    // (capture group, index of its table in CompiledRules::tables)
    std::vector<std::pair<int, size_t>> groupTables;
    QString displayTemplate;
    QString name;
    QString group;
};

struct ValueNamer::CompiledRules {
    std::vector<CompiledTable> tables;
    std::vector<CompiledRule> rules;
};

ValueNamer::ValueNamer( const QList<NamingGroup>& groups )
{
    auto compiled = std::make_shared<CompiledRules>();

    for ( const auto& group : groups ) {
        if ( !group.isEnabled() ) {
            continue;
        }
        // The tables of this group, compiled once for all its rules as they
        // are first used.
        QHash<QString, size_t> tableIndexes;
        for ( const auto& rule : group.rules() ) {
            if ( !rule.enabled ) {
                continue;
            }
            // Line breaks and control characters of the template are shown
            // as spaces, as those of a name are: a Log Line is drawn on one
            // line, whatever a Name Table says.
            CompiledRule compiledRule{ ruleRegex( rule.pattern ),
                                       {},
                                       withoutControlCharacters( rule.displayTemplate ),
                                       rule.name,
                                       group.name() };
            if ( !compiledRule.regex.isValid() ) {
                continue;
            }
            for ( const auto& groupTable : rule.groupTables ) {
                if ( groupTable.table.isEmpty() ) {
                    // No table, which never means a table without a name.
                    continue;
                }
                const auto captureGroup = captureGroupIndex( compiledRule.regex, groupTable.group );
                const auto* table = group.table( groupTable.table );
                if ( captureGroup < 0 || table == nullptr
                     || std::any_of(
                         compiledRule.groupTables.cbegin(), compiledRule.groupTables.cend(),
                         [ captureGroup ]( auto taken ) {
                             return taken.first == captureGroup;
                         } ) ) {
                    continue;
                }
                auto tableIndex = tableIndexes.constFind( table->name );
                if ( tableIndex == tableIndexes.cend() ) {
                    compiled->tables.emplace_back( *table );
                    tableIndex = tableIndexes.insert( table->name, compiled->tables.size() - 1 );
                }
                if ( compiled->tables[ *tableIndex ].namesNothing() ) {
                    continue;
                }
                compiledRule.groupTables.emplace_back( captureGroup, *tableIndex );
            }
            if ( compiledRule.groupTables.empty() ) {
                continue;
            }
            std::sort( compiledRule.groupTables.begin(), compiledRule.groupTables.end() );
            compiledRule.regex.optimize();
            compiled->rules.push_back( std::move( compiledRule ) );
        }
    }

    if ( !compiled->rules.empty() ) {
        rules_ = std::move( compiled );
    }
}

QList<NamedValue> ValueNamer::namedValues( const QString& line ) const
{
    QList<NamedValue> namedValues;
    if ( rules_ == nullptr || line.size() > MaxLineLength ) {
        return namedValues;
    }

    const auto isTaken = [ &namedValues ]( qsizetype start, qsizetype length ) {
        return std::any_of( namedValues.cbegin(), namedValues.cend(),
                            [ start, length ]( const NamedValue& taken ) {
                                return start < taken.end() && taken.start < start + length;
                            } );
    };

    for ( const auto& rule : rules_->rules ) {
        auto matches = rule.regex.globalMatch( line );
        while ( matches.hasNext() ) {
            const auto match = matches.next();
            for ( const auto& [ captureGroup, tableIndex ] : rule.groupTables ) {
                const auto start = match.capturedStart( captureGroup );
                const auto length = match.capturedLength( captureGroup );
                if ( start < 0 || length == 0 || isTaken( start, length ) ) {
                    continue;
                }
                const auto& table = rules_->tables[ tableIndex ];
                auto value = match.captured( captureGroup );
                auto name = table.nameFor( value );
                if ( !name.has_value() ) {
                    continue;
                }
                auto shown = fillTemplate( rule.displayTemplate, *name, value );
                namedValues.append( NamedValue{ start, length, std::move( shown ),
                                                std::move( value ), std::move( *name ), table.name,
                                                rule.name, rule.group } );
            }
        }
    }

    std::sort( namedValues.begin(), namedValues.end(),
               []( const NamedValue& a, const NamedValue& b ) { return a.start < b.start; } );
    return namedValues;
}

QString shownLine( QStringView line, const QList<NamedValue>& namedValues )
{
    QString shown;
    shown.reserve( line.size() );
    qsizetype rawColumn = 0;
    for ( const auto& namedValue : namedValues ) {
        shown.append( line.mid( rawColumn, namedValue.start - rawColumn ) );
        shown.append( namedValue.shown );
        rawColumn = namedValue.end();
    }
    shown.append( line.mid( rawColumn ) );
    return shown;
}

QString nameWithKeyGroups( const QString& name, const QStringList& keyGroups )
{
    QString result;
    qsizetype copied = 0;
    forEachGroupReference( name, [ & ]( int number, qsizetype start, qsizetype length ) {
        if ( number < 1 || number >= keyGroups.size() ) {
            return;
        }
        result.append( QStringView{ name }.mid( copied, start - copied ) );
        result.append( keyGroups[ number ] );
        copied = start + length;
    } );
    if ( copied == 0 ) {
        return name;
    }
    result.append( QStringView{ name }.mid( copied ) );
    return result;
}

QString fillTemplate( const QString& displayTemplate, const QString& name, const QString& value )
{
    static const QString namePlaceholder = QStringLiteral( "{name}" );
    static const QString valuePlaceholder = QStringLiteral( "{value}" );

    QString result;
    result.reserve( displayTemplate.size() + name.size() + value.size() );
    qsizetype i = 0;
    while ( i < displayTemplate.size() ) {
        const QStringView rest = QStringView{ displayTemplate }.mid( i );
        if ( rest.startsWith( namePlaceholder ) ) {
            result.append( name );
            i += namePlaceholder.size();
        }
        else if ( rest.startsWith( valuePlaceholder ) ) {
            result.append( value );
            i += valuePlaceholder.size();
        }
        else {
            result.append( displayTemplate[ i ] );
            ++i;
        }
    }
    return result;
}

QList<Problem> validate( const NamingGroup& group )
{
    QList<Problem> problems;

    QHash<QString, int> ruleNames;
    for ( const auto& rule : group.rules() ) {
        if ( ruleNames.contains( rule.name ) ) {
            problems.append(
                Problem{ Problem::Kind::DuplicateRuleName, rule.name, {}, -1, -1, rule.name } );
        }
        ruleNames.insert( rule.name, 0 );
        if ( hasControlCharacters( rule.displayTemplate ) ) {
            problems.append( Problem{ Problem::Kind::ControlCharacterInTemplate,
                                      rule.name,
                                      {},
                                      -1,
                                      -1,
                                      rule.displayTemplate } );
        }
        const auto regex = ruleRegex( rule.pattern );
        if ( !regex.isValid() ) {
            problems.append( Problem{
                Problem::Kind::InvalidRuleRegex, rule.name, {}, -1, -1, regex.errorString() } );
            continue;
        }
        QList<int> assignedGroups;
        for ( const auto& groupTable : rule.groupTables ) {
            if ( groupTable.table.isEmpty() ) {
                continue;
            }
            if ( group.table( groupTable.table ) == nullptr ) {
                problems.append( Problem{
                    Problem::Kind::UnknownTable, rule.name, {}, -1, -1, groupTable.table } );
            }
            const auto captureGroup = captureGroupIndex( regex, groupTable.group );
            if ( captureGroup < 0 ) {
                problems.append( Problem{
                    Problem::Kind::UnknownCaptureGroup, rule.name, {}, -1, -1, groupTable.group } );
            }
            else if ( assignedGroups.contains( captureGroup ) ) {
                problems.append( Problem{ Problem::Kind::DuplicateCaptureGroup,
                                          rule.name,
                                          {},
                                          -1,
                                          -1,
                                          groupTable.group } );
            }
            else {
                assignedGroups.append( captureGroup );
            }
        }
    }

    for ( const auto& table : group.tables() ) {
        QHash<QString, int> firstRows;
        for ( int row = 0; row < table.rows.size(); ++row ) {
            const auto& nameRow = table.rows[ row ];
            const auto key = keyRegex( nameRow.key, table.caseSensitive );
            if ( !key.isValid() ) {
                problems.append( Problem{
                    Problem::Kind::InvalidKeyRegex, {}, table.name, row, -1, key.errorString() } );
                continue;
            }
            if ( hasControlCharacters( nameRow.name ) ) {
                problems.append( Problem{ Problem::Kind::ControlCharacterInName,
                                          {},
                                          table.name,
                                          row,
                                          -1,
                                          nameRow.name } );
            }
            const auto identity = keyIdentity( nameRow.key, table.caseSensitive );
            const auto firstRow = firstRows.constFind( identity );
            if ( firstRow != firstRows.cend() ) {
                problems.append( Problem{
                    Problem::Kind::DuplicateKey, {}, table.name, row, *firstRow, nameRow.key } );
                continue;
            }
            firstRows.insert( identity, row );

            const auto keyGroups = key.captureCount();
            forEachGroupReference(
                nameRow.name, [ & ]( int number, qsizetype start, qsizetype length ) {
                    if ( number >= 1 && number > keyGroups ) {
                        problems.append( Problem{ Problem::Kind::MissingKeyGroup,
                                                  {},
                                                  table.name,
                                                  row,
                                                  -1,
                                                  nameRow.name.mid( start, length ) } );
                    }
                } );
        }
    }

    return problems;
}

} // namespace logsquirl::valuenames
