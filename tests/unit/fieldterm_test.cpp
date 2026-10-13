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

// Field Terms of a logical combination (#798): a sub-pattern written
// `Name:"pattern"` is applied to the text of the field named, read through
// the FieldReader the pattern carries, and not to the whole line. The
// engine knows no Log Format: the reader here is the test's own.

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <memory>
#include <string>
#include <string_view>

#include <QString>
#include <QStringList>

#include "fieldreader.h"
#include "regularexpression.h"
#include "regularexpressionpattern.h"

namespace {

// Log Lines "<Protocol> <Source> <Message>", the fields split at spaces; a
// line with fewer than two words is not parsed.
class SpaceSeparatedFields : public FieldReader {
public:
    explicit SpaceSeparatedFields( QString identity = QStringLiteral( "test#1" ) )
        : identity_( std::move( identity ) )
    {
    }

    QString identity() const override
    {
        return identity_;
    }

    QStringList fieldNames() const override
    {
        return { QStringLiteral( "Protocol" ), QStringLiteral( "Source" ),
                 QStringLiteral( "Message" ) };
    }

    std::optional<int> fieldIndex( const QString& name ) const override
    {
        const auto index = fieldNames().indexOf( name );
        return index < 0 ? std::nullopt : std::optional<int>( static_cast<int>( index ) );
    }

    bool readFields( const QString& line, QStringList& fields ) const override
    {
        const auto words = line.split( QChar( ' ' ), Qt::SkipEmptyParts );
        if ( words.size() < 2 ) {
            return false;
        }
        fields.clear();
        fields << words[ 0 ] << words[ 1 ]
               << ( words.size() > 2 ? QStringList( words.mid( 2 ) ).join( QChar( ' ' ) )
                                     : QString{} );
        return true;
    }

    std::optional<std::pair<qsizetype, qsizetype>> fieldSpan( const QString& line,
                                                              int field ) const override
    {
        QStringList fields;
        if ( !readFields( line, fields ) || field < 0 || field >= fields.size() ) {
            return std::nullopt;
        }
        const auto start = line.indexOf( fields[ field ] );
        return std::make_pair( start, start + fields[ field ].size() );
    }

private:
    QString identity_;
};

RegularExpressionPattern combination( const QString& text, bool caseSensitive = true,
                                      bool plainText = false, bool inverse = false,
                                      std::shared_ptr<const FieldReader> reader
                                      = std::make_shared<SpaceSeparatedFields>() )
{
    RegularExpressionPattern pattern( text, caseSensitive, inverse, true, plainText );
    pattern.fieldReader = std::move( reader );
    return pattern;
}

bool matches( const PatternMatcher& matcher, const char* line )
{
    return matcher.hasMatch( std::string_view( line ) );
}

} // namespace

