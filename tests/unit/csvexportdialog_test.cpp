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

// The dialog Export as CSV (#572).

#include <catch2/catch_test_macros.hpp>

#include <QCheckBox>
#include <QFile>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QStringList>
#include <QTemporaryDir>

#include "configuration.h"
#include "csvexportdialog.h"

namespace {

// Keeps the Configuration as it was before the test.
class KeptConfiguration {
public:
    KeptConfiguration()
        : saved_( Configuration::get() )
    {
    }
    ~KeptConfiguration()
    {
        Configuration::get() = saved_;
        Configuration::get().save();
    }
    KeptConfiguration( const KeptConfiguration& ) = delete;
    KeptConfiguration& operator=( const KeptConfiguration& ) = delete;

private:
    Configuration saved_;
};

CsvExportDialog::Setup tableSetup( bool hasSelection )
{
    CsvExportDialog::Setup setup;
    setup.hasSelection = hasSelection;
    setup.columns = { { "Line", false }, { "timestamp", true }, { "body", true } };
    setup.proposedFileName = "/logs/app.log.csv";
    return setup;
}

QRadioButton* radioButton( const QDialog& dialog, const QString& text )
{
    for ( auto* button : dialog.findChildren<QRadioButton*>() ) {
        if ( button->text() == text ) {
            return button;
        }
    }
    FAIL( "no radio button " << text.toStdString() );
    return nullptr;
}

QCheckBox* checkBox( const QDialog& dialog, const QString& text )
{
    for ( auto* box : dialog.findChildren<QCheckBox*>() ) {
        if ( box->text() == text ) {
            return box;
        }
    }
    FAIL( "no check box " << text.toStdString() );
    return nullptr;
}

QPushButton* exportButton( const QDialog& dialog )
{
    for ( auto* button : dialog.findChildren<QPushButton*>() ) {
        if ( button->text() == "Export..." ) {
            return button;
        }
    }
    FAIL( "no Export button" );
    return nullptr;
}

} // namespace

SCENARIO( "The dialog Export as CSV offers the selected rows only when there are some",
          "[csvexport][dialog]" )
{
    const KeptConfiguration kept;

    GIVEN( "Rows selected" )
    {
        const CsvExportDialog dialog( tableSetup( true ) );

        THEN( "the selected rows are chosen" )
        {
            REQUIRE( radioButton( dialog, "Selected rows" )->isEnabled() );
            REQUIRE( dialog.choices().selectedRowsOnly );
        }
    }

    GIVEN( "no Row selected" )
    {
        const CsvExportDialog dialog( tableSetup( false ) );

        THEN( "all rows are chosen, and the selected rows cannot be" )
        {
            REQUIRE( !radioButton( dialog, "Selected rows" )->isEnabled() );
            REQUIRE( radioButton( dialog, "All rows" )->isChecked() );
            REQUIRE( !dialog.choices().selectedRowsOnly );
        }
    }
}

SCENARIO( "The dialog Export as CSV exports only with a column checked", "[csvexport][dialog]" )
{
    const KeptConfiguration kept;
    CsvExportDialog dialog( tableSetup( false ) );
    auto* columns = dialog.findChild<QListWidget*>();
    REQUIRE( columns != nullptr );

    THEN( "the columns are checked as the setup says" )
    {
        REQUIRE( dialog.choices().columns == std::vector<size_t>{ 1, 2 } );
        REQUIRE( exportButton( dialog )->isEnabled() );
    }

    WHEN( "every column is unchecked" )
    {
        for ( int row = 0; row < columns->count(); ++row ) {
            columns->item( row )->setCheckState( Qt::Unchecked );
        }

        THEN( "Export is disabled" )
        {
            REQUIRE( !exportButton( dialog )->isEnabled() );
        }

        AND_WHEN( "one is checked again" )
        {
            columns->item( 0 )->setCheckState( Qt::Checked );

            THEN( "Export is enabled, for that column" )
            {
                REQUIRE( exportButton( dialog )->isEnabled() );
                REQUIRE( dialog.choices().columns == std::vector<size_t>{ 0 } );
            }
        }
    }
}

