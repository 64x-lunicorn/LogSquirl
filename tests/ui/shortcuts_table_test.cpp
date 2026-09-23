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

#include "configuration.h"
#include "logformatcatalog.h"
#include "optionsdialog.h"
#include "recentfiles.h"
#include "savedsearches.h"
#include "shortcuts.h"

#include <QApplication>
#include <QDialogButtonBox>
#include <QKeySequenceEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTest>
#include <QTimer>
#include <QToolButton>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

// A shortcut is recorded in place by clicking its cell or pressing Enter on
// it, without a button per cell (#263).

namespace {

constexpr int PrimaryColumn = 1;
constexpr int SecondaryColumn = 2;

int rowOf( const QTableWidget* table, const std::string& action )
{
    for ( auto row = 0; row < table->rowCount(); ++row ) {
        if ( table->item( row, 0 )->data( Qt::UserRole ).toString().toStdString() == action ) {
            return row;
        }
    }
    return -1;
}

QString shortcutIn( const QTableWidget* table, int row, int column )
{
    return table->item( row, column )->data( Qt::UserRole ).toString();
}

QKeySequenceEdit* recorderOf( const QTableWidget* table, int row, int column )
{
    return qobject_cast<QKeySequenceEdit*>(
        table->indexWidget( table->model()->index( row, column ) ) );
}

// A closed recorder is hidden at once and deleted later.
bool isRecording( const QTableWidget* table )
{
    const auto recorders = table->findChildren<QKeySequenceEdit*>();
    return std::any_of( recorders.begin(), recorders.end(),
                        []( const QWidget* recorder ) { return recorder->isVisible(); } );
}

void clickCell( QTableWidget* table, int row, int column )
{
    const auto index = table->model()->index( row, column );
    table->scrollTo( index );
    QTest::mouseClick( table->viewport(), Qt::LeftButton, {}, table->visualRect( index ).center() );
    QCoreApplication::processEvents();
}

void selectCell( QTableWidget* table, int row, int column )
{
    table->setFocus();
    table->setCurrentCell( row, column );
    table->scrollTo( table->model()->index( row, column ) );
}

// Answers the next modal message box with the given button.
class MessageBoxAnswer {
public:
    explicit MessageBoxAnswer( QMessageBox::StandardButton answer )
    {
        timer_.setInterval( 20 );
        QObject::connect( &timer_, &QTimer::timeout, [ answer ] {
            if ( auto* box = qobject_cast<QMessageBox*>( QApplication::activeModalWidget() ) ) {
                box->button( answer )->click();
            }
        } );
        timer_.start();
    }

private:
    QTimer timer_;
};

} // namespace

