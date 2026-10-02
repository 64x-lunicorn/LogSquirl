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

// The sidebar's Value Names tab and the Value Names dialog (#647): the tab
// behaves like the Filters tab and keeps its checks across a restart, the
// dialog edits the groups, and what either changes reaches the views.

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMetaObject>
#include <QPushButton>
#include <QSignalSpy>
#include <QSpinBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>

#include <memory>

#include "abstractlogview.h"
#include "applicationplugins.h"
#include "crawlerwidget.h"
#include "groupexchange.h"
#include "logformatcatalog.h"
#include "mainwindow.h"
#include "session.h"
#include "test_policies.h"
#include "test_utils.h"
#include "valuenames_fixture.h"
#include "valuenamescollection.h"
#include "valuenamesdialog.h"
#include "valuenamespanel.h"

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

using valuenamesfixture::exampleGroup;
using valuenamesfixture::GroupTable;
using valuenamesfixture::NameRow;
using valuenamesfixture::NameTable;
using valuenamesfixture::NamingGroup;
using valuenamesfixture::NamingRule;

namespace {

// Restores the collection, and what is stored of it, when it goes.
class StoredValueNamesGuard {
public:
    StoredValueNamesGuard()
        : before_( ValueNamesCollection::get().ownGroups() )
        , teamBefore_( ValueNamesCollection::get().groups().mid(
              ValueNamesCollection::get().ownGroups().size() ) )
    {
    }

    ~StoredValueNamesGuard()
    {
        auto& collection = ValueNamesCollection::get();
        collection.setTeamGroups( teamBefore_ );
        collection.setGroups( before_ );
        collection.save();
    }

    StoredValueNamesGuard( const StoredValueNamesGuard& ) = delete;
    StoredValueNamesGuard& operator=( const StoredValueNamesGuard& ) = delete;

private:
    QList<NamingGroup> before_;
    QList<NamingGroup> teamBefore_;
};

// Sets and stores the groups, as the dialog does.
void storeGroups( const QList<NamingGroup>& groups )
{
    auto& collection = ValueNamesCollection::get();
    collection.setTeamGroups( {} );
    collection.setGroups( groups );
    collection.save();
}

NamingRule rule( const QString& name, const QString& pattern )
{
    NamingRule namingRule;
    namingRule.name = name;
    namingRule.pattern = pattern;
    return namingRule;
}

// Group A with the rules One, Two and Three, group B with the rule Four.
QList<NamingGroup> twoGroups()
{
    auto first = NamingGroup::createNewGroup( QStringLiteral( "A" ) );
    first.setRules( { rule( QStringLiteral( "One" ), QStringLiteral( "one=(\\d)" ) ),
                      rule( QStringLiteral( "Two" ), QStringLiteral( "two=(\\d)" ) ),
                      rule( QStringLiteral( "Three" ), QStringLiteral( "three=(\\d)" ) ) } );
    auto second = NamingGroup::createNewGroup( QStringLiteral( "B" ) );
    second.setRules( { rule( QStringLiteral( "Four" ), QStringLiteral( "four=(\\d)" ) ) } );
    return { first, second };
}

QTreeWidget* treeOf( ValueNamesPanel& panel )
{
    auto* tree = panel.findChild<QTreeWidget*>();
    REQUIRE( tree != nullptr );
    return tree;
}

QPushButton* buttonOf( QWidget& widget, const QString& text )
{
    for ( auto* button : widget.findChildren<QPushButton*>() ) {
        if ( button->text() == text ) {
            return button;
        }
    }
    FAIL( "no button " << text.toStdString() );
    return nullptr;
}

QToolButton* toolButtonOf( QWidget& widget, const QString& toolTipOrText )
{
    for ( auto* button : widget.findChildren<QToolButton*>() ) {
        if ( button->toolTip() == toolTipOrText || button->text() == toolTipOrText ) {
            return button;
        }
    }
    FAIL( "no tool button " << toolTipOrText.toStdString() );
    return nullptr;
}

// Whether each rule of the collection's groups is checked, group by group.
QList<QList<bool>> ruleChecks()
{
    QList<QList<bool>> checks;
    for ( const auto& group : ValueNamesCollection::get().groups() ) {
        QList<bool> rules;
        for ( const auto& namingRule : group.rules() ) {
            rules.append( group.isEnabled() && namingRule.enabled );
        }
        checks.append( rules );
    }
    return checks;
}

void doubleClick( ValueNamesPanel& panel, QTreeWidgetItem* item )
{
    REQUIRE( QMetaObject::invokeMethod( &panel, "onItemDoubleClicked",
                                        Q_ARG( QTreeWidgetItem*, item ), Q_ARG( int, 0 ) ) );
}

} // namespace