SCENARIO( "The dialog Export as CSV remembers the separator and the header row",
          "[csvexport][dialog]" )
{
    const KeptConfiguration kept;
    Configuration::get().setCsvSeparator( ',' );
    Configuration::get().setCsvHeader( true );

    GIVEN( "an export with semicolons and without a header row" )
    {
        CsvExportDialog dialog( tableSetup( false ) );
        REQUIRE( radioButton( dialog, "Comma" )->isChecked() );
        REQUIRE( checkBox( dialog, "Write column names as the first row" )->isChecked() );

        radioButton( dialog, "Semicolon" )->setChecked( true );
        checkBox( dialog, "Write column names as the first row" )->setChecked( false );
        QString proposed;
        dialog.setFileNameChooser( [ &proposed ]( QWidget*, const QString& proposedName ) {
            proposed = proposedName;
            return QStringLiteral( "/exports/errors" );
        } );

        WHEN( "the user exports" )
        {
            exportButton( dialog )->click();

            THEN( "the Log File's name is proposed, and .csv is added to the file chosen" )
            {
                REQUIRE( dialog.result() == QDialog::Accepted );
                REQUIRE( proposed == "/logs/app.log.csv" );
                REQUIRE( dialog.choices().fileName == "/exports/errors.csv" );
                REQUIRE( dialog.choices().separator == QChar( ';' ) );
                REQUIRE( !dialog.choices().header );
            }

            AND_WHEN( "the dialog opens again" )
            {
                const CsvExportDialog next( tableSetup( false ) );

                THEN( "it starts with that separator and without the header row" )
                {
                    REQUIRE( radioButton( next, "Semicolon" )->isChecked() );
                    REQUIRE(
                        !checkBox( next, "Write column names as the first row" )->isChecked() );
                }
            }
        }
    }

    GIVEN( "a dialog whose file choice is cancelled" )
    {
        CsvExportDialog dialog( tableSetup( false ) );
        radioButton( dialog, "Tab" )->setChecked( true );
        dialog.setFileNameChooser( []( QWidget*, const QString& ) { return QString{}; } );

        WHEN( "the user exports" )
        {
            exportButton( dialog )->click();

            THEN( "the dialog stays open, and nothing is remembered" )
            {
                REQUIRE( dialog.result() != QDialog::Accepted );
                REQUIRE( Configuration::get().csvSeparator() == QChar( ',' ) );
            }
        }
    }
}

SCENARIO( "The dialog Export as CSV asks before replacing the file .csv is added to",
          "[csvexport][dialog]" )
{
    const KeptConfiguration kept;
    Configuration::get().setCsvSeparator( ',' );
    const QTemporaryDir folder;
    REQUIRE( folder.isValid() );
    const auto chosenName = folder.filePath( "errors" );
    const auto finalName = folder.filePath( "errors.csv" );

    CsvExportDialog dialog( tableSetup( false ) );
    radioButton( dialog, "Semicolon" )->setChecked( true );
    dialog.setFileNameChooser( [ &chosenName ]( QWidget*, const QString& ) { return chosenName; } );
    QStringList asked;
    bool replace = false;
    dialog.setOverwriteConfirmer( [ &asked, &replace ]( QWidget*, const QString& fileName ) {
        asked.push_back( fileName );
        return replace;
    } );

    GIVEN( "no file with .csv added exists" )
    {
        WHEN( "the user exports" )
        {
            exportButton( dialog )->click();

            THEN( "nothing is asked, and that file is written" )
            {
                REQUIRE( asked.isEmpty() );
                REQUIRE( dialog.result() == QDialog::Accepted );
                REQUIRE( dialog.choices().fileName == finalName );
            }
        }
    }

    GIVEN( "the file with .csv added exists" )
    {
        QFile existing( finalName );
        REQUIRE( existing.open( QIODevice::WriteOnly ) );
        existing.close();

        WHEN( "the user exports and declines to replace it" )
        {
            exportButton( dialog )->click();

            THEN( "the user is asked about that file, the dialog stays open, and nothing is "
                  "remembered" )
            {
                REQUIRE( asked == QStringList{ finalName } );
                REQUIRE( dialog.result() != QDialog::Accepted );
                REQUIRE( Configuration::get().csvSeparator() == QChar( ',' ) );
            }
        }

        WHEN( "the user exports and replaces it" )
        {
            replace = true;
            exportButton( dialog )->click();

            THEN( "that file is written" )
            {
                REQUIRE( asked == QStringList{ finalName } );
                REQUIRE( dialog.result() == QDialog::Accepted );
                REQUIRE( dialog.choices().fileName == finalName );
            }
        }
    }

    GIVEN( "a file chosen with .csv, which exists" )
    {
        QFile existing( finalName );
        REQUIRE( existing.open( QIODevice::WriteOnly ) );
        existing.close();
        dialog.setFileNameChooser(
            [ &finalName ]( QWidget*, const QString& ) { return finalName; } );

        WHEN( "the user exports" )
        {
            exportButton( dialog )->click();

            THEN( "nothing is asked again: the file dialog asked about it" )
            {
                REQUIRE( asked.isEmpty() );
                REQUIRE( dialog.result() == QDialog::Accepted );
            }
        }
    }
}

SCENARIO( "The dialog Export as CSV offers options of rows of another kind", "[csvexport][dialog]" )
{
    const KeptConfiguration kept;
    auto setup = tableSetup( true );
    setup.allRowsText = "All shown lines";
    setup.selectedRowsText = "Selected lines";
    setup.rowOptions = { { "Include Context Lines", false, false } };

    const CsvExportDialog dialog( setup );

    THEN( "it shows them, as the setup says" )
    {
        REQUIRE( radioButton( dialog, "Selected lines" )->isChecked() );
        REQUIRE( !radioButton( dialog, "All shown lines" )->isChecked() );
        REQUIRE( !checkBox( dialog, "Include Context Lines" )->isEnabled() );
        REQUIRE( dialog.choices().rowOptions == std::vector<bool>{ false } );
    }
}