SCENARIO( "A shortcut is recorded by clicking its cell", "[ui][options][shortcuts]" )
{
    SavedSearches::getSynced();
    RecentFiles::getSynced();
    auto& config = Configuration::get();
    const auto storedShortcuts = config.shortcuts();
    const auto storedLanguage = config.language();

    GIVEN( "the Options dialog showing default shortcuts" )
    {
        config.setShortcuts( {} );

        LogFormatCatalog catalog;
        OptionsDialog dialog( catalog );
        config.setLanguage( dialog.languageComboBox->currentData().toString() );
        MessageBoxAnswer messageBoxes( QMessageBox::Yes );

        dialog.tabWidget->setCurrentWidget( dialog.shortcutsTab );
        dialog.resize( 700, 800 );
        dialog.show();
        QTest::qWait( 20 );

        auto* table = dialog.shortcutsTable;
        const auto markRow = rowOf( table, ShortcutAction::LogViewMark );
        const auto exitRow = rowOf( table, ShortcutAction::MainWindowQuit );
        REQUIRE( markRow >= 0 );
        REQUIRE( exitRow >= 0 );
        const auto defaultMark = shortcutIn( table, markRow, PrimaryColumn );
        REQUIRE_FALSE( defaultMark.isEmpty() );

        THEN( "no shortcut cell carries a button" )
        {
            REQUIRE( table->findChildren<QPushButton*>().isEmpty() );
        }

        WHEN( "a shortcut cell is clicked and a key combination is pressed" )
        {
            clickCell( table, markRow, SecondaryColumn );
            auto* recorder = recorderOf( table, markRow, SecondaryColumn );
            REQUIRE( recorder != nullptr );

            QTest::keyClick( recorder, Qt::Key_K, Qt::ControlModifier | Qt::ShiftModifier );
            QCoreApplication::processEvents();

            THEN( "the cell shows the new shortcut and recording has ended" )
            {
                const QKeySequence expected( Qt::CTRL | Qt::SHIFT | Qt::Key_K );
                REQUIRE( shortcutIn( table, markRow, SecondaryColumn )
                         == expected.toString( QKeySequence::PortableText ) );
                REQUIRE( table->item( markRow, SecondaryColumn )->text()
                         == expected.toString( QKeySequence::NativeText ) );
                REQUIRE( recorderOf( table, markRow, SecondaryColumn ) == nullptr );
            }

            AND_WHEN( "the dialog is applied" )
            {
                dialog.buttonBox->button( QDialogButtonBox::Apply )->click();

                THEN( "the new shortcut is stored" )
                {
                    const auto keys = config.shortcuts().at( ShortcutAction::LogViewMark );
                    REQUIRE( keys.size() == 2 );
                    REQUIRE( QKeySequence( keys[ 1 ] )
                             == QKeySequence( Qt::CTRL | Qt::SHIFT | Qt::Key_K ) );
                    REQUIRE( QKeySequence( keys[ 0 ] ) == QKeySequence( defaultMark ) );
                }
            }
        }

        WHEN( "Enter is pressed on a selected shortcut cell" )
        {
            selectCell( table, markRow, PrimaryColumn );
            QTest::keyClick( table, Qt::Key_Return );
            QCoreApplication::processEvents();

            THEN( "recording starts and the dialog stays open" )
            {
                REQUIRE( recorderOf( table, markRow, PrimaryColumn ) != nullptr );
                REQUIRE( dialog.isVisible() );
            }

            AND_WHEN( "Escape is pressed while recording" )
            {
                QTest::keyClick( recorderOf( table, markRow, PrimaryColumn ), Qt::Key_Escape );
                QCoreApplication::processEvents();

                THEN( "recording is cancelled and the shortcut is unchanged" )
                {
                    REQUIRE( recorderOf( table, markRow, PrimaryColumn ) == nullptr );
                    REQUIRE( shortcutIn( table, markRow, PrimaryColumn ) == defaultMark );
                    REQUIRE( dialog.isVisible() );
                }
            }

            AND_WHEN( "Backspace is pressed while recording" )
            {
                QTest::keyClick( recorderOf( table, markRow, PrimaryColumn ), Qt::Key_Backspace );
                QCoreApplication::processEvents();

                THEN( "the shortcut is cleared" )
                {
                    REQUIRE( recorderOf( table, markRow, PrimaryColumn ) == nullptr );
                    REQUIRE( shortcutIn( table, markRow, PrimaryColumn ).isEmpty() );
                    REQUIRE( table->item( markRow, PrimaryColumn )->text().isEmpty() );
                }
            }

            AND_WHEN( "the clear icon is clicked while recording" )
            {
                const auto buttons
                    = recorderOf( table, markRow, PrimaryColumn )->findChildren<QToolButton*>();
                const auto clearIcon
                    = std::find_if( buttons.begin(), buttons.end(),
                                    []( const QWidget* button ) { return button->isVisible(); } );
                REQUIRE( clearIcon != buttons.end() );
                QTest::mouseClick( *clearIcon, Qt::LeftButton );
                QCoreApplication::processEvents();

                THEN( "the shortcut is cleared" )
                {
                    REQUIRE_FALSE( isRecording( table ) );
                    REQUIRE( shortcutIn( table, markRow, PrimaryColumn ).isEmpty() );
                }
            }

            AND_WHEN( "Tab is pressed while recording" )
            {
                QTest::keyClick( recorderOf( table, markRow, PrimaryColumn ), Qt::Key_Tab );
                QCoreApplication::processEvents();

                THEN( "recording is cancelled and no other cell starts recording" )
                {
                    REQUIRE_FALSE( isRecording( table ) );
                    REQUIRE( shortcutIn( table, markRow, PrimaryColumn ) == defaultMark );
                }
            }
        }

        WHEN( "Delete is pressed on a selected shortcut cell" )
        {
            selectCell( table, markRow, PrimaryColumn );
            QTest::keyClick( table, Qt::Key_Delete );
            QCoreApplication::processEvents();

            THEN( "the shortcut is cleared without recording" )
            {
                REQUIRE( shortcutIn( table, markRow, PrimaryColumn ).isEmpty() );
                REQUIRE( recorderOf( table, markRow, PrimaryColumn ) == nullptr );
            }
        }

        WHEN( "the arrow keys move between cells" )
        {
            selectCell( table, markRow, PrimaryColumn );
            QTest::keyClick( table, Qt::Key_Right );

            THEN( "the next shortcut cell becomes current without recording" )
            {
                REQUIRE( table->currentColumn() == SecondaryColumn );
                REQUIRE_FALSE( isRecording( table ) );
            }
        }

        WHEN( "a shortcut already used by another action is recorded" )
        {
            const auto unmarked = table->item( exitRow, SecondaryColumn )->background();

            selectCell( table, exitRow, SecondaryColumn );
            QTest::keyClick( table, Qt::Key_Enter );
            QCoreApplication::processEvents();
            auto* recorder = recorderOf( table, exitRow, SecondaryColumn );
            REQUIRE( recorder != nullptr );
            const QKeySequence mark( defaultMark );
            QTest::keyClick( recorder, static_cast<Qt::Key>( mark[ 0 ].key() ),
                             mark[ 0 ].keyboardModifiers() );
            QCoreApplication::processEvents();

            THEN( "both cells are marked as a conflict" )
            {
                REQUIRE( shortcutIn( table, exitRow, SecondaryColumn ) == defaultMark );
                REQUIRE( table->item( exitRow, SecondaryColumn )->background() != unmarked );
                REQUIRE( table->item( markRow, PrimaryColumn )->background() != unmarked );
            }

            AND_WHEN( "another shortcut is cleared afterwards" )
            {
                selectCell( table, exitRow, PrimaryColumn );
                QTest::keyClick( table, Qt::Key_Delete );

                THEN( "only the conflicting cells stay marked" )
                {
                    REQUIRE( table->item( exitRow, SecondaryColumn )->background() != unmarked );
                    REQUIRE( table->item( exitRow, PrimaryColumn )->background() == unmarked );
                }
            }

            AND_WHEN( "the defaults are restored" )
            {
                dialog.restoreShortcutsDefaults->click();
                QCoreApplication::processEvents();

                const auto markRowAfter = rowOf( table, ShortcutAction::LogViewMark );
                const auto exitRowAfter = rowOf( table, ShortcutAction::MainWindowQuit );

                THEN( "the conflict is gone" )
                {
                    REQUIRE( shortcutIn( table, exitRowAfter, SecondaryColumn ).isEmpty() );
                    REQUIRE( shortcutIn( table, markRowAfter, PrimaryColumn ) == defaultMark );
                    REQUIRE( table->item( markRowAfter, PrimaryColumn )->background() == unmarked );
                }
            }
        }
    }

    config.setShortcuts( storedShortcuts );
    config.setLanguage( storedLanguage );
    config.save();
}