SCENARIO( "The Value Names tab checks rules and groups like the Filters tab",
          "[ui][valuenames][valuenamespanel]" )
{
    const StoredValueNamesGuard guard;
    storeGroups( twoGroups() );

    GIVEN( "the tab showing two groups, all checked" )
    {
        ValueNamesPanel panel;
        QSignalSpy changed( &panel, &ValueNamesPanel::valueNamesChanged );
        auto* tree = treeOf( panel );
        REQUIRE( tree->topLevelItemCount() == 2 );
        auto* first = tree->topLevelItem( 0 );
        REQUIRE( first->text( 0 ) == QStringLiteral( "A" ) );
        REQUIRE( first->childCount() == 3 );
        REQUIRE( first->checkState( 0 ) == Qt::Checked );

        WHEN( "a rule is unchecked" )
        {
            first->child( 1 )->setCheckState( 0, Qt::Unchecked );
            QCoreApplication::processEvents();

            THEN( "its group is partly checked and the collection names without it" )
            {
                REQUIRE( first->checkState( 0 ) == Qt::PartiallyChecked );
                REQUIRE( ruleChecks() == QList<QList<bool>>{ { true, false, true }, { true } } );
                REQUIRE( changed.count() == 1 );
            }
        }

        WHEN( "a group is unchecked" )
        {
            first->setCheckState( 0, Qt::Unchecked );
            QCoreApplication::processEvents();

            THEN( "all its rules are, in one change" )
            {
                REQUIRE( ruleChecks() == QList<QList<bool>>{ { false, false, false }, { true } } );
                REQUIRE( changed.count() == 1 );
            }
        }

        WHEN( "a rule is double-clicked" )
        {
            doubleClick( panel, first->child( 2 ) );

            THEN( "only it is checked" )
            {
                REQUIRE( ruleChecks() == QList<QList<bool>>{ { false, false, true }, { false } } );
            }
        }

        WHEN( "a group is double-clicked" )
        {
            doubleClick( panel, tree->topLevelItem( 1 ) );

            THEN( "only its rules are checked" )
            {
                REQUIRE( ruleChecks() == QList<QList<bool>>{ { false, false, false }, { true } } );
            }
        }

        WHEN( "Deselect All and then Select All are clicked" )
        {
            buttonOf( panel, QStringLiteral( "Deselect All" ) )->click();
            const auto deselected = ruleChecks();
            buttonOf( panel, QStringLiteral( "Select All" ) )->click();

            THEN( "first none, then every rule is checked" )
            {
                REQUIRE( deselected == QList<QList<bool>>{ { false, false, false }, { false } } );
                REQUIRE( ruleChecks() == QList<QList<bool>>{ { true, true, true }, { true } } );
                REQUIRE( changed.count() == 2 );
            }
        }

        WHEN( "the search shows the rule Two only, and Deselect All is clicked" )
        {
            panel.findChild<QLineEdit*>()->setText( QStringLiteral( "two" ) );
            buttonOf( panel, QStringLiteral( "Deselect All" ) )->click();

            THEN( "only Two is shown and unchecked; the others keep their checks" )
            {
                REQUIRE( first->child( 0 )->isHidden() );
                REQUIRE_FALSE( first->child( 1 )->isHidden() );
                REQUIRE( tree->topLevelItem( 1 )->isHidden() );
                REQUIRE( ruleChecks() == QList<QList<bool>>{ { true, false, true }, { true } } );
            }
        }

        WHEN( "the search matches a group's name" )
        {
            panel.findChild<QLineEdit*>()->setText( QStringLiteral( "b" ) );

            THEN( "the group shows all its rules, and the other group is hidden" )
            {
                REQUIRE( first->isHidden() );
                REQUIRE_FALSE( tree->topLevelItem( 1 )->isHidden() );
                REQUIRE_FALSE( tree->topLevelItem( 1 )->child( 0 )->isHidden() );
            }
        }

        WHEN( "the collection gets a Team group" )
        {
            auto team = NamingGroup::createNewGroup( QStringLiteral( "Shared" ) );
            team.setRules( { rule( QStringLiteral( "Five" ), QStringLiteral( "five" ) ) } );
            ValueNamesCollection::get().setTeamGroups( { team } );
            panel.refresh();

            THEN( "it is listed last, marked as a Team group" )
            {
                REQUIRE( tree->topLevelItemCount() == 3 );
                REQUIRE( tree->topLevelItem( 2 )->text( 0 ) == QStringLiteral( "Shared (Team)" ) );
            }
        }
    }
}

SCENARIO( "The checks of the Value Names tab outlive a restart",
          "[ui][valuenames][valuenamespanel]" )
{
    const StoredValueNamesGuard guard;
    storeGroups( twoGroups() );

    GIVEN( "a rule unchecked and a rule soloed in the tab" )
    {
        {
            ValueNamesPanel panel;
            auto* tree = treeOf( panel );
            tree->topLevelItem( 0 )->child( 0 )->setCheckState( 0, Qt::Unchecked );
            QCoreApplication::processEvents();
        }
        const auto checked = ruleChecks();
        REQUIRE( checked == QList<QList<bool>>{ { false, true, true }, { true } } );

        WHEN( "the settings are read again, as at the next start" )
        {
            // Forgets the checks held, so that only what is stored counts.
            ValueNamesCollection::get().setUncheckedKeys( {} );
            REQUIRE( ruleChecks() != checked );
            ValueNamesCollection::getSynced();
            ValueNamesPanel panel;

            THEN( "the same rules are checked, in the collection and in the tab" )
            {
                REQUIRE( ruleChecks() == checked );
                REQUIRE( treeOf( panel )->topLevelItem( 0 )->child( 0 )->checkState( 0 )
                         == Qt::Unchecked );
                REQUIRE( treeOf( panel )->topLevelItem( 0 )->checkState( 0 )
                         == Qt::PartiallyChecked );
            }
        }
    }
}

