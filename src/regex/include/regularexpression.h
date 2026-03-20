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

#ifndef LOGSQUIRL_PATTERN_MATHCHER_H
#define LOGSQUIRL_PATTERN_MATHCHER_H

#include <memory>
#include <qchar.h>
#include <string_view>
#include <unordered_map>

#include <QString>

#include "containers.h"

#include "hsregularexpression.h"
#include "regularexpressionpattern.h"


class PatternMatcher;
class MultiPatternMatcher;
class BooleanExpressionEvaluator;

class RegularExpression {
  public:
    explicit RegularExpression( const RegularExpressionPattern& pattern );

    std::unique_ptr<PatternMatcher> createMatcher() const;

    bool isValid() const;
    QString errorString() const;

  private:
    bool isInverse_ = false;
    bool isBooleanCombination_ = false;

    QString expression_;
    logsquirl::vector<RegularExpressionPattern> subPatterns_;

    bool isValid_ = false;
    QString errorString_;

    HsRegularExpression hsExpression_;

    friend class PatternMatcher;
};

class PatternMatcher {
  public:
    explicit PatternMatcher( const RegularExpression& expression );
    ~PatternMatcher();

    bool hasMatch( std::string_view line ) const;

  private:
    using MatchFunc = bool ( * )( std::string_view line, const MatcherVariant& matcher, BooleanExpressionEvaluator* evaluator );
    MatchFunc hasMatchImpl_;

  private:
    bool isInverse_ = false;
    bool isBooleanCombination_ = false;

    std::string mainPatternId_;

    MatcherVariant matcher_;
    std::unique_ptr<BooleanExpressionEvaluator> evaluator_;
};

class MultiRegularExpression {
  public:
    explicit MultiRegularExpression( const logsquirl::vector<RegularExpressionPattern>& patterns );

    std::unique_ptr<MultiPatternMatcher> createMatcher() const;

    bool isValid() const;
    QString errorString() const;

  private:
    logsquirl::vector<RegularExpressionPattern> patterns_;

    bool isValid_ = false;
    QString errorString_;

    HsRegularExpression hsExpression_;

    friend class MultiPatternMatcher;
};

class MultiPatternMatcher {
  public:
    explicit MultiPatternMatcher( const MultiRegularExpression& expression );
    ~MultiPatternMatcher();

    logsquirl::vector<std::pair<RegularExpressionPattern, bool>> match( std::string_view line ) const;

  private:
    MatcherVariant matcher_;
    logsquirl::vector<RegularExpressionPattern> patterns_;
};

#endif