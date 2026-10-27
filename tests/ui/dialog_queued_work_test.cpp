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

// The dialogs that hand work to the event loop queue it on themselves, so the
// work is dropped once the dialog is gone (#875).

#include <catch2/catch_test_macros.hpp>

#include <QAbstractEventDispatcher>
#include <QCoreApplication>
#include <QEvent>
#include <QListWidget>
#include <QToolButton>

#include <memory>

#include "highlightersdialog.h"
#include "highlightersetedit.h"
#include "predefinedfiltersdialog.h"

namespace {

// Holds back every call queued on the event dispatcher of the main thread
// while it lives, and counts them. Work queued there outlives the object that
// queued it; held back, it cannot reach a destroyed dialog even where the
// test fails.
class DispatcherCalls : public QObject {
public:
    DispatcherCalls()
    {
        QAbstractEventDispatcher::instance( qApp->thread() )->installEventFilter( this );
    }
    ~DispatcherCalls() override
    {
        QAbstractEventDispatcher::instance( qApp->thread() )->removeEventFilter( this );
    }

    DispatcherCalls( const DispatcherCalls& ) = delete;
    DispatcherCalls& operator=( const DispatcherCalls& ) = delete;

    int count() const
    {
        return count_;
    }

protected:
    bool eventFilter( QObject* watched, QEvent* event ) override
    {
        if ( event->type() == QEvent::MetaCall ) {
            ++count_;
            return true;
        }
        return QObject::eventFilter( watched, event );
    }

private:
    int count_ = 0;
};

// Destroys the dialog, then turns the event loop: what the dialog queued and
// did not drop reaches the dispatcher.
template <typename Dialog>
int callsLeftBehind( std::unique_ptr<Dialog>& dialog )
{
    const DispatcherCalls calls;
    dialog.reset();
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    return calls.count();
}

} // namespace

SCENARIO( "The Predefined Filters dialog drops the work it queued once it is destroyed",
          "[ui][filters]" )
{
    GIVEN( "the Predefined Filters dialog, with a group of the user's own added" )
    {
        auto dialog = std::make_unique<PredefinedFiltersDialog>();
        dialog->addSetButton->click();
        dialog->addSetButton->click();
        QCoreApplication::processEvents();
        REQUIRE( dialog->setListWidget->currentRow() == dialog->setListWidget->count() - 1 );

        WHEN( "it is destroyed right after it was opened" )
        {
            dialog = std::make_unique<PredefinedFiltersDialog>();

            THEN( "no work of it is left to run" )
            {
                CHECK( callsLeftBehind( dialog ) == 0 );
            }
        }

        WHEN( "it is destroyed right after a group was added" )
        {
            dialog->addSetButton->click();

            THEN( "no work of it is left to run" )
            {
                CHECK( callsLeftBehind( dialog ) == 0 );
            }
        }

        WHEN( "it is destroyed right after a group was moved up" )
        {
            REQUIRE( dialog->upSetButton->isEnabled() );
            dialog->upSetButton->click();

            THEN( "no work of it is left to run" )
            {
                CHECK( callsLeftBehind( dialog ) == 0 );
            }
        }

        WHEN( "it is destroyed right after a group was moved down" )
        {
            dialog->setListWidget->setCurrentRow( dialog->setListWidget->count() - 2 );
            REQUIRE( dialog->downSetButton->isEnabled() );
            dialog->downSetButton->click();

            THEN( "no work of it is left to run" )
            {
                CHECK( callsLeftBehind( dialog ) == 0 );
            }
        }

        WHEN( "it is destroyed right after a group was removed" )
        {
            REQUIRE( dialog->removeSetButton->isEnabled() );
            dialog->removeSetButton->click();

            THEN( "no work of it is left to run" )
            {
                CHECK( callsLeftBehind( dialog ) == 0 );
            }
        }
    }
}

SCENARIO( "The Highlighters dialog and its set editor drop the work they queued once the dialog "
          "is destroyed",
          "[ui][highlighters]" )
{
    GIVEN( "the Highlighters dialog, with a set of the user's own added" )
    {
        auto dialog = std::make_unique<HighlightersDialog>();
        dialog->addHighlighterButton->click();
        dialog->addHighlighterButton->click();
        QCoreApplication::processEvents();
        REQUIRE( dialog->highlighterListWidget->currentRow()
                 == dialog->highlighterListWidget->count() - 1 );

        WHEN( "it is destroyed right after a set was added" )
        {
            dialog->addHighlighterButton->click();

            THEN( "no work of it is left to run" )
            {
                CHECK( callsLeftBehind( dialog ) == 0 );
            }
        }

        WHEN( "it is destroyed right after a set was moved up" )
        {
            REQUIRE( dialog->upHighlighterButton->isEnabled() );
            dialog->upHighlighterButton->click();

            THEN( "no work of it is left to run" )
            {
                CHECK( callsLeftBehind( dialog ) == 0 );
            }
        }

        WHEN( "it is destroyed right after a set was removed" )
        {
            REQUIRE( dialog->removeHighlighterButton->isEnabled() );
            dialog->removeHighlighterButton->click();

            THEN( "no work of it is left to run" )
            {
                CHECK( callsLeftBehind( dialog ) == 0 );
            }
        }

        WHEN( "it is destroyed right after a highlighter was added to the set and removed" )
        {
            auto* setEdit = dialog->findChild<HighlighterSetEdit*>();
            REQUIRE( setEdit != nullptr );
            setEdit->addHighlighterButton->click();
            QCoreApplication::processEvents();
            REQUIRE( setEdit->removeHighlighterButton->isEnabled() );
            setEdit->removeHighlighterButton->click();

            THEN( "no work of it is left to run" )
            {
                CHECK( callsLeftBehind( dialog ) == 0 );
            }
        }
    }
}