SCENARIO( "The Value Names dialog edits the Naming Groups", "[ui][valuenames][valuenamesdialog]" )
{
    const StoredValueNamesGuard guard;
    storeGroups( { exampleGroup() } );

    GIVEN( "the dialog showing the example group" )
    {
        ValueNamesDialog dialog;
        QSignalSpy changed( &dialog, &ValueNamesDialog::valueNamesChanged );
        auto* rules = dialog.findChild<QTableWidget*>( QStringLiteral( "rulesTable" ) );
        auto* rows = dialog.findChild<QTableWidget*>( QStringLiteral( "rowsTable" ) );
        auto* tables = dialog.findChild<QListWidget*>( QStringLiteral( "tablesList" ) );
        auto* captureGroups = dialog.findChild<QTableWidget*>( QStringLiteral( "captureGroups" ) );
        auto* preview = dialog.findChild<QLineEdit*>( QStringLiteral( "previewInput" ) );
        auto* warnings = dialog.findChild<QListWidget*>( QStringLiteral( "warnings" ) );
        REQUIRE( rules != nullptr );
        REQUIRE( rows != nullptr );
        REQUIRE( tables != nullptr );
        REQUIRE( captureGroups != nullptr );
        REQUIRE( preview != nullptr );
        REQUIRE( warnings != nullptr );

        REQUIRE( dialog.findChild<QListWidget*>( QStringLiteral( "groupList" ) )->count() == 1 );
        REQUIRE( rules->rowCount() == 2 );
        REQUIRE( tables->count() == 3 );
        // The rule BAP ECU is selected: two capture groups, tables ECU and Function.
        REQUIRE( captureGroups->rowCount() == 2 );
        REQUIRE( warnings->count() == 0 );

        const auto warningTexts = [ warnings ] {
            QStringList texts;
            for ( int row = 0; row < warnings->count(); ++row ) {
                texts.append( warnings->item( row )->text() );
            }
            return texts.join( QLatin1Char( '\n' ) );
        };

        WHEN( "a sample Log Line is pasted into the preview" )
        {
            preview->setText( QStringLiteral( "BAP << ECU 0x15 0x14 id=7" ) );

            THEN( "it is shown with the group's Value Names" )
            {
                REQUIRE(
                    dialog.shownPreview()
                    == QStringLiteral( "BAP << ECU Beispiel(0x15) Sample(0x14) id=seven(7)" ) );
            }
        }

        WHEN( "a rule is added, named like another, and given a regex and a Name Table" )
        {
            toolButtonOf( dialog, QStringLiteral( "Add a Naming Rule" ) )->click();
            REQUIRE( rules->rowCount() == 3 );
            rules->item( 2, 0 )->setText( QStringLiteral( "Id" ) );
            rules->item( 2, 1 )->setText( QStringLiteral( "v=(?<ecu>0x\\d+)" ) );
            rules->item( 2, 2 )->setText( QStringLiteral( "{name}" ) );
            REQUIRE( captureGroups->rowCount() == 1 );
            REQUIRE( captureGroups->item( 0, 0 )->text() == QStringLiteral( "1 (ecu)" ) );
            auto* combo = qobject_cast<QComboBox*>( captureGroups->cellWidget( 0, 1 ) );
            REQUIRE( combo != nullptr );
            combo->setCurrentIndex( combo->findText( QStringLiteral( "ECU" ) ) );
            preview->setText( QStringLiteral( "v=0x15" ) );

            THEN( "the rule gets a name of its own, uses the table by the group's name, and names" )
            {
                const auto& added = dialog.groups()[ 0 ].rules()[ 2 ];
                REQUIRE( added.name == QStringLiteral( "Id (2)" ) );
                REQUIRE( rules->item( 2, 0 )->text() == QStringLiteral( "Id (2)" ) );
                REQUIRE(
                    added.groupTables
                    == QList<GroupTable>{ { QStringLiteral( "ecu" ), QStringLiteral( "ECU" ) } } );
                REQUIRE( dialog.shownPreview() == QStringLiteral( "v=Beispiel" ) );
            }
        }

        WHEN( "a capture group is given no Name Table" )
        {
            auto* combo = qobject_cast<QComboBox*>( captureGroups->cellWidget( 1, 1 ) );
            REQUIRE( combo != nullptr );
            combo->setCurrentIndex( 0 );
            preview->setText( QStringLiteral( "BAP << ECU 0x15 0x14" ) );

            THEN( "its value stays as it is" )
            {
                REQUIRE(
                    dialog.groups()[ 0 ].rules()[ 0 ].tableFor( QStringLiteral( "2" ) ).isEmpty() );
                REQUIRE( dialog.shownPreview()
                         == QStringLiteral( "BAP << ECU Beispiel(0x15) 0x14" ) );
            }
        }

        WHEN( "a row is added to the table Ids, a table renamed and keys made case-sensitive" )
        {
            tables->setCurrentRow( 2 );
            toolButtonOf( dialog, QStringLiteral( "Add a row" ) )->click();
            REQUIRE( rows->rowCount() == 2 );
            rows->item( 1, 0 )->setText( QStringLiteral( "8" ) );
            rows->item( 1, 1 )->setText( QStringLiteral( "eight" ) );
            tables->item( 0 )->setText( QStringLiteral( "Ecus" ) );
            tables->setCurrentRow( 0 );
            dialog.findChild<QCheckBox*>( QStringLiteral( "caseSensitive" ) )->setChecked( true );
            preview->setText( QStringLiteral( "id=8 BAP << ECU 0x15 0x14" ) );

            THEN( "the group has the row, the rules use the table by its new name" )
            {
                const auto& group = dialog.groups()[ 0 ];
                REQUIRE( group.tables()[ 2 ].rows.last()
                         == NameRow{ QStringLiteral( "8" ), QStringLiteral( "eight" ) } );
                REQUIRE( group.tables()[ 0 ].name == QStringLiteral( "Ecus" ) );
                REQUIRE( group.tables()[ 0 ].caseSensitive );
                REQUIRE( group.rules()[ 0 ].tableFor( QStringLiteral( "1" ) )
                         == QStringLiteral( "Ecus" ) );
                REQUIRE(
                    dialog.shownPreview()
                    == QStringLiteral( "id=eight(8) BAP << ECU Beispiel(0x15) Sample(0x14)" ) );
                REQUIRE( warnings->count() == 0 );
            }
        }

        WHEN(
            "a key is not a valid regex, another is there twice, and a name uses a missing group" )
        {
            tables->setCurrentRow( 2 );
            toolButtonOf( dialog, QStringLiteral( "Add a row" ) )->click();
            toolButtonOf( dialog, QStringLiteral( "Add a row" ) )->click();
            toolButtonOf( dialog, QStringLiteral( "Add a row" ) )->click();
            rows->item( 1, 0 )->setText( QStringLiteral( "(" ) );
            rows->item( 1, 1 )->setText( QStringLiteral( "broken" ) );
            rows->item( 2, 0 )->setText( QStringLiteral( "7" ) );
            rows->item( 2, 1 )->setText( QStringLiteral( "again" ) );
            rows->item( 3, 0 )->setText( QStringLiteral( "9" ) );
            rows->item( 3, 1 )->setText( QStringLiteral( "nine{1}" ) );
            rules->item( 1, 1 )->setText( QStringLiteral( "id=(\\d+" ) );

            THEN( "the warnings tell of each, with its row" )
            {
                const auto texts = warningTexts();
                REQUIRE(
                    texts.contains( QStringLiteral( "Rule \"Id\": the regex is not valid" ) ) );
                REQUIRE( texts.contains(
                    QStringLiteral( "Table \"Ids\", row 2: the key is not a valid regex" ) ) );
                REQUIRE( texts.contains(
                    QStringLiteral( "Table \"Ids\", row 3: the key \"7\" is already in row 1" ) ) );
                REQUIRE( texts.contains( QStringLiteral(
                    "Table \"Ids\", row 4: the name uses {1}, but the key has no such group" ) ) );
            }
        }

        WHEN( "CSV with a header and a key twice replaces the rows of the table Ids" )
        {
            tables->setCurrentRow( 2 );
            logsquirl::valuenames::CsvImportOptions options;
            options.hasHeader = true;
            dialog.importCsvText( QStringLiteral( "key;name\n1;one\n1;uno\n2;two\n" ), options,
                                  CsvImportTarget::ReplaceRows );

            THEN( "it replaces the rows, the first key wins, and the warning names the line" )
            {
                REQUIRE( dialog.groups()[ 0 ].tables()[ 2 ].rows
                         == QList<NameRow>{ { QStringLiteral( "1" ), QStringLiteral( "one" ) },
                                            { QStringLiteral( "2" ), QStringLiteral( "two" ) } } );
                REQUIRE( rows->rowCount() == 2 );
                REQUIRE( warningTexts().contains( QStringLiteral( "CSV line 3: the key \"1\"" ) ) );
            }

            AND_WHEN( "rows copied from a spreadsheet are pasted" )
            {
                dialog.pasteRows( QStringLiteral( "3\tthree\n" ) );

                THEN( "they are added, and Export CSV writes all of them" )
                {
                    REQUIRE( rows->rowCount() == 3 );
                    REQUIRE( dialog.exportCsvText()
                             == QStringLiteral( "1,one\n2,two\n3,three\n" ) );
                }
            }
        }

        WHEN( "CSV is imported while no table is selected" )
        {
            toolButtonOf( dialog, QStringLiteral( "Add a Naming Group" ) )->click();
            dialog.importCsvText( QStringLiteral( "a,b\n" ), {}, CsvImportTarget::ReplaceRows,
                                  QStringLiteral( "Codes" ) );

            THEN( "it becomes a new table, named after the file" )
            {
                REQUIRE( dialog.groups().size() == 2 );
                REQUIRE( dialog.groups()[ 1 ].tables().size() == 1 );
                REQUIRE( dialog.groups()[ 1 ].tables()[ 0 ].name == QStringLiteral( "Codes" ) );
            }
        }

        WHEN( "CSV is imported into a new table and appended to the table Ids" )
        {
            tables->setCurrentRow( 2 );
            dialog.importCsvText( QStringLiteral( "8,eight\n" ), {}, CsvImportTarget::NewTable,
                                  QStringLiteral( "Ids" ) );
            const auto newTable = dialog.groups()[ 0 ].tables().last();
            tables->setCurrentRow( 2 );
            dialog.importCsvText( QStringLiteral( "9,nine\n" ), {}, CsvImportTarget::AppendRows );

            THEN( "the new table has a name of its own, and Ids keeps its row before the new one" )
            {
                REQUIRE( dialog.groups()[ 0 ].tables().size() == 4 );
                REQUIRE( newTable.name == QStringLiteral( "Ids (2)" ) );
                REQUIRE(
                    newTable.rows
                    == QList<NameRow>{ { QStringLiteral( "8" ), QStringLiteral( "eight" ) } } );
                REQUIRE( dialog.groups()[ 0 ].tables()[ 2 ].rows
                         == QList<NameRow>{ { QStringLiteral( "7" ), QStringLiteral( "seven" ) },
                                            { QStringLiteral( "9" ), QStringLiteral( "nine" ) } } );
            }
        }

        WHEN( "a key holding %2 is there twice" )
        {
            tables->setCurrentRow( 2 );
            dialog.pasteRows( QStringLiteral( "a%2Fb\tone\na%2Fb\ttwo\n" ) );
            const auto pasted = warningTexts();
            dialog.pasteRows( QStringLiteral( "a%2Fb\tthree\n" ) );

            THEN( "the warnings show the key as it is, with the right rows and CSV line" )
            {
                REQUIRE( pasted.contains( QStringLiteral(
                    "CSV line 2: the key \"a%2Fb\" was already read on line 1" ) ) );
                REQUIRE( warningTexts().contains( QStringLiteral(
                    "Table \"Ids\", row 3: the key \"a%2Fb\" is already in row 2" ) ) );
            }
        }

        WHEN( "the regex of the rule BAP ECU loses its second capture group" )
        {
            rules->item( 0, 1 )->setText( QStringLiteral( "BAP << ECU (0x[0-9A-F]{2})" ) );

            THEN( "the table of the group gone goes, that of group 1 stays" )
            {
                REQUIRE(
                    dialog.groups()[ 0 ].rules()[ 0 ].groupTables
                    == QList<GroupTable>{ { QStringLiteral( "1" ), QStringLiteral( "ECU" ) } } );
                REQUIRE( captureGroups->rowCount() == 1 );
                REQUIRE( warnings->count() == 0 );
            }
        }

        WHEN( "a rule looking up its whole match gets a capture group, then it is renamed" )
        {
            rules->selectRow( 1 );
            rules->item( 1, 1 )->setText( QStringLiteral( "id=\\d+" ) );
            auto* combo = qobject_cast<QComboBox*>( captureGroups->cellWidget( 0, 1 ) );
            REQUIRE( combo != nullptr );
            REQUIRE( captureGroups->item( 0, 0 )->text() == QStringLiteral( "whole match" ) );
            combo->setCurrentIndex( combo->findText( QStringLiteral( "Ids" ) ) );
            REQUIRE( dialog.groups()[ 0 ].rules()[ 1 ].groupTables
                     == QList<GroupTable>{ { QStringLiteral( "0" ), QStringLiteral( "Ids" ) } } );

            rules->item( 1, 1 )->setText( QStringLiteral( "id=(?<id>\\d+)" ) );
            const auto withGroup = dialog.groups()[ 0 ].rules()[ 1 ].groupTables;
            rules->item( 1, 1 )->setText( QStringLiteral( "id=(?<number>\\d+)" ) );

            THEN( "the whole match's table goes to the group, and follows it under its new name" )
            {
                REQUIRE(
                    withGroup
                    == QList<GroupTable>{ { QStringLiteral( "id" ), QStringLiteral( "Ids" ) } } );
                REQUIRE( dialog.groups()[ 0 ].rules()[ 1 ].groupTables
                         == QList<GroupTable>{
                             { QStringLiteral( "number" ), QStringLiteral( "Ids" ) } } );
                REQUIRE( warnings->count() == 0 );
            }
        }

        WHEN( "the table Function is removed" )
        {
            tables->setCurrentRow( 1 );
            toolButtonOf( dialog, QStringLiteral( "Remove the Name Table" ) )->click();

            THEN( "the rule that used it uses no table for that capture group" )
            {
                REQUIRE(
                    dialog.groups()[ 0 ].rules()[ 0 ].tableFor( QStringLiteral( "2" ) ).isEmpty() );
                REQUIRE( warnings->count() == 0 );
            }
        }

        WHEN( "a second group is named like the first, and another emptied" )
        {
            auto* name = dialog.findChild<QLineEdit*>( QStringLiteral( "groupName" ) );
            toolButtonOf( dialog, QStringLiteral( "Add a Naming Group" ) )->click();
            name->setText( QStringLiteral( "BAP" ) );
            Q_EMIT name->textEdited( name->text() );
            Q_EMIT name->editingFinished();
            toolButtonOf( dialog, QStringLiteral( "Add a Naming Group" ) )->click();
            name->setText( QString{} );
            Q_EMIT name->textEdited( name->text() );
            Q_EMIT name->editingFinished();

            THEN( "each gets a name of its own" )
            {
                REQUIRE( dialog.groups()[ 1 ].name() == QStringLiteral( "BAP (2)" ) );
                REQUIRE( dialog.groups()[ 2 ].name() == QStringLiteral( "New Naming Group" ) );
                REQUIRE( name->text() == QStringLiteral( "New Naming Group" ) );
            }
        }

        WHEN( "the selected rule and a group added are moved, past the ends too" )
        {
            // Index 0: the group's buttons, 1: the rules', beside the rules table.
            QList<QToolButton*> up{ nullptr, nullptr };
            QList<QToolButton*> down{ nullptr, nullptr };
            for ( auto* button : dialog.findChildren<QToolButton*>() ) {
                const auto index = rules->parentWidget()->isAncestorOf( button ) ? 1 : 0;
                if ( button->toolTip() == QStringLiteral( "Move up" ) ) {
                    up[ index ] = button;
                }
                else if ( button->toolTip() == QStringLiteral( "Move down" ) ) {
                    down[ index ] = button;
                }
            }
            REQUIRE( !up.contains( nullptr ) );
            REQUIRE( !down.contains( nullptr ) );
            const auto firstRule = dialog.groups()[ 0 ].rules()[ 0 ].name;
            const auto secondRule = dialog.groups()[ 0 ].rules()[ 1 ].name;

            THEN( "a rule moves down to the end and up to the start, no further" )
            {
                down[ 1 ]->click();
                REQUIRE( dialog.groups()[ 0 ].rules()[ 1 ].name == firstRule );
                down[ 1 ]->click();
                REQUIRE( dialog.groups()[ 0 ].rules()[ 1 ].name == firstRule );
                REQUIRE( rules->currentRow() == 1 );
                up[ 1 ]->click();
                up[ 1 ]->click();
                REQUIRE( dialog.groups()[ 0 ].rules()[ 0 ].name == firstRule );
                REQUIRE( dialog.groups()[ 0 ].rules()[ 1 ].name == secondRule );
                REQUIRE( rules->currentRow() == 0 );
            }

            THEN( "a group added moves up to the start, no further, and down again" )
            {
                toolButtonOf( dialog, QStringLiteral( "Add a Naming Group" ) )->click();
                auto* groupList = dialog.findChild<QListWidget*>( QStringLiteral( "groupList" ) );
                up[ 0 ]->click();
                up[ 0 ]->click();
                REQUIRE( dialog.groups()[ 0 ].name() == QStringLiteral( "New Naming Group" ) );
                REQUIRE( groupList->currentRow() == 0 );
                down[ 0 ]->click();
                down[ 0 ]->click();
                REQUIRE( dialog.groups()[ 1 ].name() == QStringLiteral( "New Naming Group" ) );
                REQUIRE( groupList->currentRow() == 1 );
            }
        }

        WHEN( "Enter is pressed in the preview" )
        {
            dialog.show();
            preview->setFocus();
            QTest::keyClick( preview, Qt::Key_Return );

            THEN( "the dialog stays open and nothing is applied" )
            {
                REQUIRE( dialog.isVisible() );
                REQUIRE( dialog.result() == 0 );
                REQUIRE( changed.count() == 0 );
            }
        }

        WHEN( "Apply is clicked after a change" )
        {
            const auto generation = ValueNamesCollection::get().generation();
            auto* name = dialog.findChild<QLineEdit*>( QStringLiteral( "groupName" ) );
            name->setText( QStringLiteral( "Renamed" ) );
            Q_EMIT name->textEdited( name->text() );
            dialog.findChild<QDialogButtonBox*>()->button( QDialogButtonBox::Apply )->click();

            THEN( "the collection holds and stores the groups, and says so" )
            {
                REQUIRE( changed.count() == 1 );
                REQUIRE( ValueNamesCollection::get().generation() > generation );
                REQUIRE( ValueNamesCollection::getSynced().ownGroups()[ 0 ].name()
                         == QStringLiteral( "Renamed" ) );
            }
        }

        WHEN( "Cancel is clicked after a change" )
        {
            toolButtonOf( dialog, QStringLiteral( "Remove the Naming Group" ) )->click();
            dialog.findChild<QDialogButtonBox*>()->button( QDialogButtonBox::Cancel )->click();

            THEN( "the collection is unchanged" )
            {
                REQUIRE( changed.count() == 0 );
                REQUIRE( ValueNamesCollection::get().ownGroups().size() == 1 );
            }
        }
    }
}

