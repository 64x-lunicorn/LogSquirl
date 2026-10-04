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

#include <catch2/catch_test_macros.hpp>

#include <QLabel>
#include <QPushButton>
#include <QSignalSpy>
#include <QTreeWidget>

#include "fake_file_associations.h"
#include "fileassociationsdialog.h"

namespace {

using Answer = FirstStartFileAssociationsDialog::Answer;

Qt::CheckState checkOf( const FirstStartFileAssociationsDialog& dialog, const QString& id )
{
    const auto* row = FileTypeChoices::row( *dialog.tree(), id );
    REQUIRE( row != nullptr );
    return row->checkState( 0 );
}

} // namespace

TEST_CASE( "The first-start dialog asks which file types LogSquirl opens, as the mockup shows",
           "[fileassociations][firststart]" )
{
    FakeFileAssociations associations;
    FirstStartFileAssociationsDialog dialog( associations, { "log", "logcat" } );

    CHECK( dialog.findChildren<QLabel*>().first()->text() == "Make LogSquirl the default app" );

    auto* tree = dialog.tree();
    REQUIRE( tree->topLevelItemCount() == 2 );
    CHECK( tree->topLevelItem( 0 )->text( 0 ) == "Log files" );
    CHECK( tree->topLevelItem( 1 )->text( 0 ) == "More (optional)" );
    REQUIRE( tree->topLevelItem( 0 )->childCount() == 2 );
    CHECK( tree->topLevelItem( 0 )->child( 0 )->text( 0 ) == ".log" );
    CHECK( tree->topLevelItem( 0 )->child( 0 )->text( 1 ) == "General log files" );
    CHECK( tree->topLevelItem( 0 )->child( 1 )->text( 0 ) == ".adb, .adb0-.adb9" );
    CHECK( tree->topLevelItem( 0 )->child( 1 )->text( 1 ) == "Android Logcat traces" );
    REQUIRE( tree->topLevelItem( 1 )->childCount() == 3 );
    CHECK( tree->topLevelItem( 1 )->child( 0 )->text( 1 ) == "Program output" );
    CHECK( tree->topLevelItem( 1 )->child( 1 )->text( 1 ) == "Trace files" );
    CHECK( tree->topLevelItem( 1 )->child( 2 )->text( 1 ) == "Text files" );

    CHECK( checkOf( dialog, "log" ) == Qt::Checked );
    CHECK( checkOf( dialog, "logcat" ) == Qt::Checked );
    CHECK( checkOf( dialog, "output" ) == Qt::Unchecked );
    CHECK( checkOf( dialog, "trace" ) == Qt::Unchecked );
    CHECK( checkOf( dialog, "text" ) == Qt::Unchecked );
    CHECK( dialog.checkedIds() == QStringList{ "log", "logcat" } );

    // The page stays reachable, and the dialog says so in one line.
    CHECK( dialog.pageLabel()->text().contains( "File Associations page" ) );
    CHECK_FALSE( dialog.noteLabel()->isVisibleTo( &dialog ) );

    CHECK( dialog.laterButton()->text() == "Later" );
    CHECK( dialog.dontAskAgainButton()->text() == "Don't ask again" );
    CHECK( dialog.applyButton()->text() == "Apply" );
    CHECK( dialog.answer() == Answer::None );
}

TEST_CASE( "The first-start dialog says what applying leads to, and warns a portable Windows run",
           "[fileassociations][firststart]" )
{
    FakeFileAssociations associations;
    associations.note = "Windows asks you to confirm. If you move LogSquirl, these associations "
                        "stop working.";
    FirstStartFileAssociationsDialog dialog( associations, { "log" } );
    CHECK( dialog.noteLabel()->isVisibleTo( &dialog ) );
    CHECK( dialog.noteLabel()->text() == associations.note );
}

TEST_CASE( "The buttons of the first-start dialog", "[fileassociations][firststart]" )
{
    FakeFileAssociations associations;
    associations.current = { { "text", FileAssociationState::Default } };
    FirstStartFileAssociationsDialog dialog( associations, { "log", "logcat", "text" } );
    QSignalSpy answered( &dialog, &FirstStartFileAssociationsDialog::answered );
    FileAssociationChoice choice;

    SECTION( "Later changes nothing and asks again at the next start" )
    {
        dialog.laterButton()->click();
        CHECK( dialog.answer() == Answer::Later );
        CHECK( dialog.result() == QDialog::Rejected );
        CHECK( answered.count() == 1 );
        CHECK( associations.applied.empty() );

        dialog.updateChoice( choice );
        CHECK( choice.ask );
        CHECK_FALSE( choice.chosen );
    }

    SECTION( "closing the dialog is Later" )
    {
        dialog.reject();
        CHECK( dialog.answer() == Answer::Later );
        CHECK( answered.count() == 1 );
        dialog.updateChoice( choice );
        CHECK( choice.ask );
    }

    SECTION( "Don't ask again changes nothing and never asks again" )
    {
        dialog.dontAskAgainButton()->click();
        CHECK( dialog.answer() == Answer::DontAskAgain );
        CHECK( answered.count() == 1 );
        CHECK( associations.applied.empty() );

        dialog.updateChoice( choice );
        CHECK_FALSE( choice.ask );
        CHECK_FALSE( choice.chosen );
    }

    SECTION( "Apply applies the choice as the page does and keeps it" )
    {
        FileTypeChoices::row( *dialog.tree(), "trace" )->setCheckState( 0, Qt::Checked );
        FileTypeChoices::row( *dialog.tree(), "text" )->setCheckState( 0, Qt::Unchecked );
        dialog.applyButton()->click();
        CHECK( dialog.answer() == Answer::Applied );
        CHECK( dialog.result() == QDialog::Accepted );
        CHECK( answered.count() == 1 );

        REQUIRE( associations.applied.size() == 1 );
        CHECK( associations.applied[ 0 ].first == QStringList{ "log", "logcat", "trace" } );
        CHECK( associations.applied[ 0 ].second == QStringList{ "text" } );
        // What the File Associations page shows then.
        CHECK( associations.states().at( "log" ) == FileAssociationState::Default );
        CHECK( associations.states().at( "trace" ) == FileAssociationState::Default );
        CHECK( associations.states().at( "text" ) == FileAssociationState::Registered );

        dialog.updateChoice( choice );
        CHECK_FALSE( choice.ask );
        REQUIRE( choice.chosen );
        CHECK( *choice.chosen == QStringList{ "log", "logcat", "trace" } );
    }

    SECTION( "a type LogSquirl already opens needs nothing applied again" )
    {
        associations.current = { { "log", FileAssociationState::Default },
                                 { "text", FileAssociationState::Default } };
        dialog.applyButton()->click();
        REQUIRE( associations.applied.size() == 1 );
        CHECK( associations.applied[ 0 ].first == QStringList{ "logcat" } );
        CHECK( associations.applied[ 0 ].second.isEmpty() );
    }
}
