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

#pragma once

#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <QByteArrayView>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include "fieldreader.h"
#include "hsregularexpression.h"

// A Field Term of a logical combination, compiled: the field it reads and
// the sub-pattern applied to the field's text, unanchored, with the
// combination's case and regexp options.
struct CompiledFieldTerm {
    int field = 0;
    QRegularExpression regexp;
};

// Matches a logical combination with Field Terms (#798): the plain
// sub-patterns on the whole line with the engine's own matcher, the Field
// Terms each on the text of its field, read once per line through the
// FieldReader. What it gives is what the combination's evaluator reads: one
// verdict per sub-pattern, in the order written.
//
// An alternative of the matcher variant beside the engine's own, so that a
// matcher without Field Terms runs the very same code as before: nothing is
// looked at per line to tell the two apart.
class FieldTermMatcher {
public:
    // lines matches the plain sub-patterns, in the order written, none when
    // the combination has only Field Terms. fieldOfSubPattern has one entry
    // per sub-pattern of the combination: -1 for a plain one, else the index
    // into terms.
    FieldTermMatcher( std::optional<LineMatcherVariant> lines,
                      std::shared_ptr<const FieldReader> reader, std::vector<int> fieldOfSubPattern,
                      std::vector<CompiledFieldTerm> terms )
        : lines_( std::move( lines ) )
        , reader_( std::move( reader ) )
        , fieldOfSubPattern_( std::move( fieldOfSubPattern ) )
        , terms_( std::move( terms ) )
    {
    }

    MatchedPatterns match( const std::string_view& utf8Data ) const
    {
        MatchedPatterns matched( fieldOfSubPattern_.size(), 0 );

        MatchedPatterns lineMatched;
        gaveUp_ = false;
        if ( lines_.has_value() ) {
            std::visit(
                [ &lineMatched, &utf8Data, this ]( const auto& matcher ) {
                    lineMatched = matcher.match( utf8Data );
                    gaveUp_ = matcher.gaveUp();
                },
                *lines_ );
        }

        // The fields are read once, for every Field Term of the combination.
        const auto line = QString::fromUtf8( QByteArrayView( utf8Data ) );
        const bool parsed = reader_->readFields( line, fields_ );

        std::size_t nextLineVerdict = 0;
        for ( std::size_t subPattern = 0; subPattern < fieldOfSubPattern_.size(); ++subPattern ) {
            const auto term = fieldOfSubPattern_[ subPattern ];
            if ( term < 0 ) {
                matched[ subPattern ] = nextLineVerdict < lineMatched.size()
                                            ? lineMatched[ nextLineVerdict ]
                                            : static_cast<char>( 0 );
                ++nextLineVerdict;
            }
            else if ( parsed ) {
                // A line the Log Format does not parse has no fields: no
                // Field Term matches it.
                const auto& compiled = terms_[ static_cast<std::size_t>( term ) ];
                const auto fieldMatch = compiled.regexp.match( fields_.at( compiled.field ) );
                gaveUp_ = gaveUp_ || engineGaveUp( fieldMatch );
                matched[ subPattern ] = fieldMatch.hasMatch() ? 1 : 0;
            }
        }
        return matched;
    }

    // Whether an engine gave up on a sub-pattern in the last match(): what
    // it reported for that one is "no match", not a verdict.
    bool gaveUp() const
    {
        return gaveUp_;
    }

private:
    std::optional<LineMatcherVariant> lines_;
    std::shared_ptr<const FieldReader> reader_;
    std::vector<int> fieldOfSubPattern_;
    std::vector<CompiledFieldTerm> terms_;
    // The fields of the line matched last: one matcher matches one line at
    // a time, so the list is kept and filled anew rather than made per line.
    mutable QStringList fields_;
    mutable bool gaveUp_ = false;
};

namespace fieldterm_detail {
template <class Variant, class... Added>
struct VariantWith;
template <class... Alternatives, class... Added>
struct VariantWith<std::variant<Alternatives...>, Added...> {
    using type = std::variant<Alternatives..., Added...>;
};
} // namespace fieldterm_detail

// What a PatternMatcher matches with: the engine's own matchers, or one with
// Field Terms around them.
using MatcherVariant = fieldterm_detail::VariantWith<LineMatcherVariant, FieldTermMatcher>::type;

// The engine's matcher as a PatternMatcher holds it.
inline MatcherVariant widenMatcher( LineMatcherVariant&& lines )
{
    return std::visit(
        []( auto&& matcher ) -> MatcherVariant {
            return MatcherVariant{ std::forward<decltype( matcher )>( matcher ) };
        },
        std::move( lines ) );
}
