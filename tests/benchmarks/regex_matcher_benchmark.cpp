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

// Micro-benchmarks for matching a block of Log Lines the way a Search and the
// Highlighters do (#279): a pattern Vectorscan rejects (a lookahead), which a
// Search runs through Vectorscan as a prefilter and confirms with
// QRegularExpression, and boolean expressions of several sub-patterns -- on
// both regex engines.
//
// Uses only what the regex module offered before #279, so the same file
// measures both sides of an A/B comparison. See tests/benchmarks/README.md.

#include "regexpengine.h"
#include "regularexpression.h"
#include "regularexpressionpattern.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#define CATCH_CONFIG_ENABLE_BENCHMARKING
#define CATCH_CONFIG_MAIN
#include <catch2/catch.hpp>

namespace {

constexpr int LogLineCount = 20'000;
constexpr int HighlightedLogLineCount = 2'000;

// A block of Log Lines as a Search holds it: one UTF-8 buffer, one view per
// Log Line. One Log Line in ten is DEBUG, one in seven is an ERROR.
class LogLineBlock {
public:
    LogLineBlock()
    {
        std::vector<std::size_t> ends;
        for ( int line = 0; line < LogLineCount; ++line ) {
            const auto number = std::to_string( line );
            if ( line % 10 == 0 ) {
                buffer_ += "2026-09-17 12:34:56." + number + " DEBUG [worker-" + number
                           + "] cache lookup for größe took twelve milliseconds";
            }
            else if ( line % 7 == 0 ) {
                buffer_ += "2026-09-17 12:34:56." + number + " ERROR [worker-" + number
                           + "] request failed, retrying in a moment";
            }
            else {
                buffer_ += "2026-09-17 12:34:56." + number + " INFO  [worker-" + number
                           + "] request served in twelve milliseconds";
            }
            ends.push_back( buffer_.size() );
        }

        std::size_t start = 0;
        for ( const auto end : ends ) {
            lines_.emplace_back( buffer_.data() + start, end - start );
            start = end;
        }
    }

    const std::vector<std::string_view>& lines() const
    {
        return lines_;
    }

private:
    std::string buffer_;
    std::vector<std::string_view> lines_;
};

std::string engineName( RegexpEngine engine )
{
    return engine == RegexpEngine::Vectorscan ? "Vectorscan" : "QRegularExpression";
}

// One matcher for the whole block, as each search thread holds one.
void benchmarkSearch( const std::string& name, const LogLineBlock& block,
                      const RegularExpressionPattern& pattern, RegexpEngine engine )
{
    const RegularExpression expression( pattern, engine );
    REQUIRE( expression.isValid() );

    BENCHMARK( name + ", " + engineName( engine ) )
    {
        const auto matcher = expression.createMatcher();
        int matches = 0;
        for ( const auto line : block.lines() ) {
            matches += matcher->hasMatch( line ) ? 1 : 0;
        }
        return matches;
    };
}

} // namespace

TEST_CASE( "Searching a block of Log Lines", "[regex-benchmark][search]" )
{
    const LogLineBlock block;

    const RegularExpressionPattern lookahead( "^(?!.*DEBUG)", true, false, false, false );

    const RegularExpressionPattern booleanOfRegexes(
        "(\"ERROR|INFO\") and (\"worker-[0-9]+\") and not (\"twelve\") and not (\"DEBUG\")", true,
        false, true, false );

    const RegularExpressionPattern booleanWithLookahead(
        "(\"^(?!.*DEBUG)\") and (\"worker-[0-9]+\") and not (\"twelve\")", true, false, true,
        false );

    for ( const auto engine : { RegexpEngine::Vectorscan, RegexpEngine::QRegularExpression } ) {
        benchmarkSearch( "lookahead", block, lookahead, engine );
        benchmarkSearch( "boolean expression of four sub-patterns", block, booleanOfRegexes,
                         engine );
        benchmarkSearch( "boolean expression with a lookahead", block, booleanWithLookahead,
                         engine );
    }
}

TEST_CASE( "Highlighting Log Lines", "[regex-benchmark][highlight]" )
{
    const LogLineBlock block;

    // A Highlighter Set of three Highlighters, one with a lookahead. The
    // Highlighter Set on origin/master creates a matcher for every Log Line it
    // colors.
    const MultiRegularExpression expression( {
        RegularExpressionPattern( "^(?!.*DEBUG).*ERROR", true, false, false, false ),
        RegularExpressionPattern( "worker-[0-9]+", true, false, false, false ),
        RegularExpressionPattern( "milliseconds", false, false, false, false ),
    } );

    BENCHMARK( "three Highlighters, one with a lookahead, a matcher per Log Line" )
    {
        int matches = 0;
        for ( int line = 0; line < HighlightedLogLineCount; ++line ) {
            const auto matcher = expression.createMatcher();
            for ( const auto& [ pattern, matched ] :
                  matcher->match( block.lines()[ static_cast<std::size_t>( line ) ] ) ) {
                matches += matched ? 1 : 0;
            }
        }
        return matches;
    };
}