SCENARIO( "A Field Term matches the text of its field only", "[regex][fieldterm]" )
{
    const auto engine = GENERATE( RegexpEngine::Vectorscan, RegexpEngine::QRegularExpression );

    GIVEN( "a combination of one Field Term" )
    {
        const RegularExpression expression( combination( R"(Protocol:"DNS")" ), engine );
        REQUIRE( expression.isValid() );
        const auto matcher = expression.createMatcher();

        THEN( "a line whose field has the text matches, one with it elsewhere does not" )
        {
            REQUIRE( matches( *matcher, "DNS 10.0.0.5 query" ) );
            REQUIRE_FALSE( matches( *matcher, "TCP 10.0.0.5 DNS lookup" ) );
            REQUIRE_FALSE( matches( *matcher, "TCP DNS query" ) );
        }

        THEN( "the Field Term is unanchored" )
        {
            REQUIRE( matches( *matcher, "MDNS 10.0.0.5 query" ) );
            REQUIRE( matches( *matcher, "DNSSEC 10.0.0.5 query" ) );
        }

        THEN( "a line the Log Format does not parse matches no Field Term" )
        {
            REQUIRE_FALSE( matches( *matcher, "DNS" ) );
            REQUIRE_FALSE( matches( *matcher, "" ) );
        }
    }

    GIVEN( "two Field Terms combined with and" )
    {
        const RegularExpression expression(
            combination( R"(Protocol:"DNS" and Source:"10\.0\.0\.5")" ), engine );
        REQUIRE( expression.isValid() );
        const auto matcher = expression.createMatcher();

        THEN( "both fields must match" )
        {
            REQUIRE( matches( *matcher, "DNS 10.0.0.5 query" ) );
            REQUIRE_FALSE( matches( *matcher, "DNS 10.0.0.6 query" ) );
            REQUIRE_FALSE( matches( *matcher, "TCP 10.0.0.5 query" ) );
        }
    }

    GIVEN( "a negated Field Term" )
    {
        const RegularExpression expression( combination( R"(not(Protocol:"DNS"))" ), engine );
        REQUIRE( expression.isValid() );
        const auto matcher = expression.createMatcher();

        THEN( "a line with another Protocol matches, and so does a line that does not parse" )
        {
            REQUIRE( matches( *matcher, "TCP 10.0.0.5 query" ) );
            REQUIRE( matches( *matcher, "not a log line" ) );
            REQUIRE( matches( *matcher, "DNS" ) );
            REQUIRE_FALSE( matches( *matcher, "DNS 10.0.0.5 query" ) );
        }
    }

    GIVEN( "a Field Term combined with a plain sub-pattern" )
    {
        const RegularExpression expression( combination( R"(Protocol:"DNS" and "query")" ),
                                            engine );
        REQUIRE( expression.isValid() );
        const auto matcher = expression.createMatcher();

        THEN( "the plain sub-pattern matches the whole line, the Field Term its field" )
        {
            REQUIRE( matches( *matcher, "DNS 10.0.0.5 query" ) );
            REQUIRE( matches( *matcher, "DNS query 10.0.0.5" ) );
            REQUIRE_FALSE( matches( *matcher, "DNS 10.0.0.5 answer" ) );
            REQUIRE_FALSE( matches( *matcher, "TCP 10.0.0.5 query DNS" ) );
        }

        THEN( "each sub-pattern's verdict is told in the order written" )
        {
            using Verdicts = logsquirl::vector<bool>;
            REQUIRE( matcher->subPatternMatches( "DNS 10.0.0.5 answer" )
                     == Verdicts{ true, false } );
            REQUIRE( matcher->subPatternMatches( "TCP 10.0.0.5 query DNS" )
                     == Verdicts{ false, true } );
            REQUIRE( matcher->subPatternMatches( "no" ) == Verdicts{ false, false } );
        }
    }

    GIVEN( "a plain sub-pattern written before a Field Term, with or" )
    {
        const RegularExpression expression( combination( R"("answer" or Source:"10\.0\.0\.6")" ),
                                            engine );
        REQUIRE( expression.isValid() );
        const auto matcher = expression.createMatcher();

        THEN( "either side matches" )
        {
            REQUIRE( matches( *matcher, "DNS 10.0.0.5 answer" ) );
            REQUIRE( matches( *matcher, "DNS 10.0.0.6 query" ) );
            REQUIRE_FALSE( matches( *matcher, "DNS 10.0.0.5 query" ) );
        }
    }

    GIVEN( "an inverted combination of a Field Term" )
    {
        const RegularExpression expression( combination( R"(Protocol:"DNS")", true, false, true ),
                                            engine );
        REQUIRE( expression.isValid() );
        const auto matcher = expression.createMatcher();

        THEN( "the verdict is turned round" )
        {
            REQUIRE_FALSE( matches( *matcher, "DNS 10.0.0.5 query" ) );
            REQUIRE( matches( *matcher, "TCP 10.0.0.5 query" ) );
        }
    }
}

