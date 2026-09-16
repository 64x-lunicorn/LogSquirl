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

#include <catch2/catch.hpp>

#include "quickfindwidget.h"
#include "test_policies.h"

#include <QCheckBox>
#include <QLineEdit>
#include <QSignalSpy>
#include <QTest>

// The QuickFind bar holds a QuickFind Policy and reads no setting of its own
// (#185): the Policy says whether case is ignored and how the text the user
// types is read. The bar still *writes* the ignore-case setting when the user
// toggles it -- the seam forbids reading an undeclared setting, not writing a
// declared one.

namespace {

// The bar and the two children a scenario drives it through.
struct BarFixture {
    QuickFindWidget widget;
    QCheckBox* ignoreCaseCheck = widget.findChild<QCheckBox*>();
    QLineEdit* patternEdit = widget.findChild<QLineEdit*>();
    QuickFindPolicy policy = testSettingsPolicies().quickFind;

    BarFixture()
    {
        REQUIRE( ignoreCaseCheck != nullptr );
        REQUIRE( patternEdit != nullptr );
    }
};

} // namespace

SCENARIO( "The QuickFind bar shows what its Policy says about case", "[quickfind][settings]" )
{
    BarFixture fixture;

    GIVEN( "a Policy that ignores case" )
    {
        fixture.policy.ignoreCase = true;

        WHEN( "it arrives" )
        {
            QSignalSpy updated( &fixture.widget, &QuickFindWidget::patternUpdated );
            fixture.widget.setQuickFindPolicy( fixture.policy );

            THEN( "the bar shows it" )
            {
                REQUIRE( fixture.ignoreCaseCheck->isChecked() );
            }

            THEN( "no QuickFind was asked for: a Policy arriving is not the user typing" )
            {
                REQUIRE( updated.isEmpty() );
            }
        }
    }

    GIVEN( "a Policy that matches case" )
    {
        fixture.policy.ignoreCase = false;

        WHEN( "it arrives" )
        {
            fixture.widget.setQuickFindPolicy( fixture.policy );

            THEN( "the bar shows it" )
            {
                REQUIRE( !fixture.ignoreCaseCheck->isChecked() );
            }
        }
    }
}

SCENARIO( "The QuickFind Policy decides how the bar reads what is typed", "[quickfind][settings]" )
{
    BarFixture fixture;

    GIVEN( "a Policy reading a QuickFind pattern as an extended regexp" )
    {
        fixture.policy.quickFindRegexpType = SearchRegexpType::ExtendedRegexp;
        fixture.widget.setQuickFindPolicy( fixture.policy );

        WHEN( "the user types" )
        {
            QSignalSpy updated( &fixture.widget, &QuickFindWidget::patternUpdated );
            QTest::keyClicks( fixture.patternEdit, "abc" );

            THEN( "the bar reports the pattern is to be read as a regexp" )
            {
                REQUIRE( !updated.isEmpty() );
                REQUIRE( updated.last().at( 2 ).toBool() );
            }
        }
    }

    GIVEN( "a Policy reading a QuickFind pattern as a fixed string" )
    {
        fixture.policy.quickFindRegexpType = SearchRegexpType::FixedString;
        fixture.widget.setQuickFindPolicy( fixture.policy );

        WHEN( "the user types" )
        {
            QSignalSpy updated( &fixture.widget, &QuickFindWidget::patternUpdated );
            QTest::keyClicks( fixture.patternEdit, "abc" );

            THEN( "the bar reports the pattern stands for itself" )
            {
                REQUIRE( !updated.isEmpty() );
                REQUIRE( !updated.last().at( 2 ).toBool() );
            }
        }
    }
}