SCENARIO( "The Value Names dialog exports and imports a Naming Group",
          "[ui][valuenames][valuenamesdialog][groupexchange]" )
{
    using namespace logsquirl::groupexchange;
    const StoredValueNamesGuard guard;
    storeGroups( { exampleGroup() } );
    const QTemporaryDir dir;
    REQUIRE( dir.isValid() );

    ValueNamesDialog dialog;
    auto* exportButton = dialog.findChild<QPushButton*>( QStringLiteral( "exportGroup" ) );
    REQUIRE( exportButton != nullptr );
    REQUIRE( dialog.findChild<QPushButton*>( QStringLiteral( "importGroups" ) ) != nullptr );
    REQUIRE( exportButton->isEnabled() );

    // Exported, then changed here, then imported again.
    const auto file = dir.filePath( suggestedFileName( "BAP", GroupKind::ValueNames ) );
    REQUIRE( dialog.exportShownGroup( file ) );
    const auto exported = readGroups<NamingGroup>( file );
    REQUIRE( exported.groups.size() == 1 );
    REQUIRE(
        exported.groups.front().sameAs( exampleGroup().withId( exported.groups.front().id() ) ) );

    auto* name = dialog.findChild<QLineEdit*>( QStringLiteral( "groupName" ) );
    QTest::keyClicks( name, QStringLiteral( " mine" ) );
    REQUIRE( dialog.groups().front().name() == QStringLiteral( "BAP mine" ) );

    const auto answer
        = GENERATE( ConflictAnswer::Replace, ConflictAnswer::KeepBoth, ConflictAnswer::Skip );
    int asked = 0;
    dialog.importGroupFiles( { file }, [ &asked, answer ]( const ConflictQuestion& question ) {
        ++asked;
        REQUIRE( question.kind == ConflictKind::SameId );
        return ConflictDecision{ answer, false };
    } );
    REQUIRE( asked == 1 );

    QStringList names;
    for ( const auto& group : dialog.groups() ) {
        names.append( group.name() );
    }
    switch ( answer ) {
    case ConflictAnswer::Replace:
        REQUIRE( names == QStringList{ "BAP" } );
        break;
    case ConflictAnswer::KeepBoth:
        REQUIRE( names == QStringList{ "BAP mine", "BAP" } );
        REQUIRE( dialog.groups()[ 1 ].id() != dialog.groups()[ 0 ].id() );
        break;
    case ConflictAnswer::Skip:
        REQUIRE( names == QStringList{ "BAP mine" } );
        break;
    }

    // OK keeps what was imported.
    dialog.findChild<QDialogButtonBox*>()->button( QDialogButtonBox::Ok )->click();
    REQUIRE( ValueNamesCollection::get().ownGroups().size() == names.size() );
    QCoreApplication::processEvents();
}

