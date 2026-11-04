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
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRect>
#include <QTest>

#include "highlighter.h"
#include "highlighteredit.h"
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

// Where a widget sits in the dialog's own coordinates.
QRect geometryIn( const QWidget& ancestor, const QWidget& widget )
{
    return { widget.mapTo( &ancestor, QPoint( 0, 0 ) ), widget.size() };
}

} // namespace

// The right pane shows the Highlighter list and the selected Highlighter's
// form side by side, under the set name (#895).
SCENARIO( "The Highlighters dialog shows the Highlighter list beside its form",
          "[ui][highlighters]" )
{
    const KeepHighlighterSets keep;
    auto mine = HighlighterSet::createNewSet( "Mine" );
    mine.addHighlighter( Highlighter( "ERROR", false, true, Qt::red, Qt::white ) );
    mine.addHighlighter( Highlighter( "WARN", false, true, Qt::black, Qt::yellow ) );
    HighlighterSetCollection::get().setHighlighterSets( { mine } );
    HighlighterSetCollection::get().save();

    GIVEN( "the Highlighters dialog at its default size with the user's set selected" )
    {
        HighlightersDialog dialog;
        showUntilExposed( dialog );
        dialog.groupList()->setCurrent( GroupList::Section::Own, 0 );
        // The editor selects the first Highlighter once the event loop runs.
        QCoreApplication::processEvents();

        auto* setEdit = dialog.findChild<HighlighterSetEdit*>();
        REQUIRE( setEdit != nullptr );
        auto* form = setEdit->findChild<HighlighterEdit*>();
        REQUIRE( form != nullptr );
        const QWidget& list = *setEdit->highlighterListWidget;

        const auto nameRect = geometryIn( dialog, *setEdit->nameEdit );
        const auto listRect = geometryIn( dialog, list );
        const auto formRect = geometryIn( dialog, *form );

        THEN( "the list is left of the form, in the same row, below the set name" )
        {
            CHECK( listRect.right() < formRect.left() );
            CHECK( listRect.top() < formRect.bottom() );
            CHECK( formRect.top() < listRect.bottom() );
            CHECK( nameRect.bottom() < listRect.top() );
            CHECK( nameRect.bottom() < formRect.top() );
        }

        THEN( "list and form are fully visible without scrolling" )
        {
            const auto shown = dialog.rect();
            CHECK( shown.contains( listRect ) );
            CHECK( shown.contains( formRect ) );
            CHECK( form->width() >= form->minimumSizeHint().width() );
            CHECK( form->height() >= form->minimumSizeHint().height() );

            const QList<const QWidget*> fields{
                form->patternEdit,        form->testPatternButton, form->patternTypeComboBox,
                form->ignoreCaseCheckBox, form->onlyMatchCheckBox, form->variateColorsCheckBox,
                form->variationSpinBox,   form->foreColorButton,   form->backColorButton,
            };
            for ( const auto* field : fields ) {
                INFO( field->objectName().toStdString() );
                CHECK( field->isVisible() );
                CHECK( formRect.contains( geometryIn( dialog, *field ) ) );
                CHECK( field->width() >= field->minimumSizeHint().width() );
                CHECK( field->height() >= field->minimumSizeHint().height() );
            }

            // Room for more than the two Highlighters of the set.
            const auto* listWidget = setEdit->highlighterListWidget;
            CHECK( listWidget->viewport()->height() >= 5 * listWidget->sizeHintForRow( 0 ) );
        }

        WHEN( "the selected Highlighter's pattern is typed in the form" )
        {
            REQUIRE( setEdit->highlighterListWidget->currentRow() == 0 );
            form->patternEdit->setFocus();
            QTest::keyClicks( form->patternEdit, "S" );

            THEN( "the list shows the new pattern" )
            {
                CHECK( setEdit->highlighterListWidget->item( 0 )->text() == "ERRORS" );
            }
        }
    }
}

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
        dialog.groupList()->setCurrent( GroupList::Section::Own, 0 );
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
