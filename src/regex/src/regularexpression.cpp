/*
 * Copyright (C) 2021 Anton Filimonov and other contributors
 *
 * This file is part of logsquirl.
 *
 * logsquirl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * logsquirl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with logsquirl.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <algorithm>
#include <exception>
#include <memory>
#include <qregularexpression.h>
#include <string>
#include <variant>

#include "containers.h"
#include "log.h"
#include "regularexpressionpattern.h"
#include "uuid.h"

#include "booleanevaluator.h"
#include "regularexpression.h"

namespace {

// A sub-pattern of a logical combination is enclosed in quotes. A quote
// inside it is written \", and a run of backslashes right before a quote --
// an inner one or the closing one -- is written doubled, so that a
// sub-pattern ending in a backslash does not escape its closing quote. A
// backslash anywhere else is read as written: "\d+" stays a regexp class
// (#405). quoteSubPattern() writes a sub-pattern this way.

// The number of backslashes right before index.
qsizetype backslashesBefore( const QString& pattern, qsizetype index )
{
    qsizetype count = 0;
    while ( index - count > 0 && pattern[ index - count - 1 ] == QChar( '\\' ) ) {
        ++count;
    }
    return count;
}

// A quote after an odd run of backslashes is part of the sub-pattern.
bool isEscapedQuote( const QString& pattern, qsizetype quote )
{
    return backslashesBefore( pattern, quote ) % 2 == 1;
}

// The sub-pattern written between its quotes, as it is meant: each run of
// backslashes right before a quote or at its end read two for one.
QString unescapeSubPattern( const QString& written )
{
    QString subPattern;
    subPattern.reserve( written.size() );

    qsizetype index = 0;
    while ( index < written.size() ) {
        if ( written[ index ] != QChar( '\\' ) ) {
            subPattern.append( written[ index ] );
            ++index;
            continue;
        }

        auto runEnd = index;
        while ( runEnd < written.size() && written[ runEnd ] == QChar( '\\' ) ) {
            ++runEnd;
        }
        auto run = runEnd - index;
        if ( runEnd == written.size() || written[ runEnd ] == QChar( '"' ) ) {
            // An odd run escapes the quote after it, which follows as is.
            run /= 2;
        }
        subPattern.append( QString( run, QChar( '\\' ) ) );
        index = runEnd;
    }

    return subPattern;
}

logsquirl::vector<RegularExpressionPattern>
parseBooleanExpressions( QString& pattern, bool isCaseSensitive, bool isPlainText )
{
    if ( !pattern.contains( '"' ) ) {
        throw std::runtime_error( "Patterns must be enclosed in quotes" );
    }

    logsquirl::vector<RegularExpressionPattern> subPatterns;
    subPatterns.reserve( static_cast<size_t>( pattern.size() ) );

    int currentIndex = 0;
    int leftQuote = -1;
    int rightQuote = -1;

    while ( currentIndex < pattern.size() ) {
        leftQuote = type_safe::narrow_cast<int>( pattern.indexOf( QChar( '"' ), currentIndex ) );
        if ( leftQuote < 0 ) {
            break;
        }

        currentIndex = leftQuote + 1;
        if ( isEscapedQuote( pattern, leftQuote ) ) {
            leftQuote = -1;
            continue;
        }

        while ( currentIndex < pattern.size() ) {
            rightQuote
                = type_safe::narrow_cast<int>( pattern.indexOf( QChar( '"' ), currentIndex ) );
            if ( rightQuote < 0 ) {
                break;
            }

            currentIndex = rightQuote + 1;
            if ( isEscapedQuote( pattern, rightQuote ) ) {
                rightQuote = -1;
                continue;
            }

            break;
        }

        if ( rightQuote < 0 ) {
            break;
        }

        const auto subPatternLength = rightQuote - leftQuote - 1;
        const auto subPattern
            = unescapeSubPattern( pattern.mid( leftQuote + 1, subPatternLength ) );

        subPatterns.emplace_back( subPattern, isCaseSensitive, false, false, isPlainText );

        pattern.replace( leftQuote, subPatternLength + 2,
                         QString::fromStdString( subPatterns.back().id() ) );

        currentIndex = 0;
        leftQuote = -1;
        rightQuote = -1;
    }

    if ( pattern.contains( '"' ) ) {
        throw std::runtime_error( "Pattern has unmatched quotes" );
    }

    pattern = pattern.toLower();
    LOG_INFO << "Parsed pattern: " << pattern;
    QRegularExpression finalPatternCheck( "^(and|nand|or|nor|xor|xnor|not|[ ()!|&]|p_[0-9]+)+$" );
    if ( !finalPatternCheck.match( pattern ).hasMatch() ) {
        throw std::runtime_error( "Sub-patterns must be enclosed in quotes" );
    }

    return subPatterns;
}

} // namespace

QStringList logicalSubPatterns( const QString& combination )
{
    auto expression = combination;
    QStringList subPatterns;
    try {
        for ( const auto& subPattern : parseBooleanExpressions( expression, true, false ) ) {
            subPatterns.append( subPattern.pattern );
        }
    } catch ( const std::exception& ) {
        return {};
    }
    return subPatterns;
}

QString quoteSubPattern( const QString& subPattern )
{
    QString quoted;
    quoted.reserve( subPattern.size() + 2 );
    quoted.append( QChar( '"' ) );

    qsizetype index = 0;
    while ( index < subPattern.size() ) {
        if ( subPattern[ index ] == QChar( '"' ) ) {
            quoted.append( QChar( '\\' ) ).append( QChar( '"' ) );
            ++index;
        }
        else if ( subPattern[ index ] == QChar( '\\' ) ) {
            auto runEnd = index;
            while ( runEnd < subPattern.size() && subPattern[ runEnd ] == QChar( '\\' ) ) {
                ++runEnd;
            }
            auto run = runEnd - index;
            if ( runEnd == subPattern.size() || subPattern[ runEnd ] == QChar( '"' ) ) {
                run *= 2;
            }
            quoted.append( QString( run, QChar( '\\' ) ) );
            index = runEnd;
        }
        else {
            quoted.append( subPattern[ index ] );
            ++index;
        }
    }

    return quoted.append( QChar( '"' ) );
}

QStringList regexpAlternatives( const QString& regexp )
{
    QStringList alternatives;
    const auto addAlternative = [ & ]( qsizetype from, qsizetype to ) {
        if ( to > from ) {
            alternatives.append( regexp.mid( from, to - from ) );
        }
    };

    qsizetype alternativeStart = 0;
    qsizetype groupDepth = 0;
    // Where the character class opened, its [ and a ^ after it passed; a ]
    // right there is a character of the class, not its end.
    qsizetype classStart = -1;
    for ( qsizetype i = 0; i < regexp.size(); ++i ) {
        const auto c = regexp[ i ];
        if ( c == '\\' ) {
            if ( i + 1 < regexp.size() && regexp[ i + 1 ] == 'Q' ) {
                // \Q...\E quotes everything up to \E, or to the end.
                const auto quoteEnd = regexp.indexOf( QLatin1String( "\\E" ), i + 2 );
                i = quoteEnd < 0 ? regexp.size() : quoteEnd + 1;
            }
            else {
                ++i;
            }
        }
        else if ( classStart >= 0 ) {
            if ( c == '[' && i + 1 < regexp.size() && regexp[ i + 1 ] == ':' ) {
                // A POSIX class such as [:alpha:] inside the character class.
                const auto posixEnd = regexp.indexOf( QLatin1String( ":]" ), i + 2 );
                if ( posixEnd >= 0 ) {
                    i = posixEnd + 1;
                }
            }
            else if ( c == ']' && i > classStart ) {
                classStart = -1;
            }
        }
        else if ( c == '[' ) {
            classStart = i + 1;
            if ( classStart < regexp.size() && regexp[ classStart ] == '^' ) {
                ++classStart;
            }
        }
        else if ( c == '(' ) {
            ++groupDepth;
        }
        else if ( c == ')' ) {
            groupDepth = std::max<qsizetype>( groupDepth - 1, 0 );
        }
        else if ( c == '|' && groupDepth == 0 ) {
            addAlternative( alternativeStart, i );
            alternativeStart = i + 1;
        }
    }
    addAlternative( alternativeStart, regexp.size() );
    return alternatives;
}

RegularExpression::RegularExpression( const RegularExpressionPattern& pattern, RegexpEngine engine )
    : isInverse_( pattern.isExclude )
    , isBooleanCombination_( pattern.isBoolean )
    , engine_( engine )
    , expression_( pattern.pattern )
{
    try {
        if ( pattern.isBoolean ) {
            subPatterns_ = parseBooleanExpressions( expression_, pattern.isCaseSensitive,
                                                    pattern.isPlainText );

            BooleanExpressionEvaluator evaluator{ expression_.toStdString(), subPatterns_ };
            if ( !evaluator.isValid() ) {
                isValid_ = false;
                errorString_ = QString::fromStdString( evaluator.errorString() );
                return;
            }
        }
        else {
            subPatterns_.emplace_back( pattern );
            expression_ = QString::fromStdString( subPatterns_.front().id() );
        }

        hsExpression_ = HsRegularExpression( subPatterns_ );
        isValid_ = hsExpression_.isValid();
        errorString_ = hsExpression_.errorString();

        if ( engine_ != RegexpEngine::Vectorscan ) {
            qtRegexps_ = compileRegularExpressions( subPatterns_ );
        }

    } catch ( std::exception& err ) {
        isValid_ = false;
        errorString_ = err.what();
    }
}

bool RegularExpression::isValid() const
{
    return isValid_;
}

QString RegularExpression::errorString() const
{
    return errorString_;
}

std::unique_ptr<PatternMatcher> RegularExpression::createMatcher() const
{
    return std::make_unique<PatternMatcher>( *this );
}

namespace matching {

bool hasSingleMatch( std::string_view line, const MatcherVariant& matcher,
                     BooleanExpressionEvaluator* )
{
    const auto result
        = std::visit( [ &line ]( const auto& m ) { return m.match( line ); }, matcher );

    return !result.empty() && result[ 0 ] > 0;
}

bool hasCombinedMatch( std::string_view line, const MatcherVariant& matcher,
                       BooleanExpressionEvaluator* evaluator )
{
    auto result = std::visit( [ &line ]( const auto& m ) { return m.match( line ); }, matcher );
    return evaluator && evaluator->evaluate( result );
}

bool hasInverseSingleMatch( std::string_view line, const MatcherVariant& matcher,
                            BooleanExpressionEvaluator* evaluator )
{
    return !hasSingleMatch( line, matcher, evaluator );
}

bool hasInverseCombinedMatch( std::string_view line, const MatcherVariant& matcher,
                              BooleanExpressionEvaluator* evaluator )
{
    return !hasCombinedMatch( line, matcher, evaluator );
}

} // namespace matching

PatternMatcher::PatternMatcher( const RegularExpression& expression )
    : isInverse_( expression.isInverse_ )
    , isBooleanCombination_( expression.isBooleanCombination_ )
    , mainPatternId_( expression.subPatterns_.front().id() )
    , matcher_( expression.engine_ == RegexpEngine::Vectorscan
                    ? expression.hsExpression_.createMatcher()
                    : MatcherVariant{ DefaultRegularExpressionMatcher( expression.qtRegexps_ ) } )
{

    if ( expression.isBooleanCombination_ ) {
        evaluator_ = std::make_unique<BooleanExpressionEvaluator>(
            expression.expression_.toStdString(), expression.subPatterns_ );
    }

    if ( !isBooleanCombination_ ) {
        hasMatchImpl_ = isInverse_ ? matching::hasInverseSingleMatch : matching::hasSingleMatch;
    }
    else {
        hasMatchImpl_ = isInverse_ ? matching::hasInverseCombinedMatch : matching::hasCombinedMatch;
    }
}

PatternMatcher::~PatternMatcher() = default;

bool PatternMatcher::hasMatch( std::string_view line ) const
{
    return hasMatchImpl_( line, matcher_, evaluator_.get() );
}

MultiRegularExpression::MultiRegularExpression(
    const logsquirl::vector<RegularExpressionPattern>& patterns )
    : patterns_( patterns )
{
    try {
        hsExpression_ = HsRegularExpression( patterns_ );
        isValid_ = hsExpression_.isValid();
        errorString_ = hsExpression_.errorString();

    } catch ( std::exception& err ) {
        isValid_ = false;
        errorString_ = err.what();
    }
}

bool MultiRegularExpression::isValid() const
{
    return isValid_;
}

QString MultiRegularExpression::errorString() const
{
    return errorString_;
}

std::unique_ptr<MultiPatternMatcher> MultiRegularExpression::createMatcher() const
{
    return std::make_unique<MultiPatternMatcher>( *this );
}

MultiPatternMatcher::MultiPatternMatcher( const MultiRegularExpression& expression )
    : matcher_( expression.hsExpression_.createMatcher() )
    , patterns_( expression.patterns_ )
{
}

MultiPatternMatcher::~MultiPatternMatcher() = default;

logsquirl::vector<std::pair<RegularExpressionPattern, bool>>
MultiPatternMatcher::match( std::string_view line ) const
{
    const auto result
        = std::visit( [ &line ]( const auto& m ) { return m.match( line ); }, matcher_ );

    logsquirl::vector<std::pair<RegularExpressionPattern, bool>> matchedPatterns;
    for ( size_t i = 0u; i < result.size(); ++i ) {
        matchedPatterns.emplace_back( patterns_[ i ], result[ i ] );
    }

    return matchedPatterns;
}
