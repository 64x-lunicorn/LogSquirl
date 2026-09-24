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

#ifndef LOGSQUIRL_HS_REGULAR_EXPRESSION
#define LOGSQUIRL_HS_REGULAR_EXPRESSION

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <QRegularExpression>
#include <QString>

#include "containers.h"

#ifdef LOGSQUIRL_HAS_HS
#include <hs.h>

#include "resourcewrapper.h"
#endif

#include "regularexpressionpattern.h"

using MatchedPatterns = std::string;

using CompiledRegularExpressions = logsquirl::vector<QRegularExpression>;

// Compiles every pattern once, for all the matchers an expression creates.
// Those matchers share the compiled patterns (QRegularExpression is implicitly
// shared) and may match on several search threads at once, so optimize()
// compiles and JITs each pattern here, while only one thread has it: every
// match after that only reads the compiled pattern, with a JIT stack per
// thread. A pattern that does not compile stays invalid and reports why.
inline CompiledRegularExpressions
compileRegularExpressions( const logsquirl::vector<RegularExpressionPattern>& patterns )
{
    CompiledRegularExpressions regexps;
    regexps.reserve( patterns.size() );
    for ( const auto& pattern : patterns ) {
        regexps.push_back( static_cast<QRegularExpression>( pattern ) );
        regexps.back().optimize();
    }
    return regexps;
}

class DefaultRegularExpressionMatcher {
public:
    explicit DefaultRegularExpressionMatcher( CompiledRegularExpressions regexps )
        : regexp_( std::move( regexps ) )
    {
    }

    MatchedPatterns match( const std::string_view& utf8Data ) const
    {
        // One conversion per Log Line, however many sub-patterns match it.
        const auto line = QString::fromUtf8( QByteArrayView( utf8Data ) );

        MatchedPatterns matchedPatterns( regexp_.size(), 0 );
        std::transform(
            regexp_.cbegin(), regexp_.cend(), matchedPatterns.begin(),
            [ &line ]( const auto& regexp ) { return regexp.match( line ).hasMatch(); } );

        return matchedPatterns;
    }

private:
    CompiledRegularExpressions regexp_;
};

#ifdef LOGSQUIRL_HAS_HS

using HsScratch = UniqueResource<hs_scratch_t, hs_free_scratch>;
using HsDatabase = SharedResource<hs_database_t>;

struct HsMatcherContext {

    HsMatcherContext( std::size_t numberOfPatterns = 1 );

    void reset();

    MatchedPatterns matchingPatterns;

private:
    MatchedPatterns matchingPatternsTemplate_;
};

class HsMatcher {
public:
    HsMatcher() = default;
    HsMatcher( HsDatabase database, HsScratch scratch, std::size_t numberOfPatterns );

    HsMatcher( const HsMatcher& ) = delete;
    HsMatcher& operator=( const HsMatcher& ) = delete;

    HsMatcher( HsMatcher&& other ) noexcept = default;
    HsMatcher& operator=( HsMatcher&& other ) noexcept = default;

protected:
    HsDatabase database_;
    HsScratch scratch_;

    mutable HsMatcherContext context_;
};

class HsSingleMatcher : public HsMatcher {
public:
    HsSingleMatcher() = default;
    HsSingleMatcher( HsDatabase database, HsScratch scratch );

    MatchedPatterns match( const std::string_view& utf8Data ) const;
};

class HsMultiMatcher : public HsMatcher {
public:
    HsMultiMatcher() = default;
    HsMultiMatcher( HsDatabase database, HsScratch scratch, std::size_t numberOfPatterns );

    MatchedPatterns match( const std::string_view& utf8Data ) const;
};

class HsNoopMatcher {
public:
    MatchedPatterns match( const std::string_view& utf8Data ) const;
};

class HsPrefilterMatcher {
public:
    HsPrefilterMatcher( CompiledRegularExpressions regexps, HsMultiMatcher&& hsMatcher );

    MatchedPatterns match( const std::string_view& utf8Data ) const;

private:
    CompiledRegularExpressions regexps_;
    HsMultiMatcher hsMatcher_;
};

using MatcherVariant = std::variant<DefaultRegularExpressionMatcher, HsNoopMatcher, HsSingleMatcher,
                                    HsMultiMatcher, HsPrefilterMatcher>;

class HsRegularExpression {
public:
    HsRegularExpression() = default;
    explicit HsRegularExpression( const RegularExpressionPattern& includePattern );
    explicit HsRegularExpression( const logsquirl::vector<RegularExpressionPattern>& patterns );

    HsRegularExpression( const HsRegularExpression& ) = delete;
    HsRegularExpression& operator=( const HsRegularExpression& ) = delete;

    HsRegularExpression( HsRegularExpression&& other ) = default;
    HsRegularExpression& operator=( HsRegularExpression&& other ) = default;

    bool isValid() const;
    QString errorString() const;

    MatcherVariant createMatcher() const;

private:
    bool isHsValid() const;

private:
    HsDatabase database_;
    HsScratch scratch_;

    logsquirl::vector<RegularExpressionPattern> patterns_;
    // Compiled only when matching needs QRegularExpression: Vectorscan could
    // not take the patterns, or took them only as a prefilter.
    CompiledRegularExpressions regexps_;

    bool isValid_ = true;
    QString errorMessage_;

    bool isPrefilter_ = false;
};
#else

using MatcherVariant = std::variant<DefaultRegularExpressionMatcher>;

class HsRegularExpression {
public:
    HsRegularExpression() = default;

    explicit HsRegularExpression( const RegularExpressionPattern& includePattern )
        : HsRegularExpression( logsquirl::vector<RegularExpressionPattern>{ includePattern } )
    {
    }

    explicit HsRegularExpression( const logsquirl::vector<RegularExpressionPattern>& patterns )
        : regexps_( compileRegularExpressions( patterns ) )
    {
        for ( const auto& regex : regexps_ ) {
            if ( !regex.isValid() ) {
                isValid_ = false;
                errorString_ = regex.errorString();
                break;
            }
        }
    }

    bool isValid() const
    {
        return isValid_;
    }

    QString errorString() const
    {
        return errorString_;
    }

    MatcherVariant createMatcher() const
    {
        return MatcherVariant{ DefaultRegularExpressionMatcher( regexps_ ) };
    }

private:
    bool isValid_ = true;
    QString errorString_;

    CompiledRegularExpressions regexps_;
};

#endif

#endif