SCENARIO( "A Team Naming Group exported and imported again is a group of the user's own",
          "[ui][valuenames][valuenamesdialog][teamfolder]" )
{
    using namespace logsquirl::groupexchange;
    const StoredValueNamesGuard guard;
    storeGroups( { exampleGroup() } );
    auto team = exampleGroup();
    team.setName( QStringLiteral( "Shared" ) );
    auto& collection = ValueNamesCollection::get();
    collection.setTeamGroups( { team } );
    const QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const auto file = dir.filePath( QStringLiteral( "Shared_valuenames.conf" ) );

    ValueNamesDialog dialog;
    dialog.showTeamGroups( { team }, false );
    dialog.findChild<QListWidget*>( QStringLiteral( "teamGroupList" ) )->setCurrentRow( 0 );
    REQUIRE( dialog.exportShownGroup( file ) );
    int asked = 0;
    dialog.importGroupFiles( { file }, [ &asked ]( const ConflictQuestion& ) {
        ++asked;
        return ConflictDecision{ ConflictAnswer::Skip, false };
    } );
    dialog.findChild<QDialogButtonBox*>()->button( QDialogButtonBox::Ok )->click();

    THEN( "it has an id of its own, and its checks are its own" )
    {
        REQUIRE( asked == 0 );
        REQUIRE( collection.ownGroups().size() == 2 );
        const auto imported = collection.ownGroups()[ 1 ];
        REQUIRE( imported.name() == QStringLiteral( "Shared" ) );
        REQUIRE( imported.id() != team.id() );

        collection.setUncheckedKeys( { ValueNamesCollection::groupCheckKey( team.id() ) } );
        REQUIRE_FALSE( collection.groups().back().isEnabled() );
        REQUIRE( collection.groups()[ 1 ].id() == imported.id() );
        REQUIRE( collection.groups()[ 1 ].isEnabled() );
    }
    collection.setUncheckedKeys( {} );
    QCoreApplication::processEvents();
}