SCENARIO( "Match case and Use regex apply to a Field Term as to any sub-pattern",
          "[regex][fieldterm]" )
{
    const auto engine = GENERATE( RegexpEngine::Vectorscan, RegexpEngine::QRegularExpression );

    GIVEN( "a Field Term that ignores case" )
    {
        const RegularExpression expression( combination( R"(Protocol:"dns")", false ), engine );
        REQUIRE( expression.isValid() );
        const auto matcher = expression.createMatcher();

        THEN( "it matches the field whatever its case" )
        {
            REQUIRE( matches( *matcher, "DNS 10.0.0.5 query" ) );
            REQUIRE( matches( *matcher, "Dns 10.0.0.5 query" ) );
        }
    }

    GIVEN( "a Field Term that matches case" )
    {
        const RegularExpression expression( combination( R"(Protocol:"dns")", true ), engine );
        const auto matcher = expression.createMatcher();

        THEN( "it does not match another case" )
        {
            REQUIRE_FALSE( matches( *matcher, "DNS 10.0.0.5 query" ) );
            REQUIRE( matches( *matcher, "dns 10.0.0.5 query" ) );
        }
    }

    GIVEN( "a Field Term read as plain text" )
    {
        const RegularExpression expression( combination( R"(Source:"10.0.0.5")", true, true ),
                                            engine );
        REQUIRE( expression.isValid() );
        const auto matcher = expression.createMatcher();

        THEN( "the dots are dots, not any character" )
        {
            REQUIRE( matches( *matcher, "DNS 10.0.0.5 query" ) );
            REQUIRE_FALSE( matches( *matcher, "DNS 10x0x0x5 query" ) );
        }
    }

    GIVEN( "a Field Term read as a regular expression" )
    {
        const RegularExpression expression( combination( R"(Source:"^10\.0\.0\.[0-9]$")" ),
                                            engine );
        REQUIRE( expression.isValid() );
        const auto matcher = expression.createMatcher();

        THEN( "it is applied to the field's text, anchors included" )
        {
            REQUIRE( matches( *matcher, "DNS 10.0.0.5 query" ) );
            REQUIRE_FALSE( matches( *matcher, "DNS 10.0.0.55 query" ) );
        }
    }
}

