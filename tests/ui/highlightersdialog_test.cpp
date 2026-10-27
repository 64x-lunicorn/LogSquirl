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

// OK in the Highlighters dialog saves the sets while one of them is selected (#872).

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QPushButton>
#include <QTest>

#include "highlighter.h"
#include "highlightersdialog.h"
#include "highlighterset.h"
#include "highlightersetedit.h"
#include "shown_widget.h"

namespace {

// The user's Highlighter Sets, stored again as they were when this goes.
class KeepHighlighterSets {
public:
    KeepHighlighterSets()
        : sets_( HighlighterSetCollection::get().highlighterSets() )
    {
    }
    ~KeepHighlighterSets()
    {
        auto& collection = HighlighterSetCollection::get();
        collection.setHighlighterSets( sets_ );
        collection.save();
    }

    KeepHighlighterSets( const KeepHighlighterSets& ) = delete;
    KeepHighlighterSets& operator=( const KeepHighlighterSets& ) = delete;

private:
    QList<HighlighterSet> sets_;
};

} // namespace

SCENARIO( "OK in the Highlighters dialog saves the selected set of the user's own",
          "[ui][highlighters]" )
{
    const KeepHighlighterSets keep;
    auto mine = HighlighterSet::createNewSet( "Mine" );
    mine.addHighlighter( Highlighter( "ERROR", false, true, Qt::red, Qt::white ) );
    // The dialog starts from the sets as they are stored.
    HighlighterSetCollection::get().setHighlighterSets( { mine } );
    HighlighterSetCollection::get().save();

    GIVEN( "the Highlighters dialog with the user's set selected" )
    {
        HighlightersDialog dialog;
        showUntilExposed( dialog );
        dialog.highlighterListWidget->setCurrentRow( 0 );
        auto* setEdit = dialog.findChild<HighlighterSetEdit*>();
        REQUIRE( setEdit != nullptr );
        REQUIRE( setEdit->nameEdit->text() == "Mine" );

        WHEN( "OK is pressed" )
        {
            dialog.buttonBox->button( QDialogButtonBox::Ok )->click();
            QCoreApplication::processEvents();

            THEN( "the set is saved and the dialog is closed" )
            {
                CHECK_FALSE( dialog.isVisible() );
                const auto sets = HighlighterSetCollection::get().highlighterSets();
                REQUIRE( sets.size() == 1 );
                CHECK( sets.front().name() == "Mine" );
            }
        }

        WHEN( "its name is typed, a Highlighter is added, and OK is pressed while the name field "
              "has the focus" )
        {
            setEdit->nameEdit->setFocus();
            QTest::keyClicks( setEdit->nameEdit, " renamed" );
            // The editor selects the new Highlighter only once the event loop runs.
            setEdit->addHighlighterButton->click();
            REQUIRE( setEdit->nameEdit->hasFocus() );
            dialog.buttonBox->button( QDialogButtonBox::Ok )->click();
            QCoreApplication::processEvents();

            THEN( "the set is saved as typed and the dialog is closed" )
            {
                CHECK_FALSE( dialog.isVisible() );
                const auto sets = HighlighterSetCollection::get().highlighterSets();
                REQUIRE( sets.size() == 1 );
                CHECK( sets.front().name() == "Mine renamed" );
            }
        }
    }
}