SCENARIO( "The Value Names dialog shows the Team groups below the user's own",
          "[ui][valuenames][valuenamesdialog][teamfolder]" )
{
    using logsquirl::teamfolder::GroupAction;
    using logsquirl::teamfolder::PublishRequest;
    const StoredValueNamesGuard guard;
    storeGroups( { exampleGroup() } );
    auto team = exampleGroup();
    team = team.withId( NamingGroup::createNewGroup( QStringLiteral( "x" ) ).id() );
    team.setName( QStringLiteral( "Shared" ) );

    ValueNamesDialog dialog;
    QList<PublishRequest> requests;
    QObject::connect( &dialog, &ValueNamesDialog::publishRequested, &dialog,
                      [ &requests ]( const auto& published ) { requests = published; } );
    auto* own = dialog.findChild<QListWidget*>( QStringLiteral( "groupList" ) );
    auto* name = dialog.findChild<QLineEdit*>( QStringLiteral( "groupName" ) );
    auto* rules = dialog.findChild<QTableWidget*>( QStringLiteral( "rulesTable" ) );
    auto* caseSensitive = dialog.findChild<QCheckBox*>( QStringLiteral( "caseSensitive" ) );
    auto* preview = dialog.findChild<QLineEdit*>( QStringLiteral( "previewInput" ) );
    auto* apply = dialog.findChild<QDialogButtonBox*>()->button( QDialogButtonBox::Apply );
    REQUIRE( dialog.findChild<QListWidget*>( QStringLiteral( "teamGroupList" ) ) == nullptr );

    WHEN( "the Team groups are read-only" )
    {
        dialog.showTeamGroups( { team }, false );
        auto* teamList = dialog.findChild<QListWidget*>( QStringLiteral( "teamGroupList" ) );
        REQUIRE( teamList != nullptr );
        REQUIRE( teamList->count() == 1 );
        REQUIRE( dialog.findChild<QPushButton*>( QStringLiteral( "teamAdd" ) )->isHidden() );
        REQUIRE( dialog.findChild<QPushButton*>( QStringLiteral( "teamShare" ) )->isHidden() );
        REQUIRE( dialog.findChild<QPushButton*>( QStringLiteral( "teamDelete" ) )->isHidden() );

        teamList->setCurrentRow( 0 );

        THEN( "the Team group is shown and tried, but not changed" )
        {
            REQUIRE( own->currentRow() == -1 );
            REQUIRE( name->text() == QStringLiteral( "Shared" ) );
            REQUIRE( name->isReadOnly() );
            REQUIRE( rules->editTriggers() == QAbstractItemView::NoEditTriggers );
            REQUIRE_FALSE(
                toolButtonOf( dialog, QStringLiteral( "Add a Naming Rule" ) )->isEnabled() );
            REQUIRE_FALSE(
                toolButtonOf( dialog, QStringLiteral( "Add a Name Table" ) )->isEnabled() );
            REQUIRE_FALSE( caseSensitive->isEnabled() );
            auto* captureGroups
                = dialog.findChild<QTableWidget*>( QStringLiteral( "captureGroups" ) );
            REQUIRE( captureGroups->rowCount() == 2 );
            for ( int row = 0; row < captureGroups->rowCount(); ++row ) {
                REQUIRE_FALSE( captureGroups->cellWidget( row, 1 )->isEnabled() );
            }
            REQUIRE(
                dialog.findChild<QPushButton*>( QStringLiteral( "exportGroup" ) )->isEnabled() );

            preview->setText( QStringLiteral( "BAP << ECU 0x15 0x14" ) );
            REQUIRE( dialog.shownPreview()
                     == QStringLiteral( "BAP << ECU Beispiel(0x15) Sample(0x14)" ) );

            apply->click();
            REQUIRE( requests.isEmpty() );
        }

        AND_WHEN( "it is copied to the user's own groups" )
        {
            dialog.findChild<QPushButton*>( QStringLiteral( "teamCopy" ) )->click();

            THEN( "the copy is one of the user's own, and can be changed" )
            {
                REQUIRE( dialog.groups().size() == 2 );
                REQUIRE( dialog.groups()[ 1 ].name() == QStringLiteral( "Shared" ) );
                REQUIRE( dialog.groups()[ 1 ].id() != team.id() );
                REQUIRE( own->currentRow() == 1 );
                REQUIRE( teamList->currentRow() == -1 );
                REQUIRE_FALSE( name->isReadOnly() );
                REQUIRE(
                    toolButtonOf( dialog, QStringLiteral( "Add a Naming Rule" ) )->isEnabled() );
            }
        }
    }

    WHEN( "the Team groups can be changed" )
    {
        dialog.showTeamGroups( { team }, true, { { team.id(), QStringLiteral( "R1" ) } } );
        auto* teamList = dialog.findChild<QListWidget*>( QStringLiteral( "teamGroupList" ) );
        REQUIRE( teamList != nullptr );

        AND_WHEN( "one is renamed and Apply is clicked" )
        {
            teamList->setCurrentRow( 0 );
            REQUIRE_FALSE( name->isReadOnly() );
            QTest::keyClicks( name, QStringLiteral( " now" ) );
            apply->click();

            THEN( "the rename is published on the revision loaded, and the own groups stay" )
            {
                REQUIRE( requests.size() == 1 );
                REQUIRE( requests[ 0 ].kind == logsquirl::groupexchange::GroupKind::ValueNames );
                REQUIRE( requests[ 0 ].action == GroupAction::Rename );
                REQUIRE( requests[ 0 ].name == QStringLiteral( "Shared now" ) );
                REQUIRE( requests[ 0 ].baseRevision == QString( "R1" ) );
                REQUIRE( ValueNamesCollection::get().ownGroups().size() == 1 );
                REQUIRE( ValueNamesCollection::get().ownGroups()[ 0 ].name()
                         == QStringLiteral( "BAP" ) );
            }
        }

        AND_WHEN( "a group of the user's own is shared and a new Team group added" )
        {
            own->setCurrentRow( 0 );
            dialog.findChild<QPushButton*>( QStringLiteral( "teamShare" ) )->click();
            dialog.findChild<QPushButton*>( QStringLiteral( "teamAdd" ) )->click();
            apply->click();

            THEN( "both are published as new Team groups" )
            {
                REQUIRE( teamList->count() == 3 );
                REQUIRE( requests.size() == 2 );
                REQUIRE( requests[ 0 ].action == GroupAction::Add );
                const auto* shared
                    = logsquirl::teamfolder::groupOfKind<NamingGroup>( requests[ 0 ].group );
                REQUIRE( shared != nullptr );
                REQUIRE( shared->id() != dialog.groups()[ 0 ].id() );
                REQUIRE( shared->rules() == dialog.groups()[ 0 ].rules() );
                REQUIRE( requests[ 1 ].action == GroupAction::Add );
            }
        }
    }
    QCoreApplication::processEvents();
}