SCENARIO( "A Field Term without its field is a pattern error naming the field",
          "[regex][fieldterm]" )
{
    const auto engine = GENERATE( RegexpEngine::Vectorscan, RegexpEngine::QRegularExpression );

    GIVEN( "a Field Term naming a field the reader lacks" )
    {
        const RegularExpression expression( combination( R"("query" and Port:"53")" ), engine );

        THEN( "the expression is invalid and the error names the field" )
        {
            REQUIRE_FALSE( expression.isValid() );
            REQUIRE( expression.errorString().contains( QStringLiteral( "\"Port\"" ) ) );
        }
    }

    GIVEN( "a Field Term in a pattern without a reader" )
    {
        const RegularExpression expression(
            combination( R"(Protocol:"DNS")", true, false, false, nullptr ), engine );

        THEN( "the expression is invalid and the error names the field" )
        {
            REQUIRE_FALSE( expression.isValid() );
            REQUIRE( expression.errorString().contains( QStringLiteral( "\"Protocol\"" ) ) );
            REQUIRE( expression.errorString().contains( QStringLiteral( "Log Format" ) ) );
        }
    }

    GIVEN( "a Field Term whose sub-pattern is no regular expression" )
    {
        const RegularExpression expression( combination( R"(Protocol:"(unclosed")" ), engine );

        THEN( "the expression is invalid" )
        {
            REQUIRE_FALSE( expression.isValid() );
            REQUIRE_FALSE( expression.errorString().isEmpty() );
        }
    }

    GIVEN( "a combination without a Field Term, in a pattern without a reader" )
    {
        const RegularExpression expression(
            combination( R"("DNS" and not("TCP"))", true, false, false, nullptr ), engine );

        THEN( "it is valid: no field is read" )
        {
            REQUIRE( expression.isValid() );
            REQUIRE( matches( *expression.createMatcher(), "DNS 10.0.0.5 query" ) );
        }
    }
}

SCENARIO( "Outside a logical combination a colon before a quote is text", "[regex][fieldterm]" )
{
    const auto engine = GENERATE( RegexpEngine::Vectorscan, RegexpEngine::QRegularExpression );

    GIVEN( "the plain-text pattern a:b" )
    {
        RegularExpressionPattern pattern( QStringLiteral( "a:b" ), true, false, false, true );
        pattern.fieldReader = std::make_shared<SpaceSeparatedFields>();
        const RegularExpression expression( pattern, engine );
        REQUIRE( expression.isValid() );
        const auto matcher = expression.createMatcher();

        THEN( "it matches the text a:b" )
        {
            REQUIRE( matches( *matcher, "DNS 10.0.0.5 a:b" ) );
            REQUIRE_FALSE( matches( *matcher, "a 10.0.0.5 b" ) );
        }
    }

    GIVEN( "a colon inside a quoted sub-pattern" )
    {
        const RegularExpression expression( combination( R"("key:value")" ), engine );
        REQUIRE( expression.isValid() );

        THEN( "it is part of the sub-pattern, which matches the whole line" )
        {
            REQUIRE( matches( *expression.createMatcher(), "DNS 10.0.0.5 key:value" ) );
            REQUIRE( logicalFieldTerms( R"("key:value")" ) == QStringList{ QString{} } );
        }
    }
}

SCENARIO( "The Field Terms of a combination are read back as written", "[regex][fieldterm]" )
{
    GIVEN( "a combination with a Field Term, a plain sub-pattern and a negated Field Term" )
    {
        const auto text = R"(Protocol:"DNS" and "query" and not(Source:"10\.0\.0\.5"))";

        THEN( "the sub-patterns are the quoted parts and the fields are named per sub-pattern" )
        {
            REQUIRE( logicalSubPatterns( text )
                     == QStringList{ QStringLiteral( "DNS" ), QStringLiteral( "query" ),
                                     QStringLiteral( R"(10\.0\.0\.5)" ) } );
            REQUIRE( logicalFieldTerms( text )
                     == QStringList{ QStringLiteral( "Protocol" ), QString{},
                                     QStringLiteral( "Source" ) } );
        }
    }

    GIVEN( "a field name with a dot and a sub-pattern with a quote" )
    {
        THEN( "writing the Field Term and reading it back agree" )
        {
            const auto written
                = writeFieldTerm( QStringLiteral( "http.path" ), QStringLiteral( R"(say "hi")" ) );
            REQUIRE( written == QStringLiteral( R"(http.path:"say \"hi\"")" ) );
            REQUIRE( logicalFieldTerms( written ) == QStringList{ QStringLiteral( "http.path" ) } );
            REQUIRE( logicalSubPatterns( written )
                     == QStringList{ QStringLiteral( R"(say "hi")" ) } );
        }
    }

    GIVEN( "a pattern that is no logical combination" )
    {
        THEN( "there are no Field Terms" )
        {
            REQUIRE( logicalFieldTerms( QStringLiteral( "no quotes" ) ).isEmpty() );
        }
    }
}

SCENARIO( "A pattern's identity includes its field reader's", "[regex][fieldterm]" )
{
    GIVEN( "the same combination with readers of two Log Formats, and without one" )
    {
        const auto a = combination( R"(Protocol:"DNS")", true, false, false,
                                    std::make_shared<SpaceSeparatedFields>( "format-a" ) );
        const auto sameAsA = combination( R"(Protocol:"DNS")", true, false, false,
                                          std::make_shared<SpaceSeparatedFields>( "format-a" ) );
        const auto b = combination( R"(Protocol:"DNS")", true, false, false,
                                    std::make_shared<SpaceSeparatedFields>( "format-b" ) );
        const auto none = combination( R"(Protocol:"DNS")", true, false, false, nullptr );

        THEN( "patterns under the same Log Format are equal, the others are not" )
        {
            REQUIRE( a == sameAsA );
            REQUIRE_FALSE( a == b );
            REQUIRE_FALSE( a == none );
            REQUIRE( none.fieldReaderIdentity().isEmpty() );
            REQUIRE( a.fieldReaderIdentity() == QStringLiteral( "format-a" ) );
        }
    }
}