SCENARIO( "The CSV import of a Name Table shows the separator and asks for the columns",
          "[ui][valuenames][valuenamesdialog]" )
{
    GIVEN( "CSV separated by semicolons, with no table selected" )
    {
        NameTableCsvImportDialog dialog( QStringLiteral( "a;b;c\n1;2;3\n" ), true );
        auto* target = dialog.findChild<QComboBox*>();
        REQUIRE( target != nullptr );

        THEN( "the separator shown is the semicolon, the columns are 1 and 2, into a new table" )
        {
            REQUIRE( dialog.separatorShown() == QStringLiteral( "semicolon" ) );
            const auto options = dialog.options();
            REQUIRE( options.keyColumn == 0 );
            REQUIRE( options.nameColumn == 1 );
            REQUIRE_FALSE( options.hasHeader );
            REQUIRE( options.caseSensitive );
            REQUIRE( target->count() == 1 );
            REQUIRE( dialog.target() == CsvImportTarget::NewTable );
        }

        WHEN( "the key and the name are given the same column" )
        {
            auto spins = dialog.findChildren<QSpinBox*>();
            REQUIRE( spins.size() == 2 );
            spins[ 1 ]->setValue( spins[ 0 ]->value() );

            THEN( "OK cannot be clicked" )
            {
                REQUIRE_FALSE( dialog.findChild<QDialogButtonBox*>()
                                   ->button( QDialogButtonBox::Ok )
                                   ->isEnabled() );
            }
        }
    }

    GIVEN( "a table selected" )
    {
        NameTableCsvImportDialog dialog( QStringLiteral( "a,b\n" ), false,
                                         QStringLiteral( "ECU" ) );
        auto* target = dialog.findChild<QComboBox*>();

        THEN( "the rows can also replace those of the table or be appended to them" )
        {
            REQUIRE( target->count() == 3 );
            REQUIRE( dialog.target() == CsvImportTarget::NewTable );
            target->setCurrentIndex( 1 );
            REQUIRE( dialog.target() == CsvImportTarget::ReplaceRows );
            target->setCurrentIndex( 2 );
            REQUIRE( dialog.target() == CsvImportTarget::AppendRows );
        }
    }
}

SCENARIO( "A CSV file is read in the encoding a spreadsheet saves it in",
          "[ui][valuenames][valuenamesdialog]" )
{
    const auto expected = QStringLiteral( "Tür,Öl\n" );

    WHEN( "it is UTF-16LE with a byte order mark" )
    {
        QByteArray bytes( "\xFF\xFE", 2 );
        bytes
            += QByteArray( reinterpret_cast<const char*>( expected.utf16() ), expected.size() * 2 );
        REQUIRE( decodeCsvFile( bytes ) == expected );
    }

    WHEN( "it is UTF-8 with or without a byte order mark" )
    {
        REQUIRE( decodeCsvFile( expected.toUtf8() ) == expected );
        REQUIRE( decodeCsvFile( QByteArray( "\xEF\xBB\xBF" ) + expected.toUtf8() ) == expected );
    }

    WHEN( "it is windows-1252, which is not valid UTF-8" )
    {
#ifdef Q_OS_WIN
        // Windows reads it in the system's ANSI code page, which is
        // windows-1252 only on a western system.
        if ( ::GetACP() != 1252 ) {
            SKIP( "The ANSI code page of this system is " << ::GetACP() << ", not 1252" );
        }
#endif
        // T \xFC r , \xD6 l, and the euro sign 0x80 only windows-1252 has.
        REQUIRE( decodeCsvFile( QByteArray( "T\xFCr,\xD6l \x80\n" ) )
                 == QStringLiteral( "Tür,Öl €\n" ) );
    }
}

namespace {

// A window over the Session showing a Log File with Named Values, in a tab
// that shows Value Names.
struct ValueNamesWindow {
    ValueNamesWindow( const std::shared_ptr<Session>& session, const QString& path,
                      const QString& name )
        : window( std::make_unique<MainWindow>(
              WindowSession{ session, name, 0 },
              std::make_shared<logsquirl::plugins::ApplicationPlugins>() ) )
    {
        window->show();
        window->loadFileNonInteractive( path );
        CrawlerWidget* crawler = nullptr;
        REQUIRE( waitUiState( [ & ] {
            crawler = window->findChild<CrawlerWidget*>();
            return crawler != nullptr;
        } ) );
        view = crawler->findChild<AbstractLogView*>();
        REQUIRE( view != nullptr );
        REQUIRE( waitUiState( [ this ] { return view->showsValueNames(); } ) );
        panel = window->findChild<ValueNamesPanel*>();
        REQUIRE( panel != nullptr );
        tree = treeOf( *panel );
    }

    ~ValueNamesWindow()
    {
        window.reset();
        QTest::qWait( 50 );
    }

    ValueNamesWindow( const ValueNamesWindow& ) = delete;
    ValueNamesWindow& operator=( const ValueNamesWindow& ) = delete;

    QAction* action( const QString& text ) const
    {
        for ( auto* candidate : window->findChildren<QAction*>() ) {
            if ( candidate->text() == text ) {
                return candidate;
            }
        }
        FAIL( "no action " << text.toStdString() );
        return nullptr;
    }

    std::unique_ptr<MainWindow> window;
    AbstractLogView* view = nullptr;
    ValueNamesPanel* panel = nullptr;
    QTreeWidget* tree = nullptr;
};

QString writeLogFile( const QTemporaryDir& dir, const QString& name )
{
    const auto path = dir.filePath( name );
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly ) );
    file.write( "BAP << ECU 0x15 0x14 sonstiges\nid=7\n" );
    return path;
}

std::shared_ptr<Session> valueNamesSession()
{
    auto policies = testSettingsPolicies();
    policies.presentation.showValueNames = true;
    return std::make_shared<Session>( policies, std::make_shared<LogFormatCatalog>() );
}

// Removes the selected group in the Value Names dialog about to open, and
// clicks OK.
void removeGroupInDialog()
{
    QTimer::singleShot( 0, [] {
        auto* dialog = qobject_cast<ValueNamesDialog*>( QApplication::activeModalWidget() );
        REQUIRE( dialog != nullptr );
        toolButtonOf( *dialog, QStringLiteral( "Remove the Naming Group" ) )->click();
        dialog->findChild<QDialogButtonBox*>()->button( QDialogButtonBox::Ok )->click();
    } );
}

} // namespace

SCENARIO( "What the Value Names dialog and tab change reaches the views of a window",
          "[ui][valuenames][valuenamesdialog][valuenamespanel]" )
{
    const StoredValueNamesGuard guard;
    storeGroups( { exampleGroup() } );

    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const auto session = valueNamesSession();
    ValueNamesWindow shown( session, writeLogFile( dir, QStringLiteral( "value-names.log" ) ),
                            QStringLiteral( "Main" ) );
    auto* panel = shown.panel;
    auto* tree = shown.tree;
    auto* view = shown.view;

    GIVEN( "the window's Value Names tab, third after Filters and Scratchpad" )
    {
        auto* tabs = qobject_cast<QTabWidget*>( panel->parentWidget()->parentWidget() );
        REQUIRE( tabs != nullptr );
        REQUIRE( tabs->indexOf( panel ) == 2 );
        REQUIRE( tabs->tabText( 2 ) == QStringLiteral( "Value Names" ) );

        WHEN( "Tools -> Value Names tab is chosen" )
        {
            tabs->setCurrentIndex( 0 );
            shown.action( QStringLiteral( "Value Names tab" ) )->trigger();

            THEN( "the sidebar shows the tab" )
            {
                REQUIRE( tabs->currentIndex() == 2 );
                REQUIRE( panel->isVisible() );
            }
        }

        WHEN( "its group is unchecked" )
        {
            tree->topLevelItem( 0 )->setCheckState( 0, Qt::Unchecked );

            THEN( "the view no longer names values" )
            {
                REQUIRE( waitUiState( [ view ] { return !view->showsValueNames(); } ) );
            }
        }

        WHEN( "the group is removed in the dialog opened by Edit..." )
        {
            removeGroupInDialog();
            buttonOf( *panel, QStringLiteral( "Edit..." ) )->click();

            THEN( "the view no longer names values, and the tab lists no group" )
            {
                REQUIRE( ValueNamesCollection::get().groups().isEmpty() );
                REQUIRE( !view->showsValueNames() );
                REQUIRE( tree->topLevelItemCount() == 0 );
            }
        }

        WHEN( "the dialog, opened by Tools -> Value Names..., reads other groups from the "
              "settings, and is cancelled" )
        {
            // What another instance saved: the settings hold another group
            // than this one does.
            auto stored = exampleGroup();
            stored.setName( QStringLiteral( "Stored" ) );
            storeGroups( { stored } );
            ValueNamesCollection::get().setGroups( { exampleGroup() } );
            session->applyChange( Changed::ValueNames );
            REQUIRE( tree->topLevelItem( 0 )->text( 0 ) == QStringLiteral( "BAP" ) );

            QTimer::singleShot( 0, [] {
                auto* dialog = qobject_cast<ValueNamesDialog*>( QApplication::activeModalWidget() );
                REQUIRE( dialog != nullptr );
                dialog->findChild<QDialogButtonBox*>()->button( QDialogButtonBox::Cancel )->click();
            } );
            shown.action( QStringLiteral( "Value Names..." ) )->trigger();

            THEN( "the tab shows the groups read" )
            {
                REQUIRE( tree->topLevelItem( 0 )->text( 0 ) == QStringLiteral( "Stored" ) );
            }
        }
    }
}

SCENARIO( "What the Value Names dialog of one window changes reaches the other window",
          "[ui][valuenames][valuenamesdialog][valuenamespanel]" )
{
    const StoredValueNamesGuard guard;
    storeGroups( { exampleGroup() } );

    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const auto session = valueNamesSession();
    ValueNamesWindow first( session, writeLogFile( dir, QStringLiteral( "first.log" ) ),
                            QStringLiteral( "First" ) );
    ValueNamesWindow second( session, writeLogFile( dir, QStringLiteral( "second.log" ) ),
                             QStringLiteral( "Second" ) );
    REQUIRE( second.tree->topLevelItemCount() == 1 );

    WHEN( "the group is removed in the first window's dialog" )
    {
        removeGroupInDialog();
        buttonOf( *first.panel, QStringLiteral( "Edit..." ) )->click();

        THEN( "the second window's tab lists no group, and its view names nothing" )
        {
            REQUIRE( second.tree->topLevelItemCount() == 0 );
            REQUIRE_FALSE( second.view->showsValueNames() );
        }
    }
}
