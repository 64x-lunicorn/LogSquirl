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

#include "namevaluedialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

#include "valuenamer.h"
#include "valuenamescollection.h"

using logsquirl::valuenames::LineRange;
using logsquirl::valuenames::NameRow;
using logsquirl::valuenames::NameTable;
using logsquirl::valuenames::NamingGroup;
using logsquirl::valuenames::NamingProposal;
using logsquirl::valuenames::RuleWithTable;

namespace {

enum RowColumn { RowKey = 0, RowName = 1 };

// What is proposed for the range: the value and the text around it, or, if
// it holds no value after all, the text itself as the regex.
NamingProposal proposalFor( const QString& logLine, LineRange selection )
{
    if ( auto proposal = logsquirl::valuenames::proposeNaming( logLine, selection ) ) {
        return *proposal;
    }
    NamingProposal proposal;
    proposal.value = logLine.mid( selection.start, selection.length );
    proposal.valueRange = selection;
    proposal.pattern = QStringLiteral( "(" )
                       + logsquirl::valuenames::escapedForRegex( proposal.value )
                       + QStringLiteral( ")" );
    proposal.key = logsquirl::valuenames::escapedForRegex( proposal.value );
    proposal.displayTemplate = logsquirl::valuenames::proposedTemplate();
    return proposal;
}

QStringList groupNames( const QList<NamingGroup>& groups )
{
    QStringList names;
    for ( const auto& group : groups ) {
        names.append( group.name() );
    }
    return names;
}

} // namespace

NameValueDialog::NameValueDialog( const QString& logLine, LineRange selection, QWidget* parent )
    : QDialog( parent )
    , logLine_( logLine )
    , proposal_( proposalFor( logLine, selection ) )
    , groups_( ValueNamesCollection::getSynced().ownGroups() )
{
    setWindowTitle( tr( "Name This Value" ) );

    auto* form = new QFormLayout;

    // The group: one of the user's own, or a new one named here.
    group_ = new QComboBox( this );
    group_->setObjectName( QStringLiteral( "namingGroup" ) );
    for ( const auto& group : groups_ ) {
        group_->addItem( group.name(), group.id() );
    }
    group_->addItem( tr( "New Naming Group" ), QString{} );
    newGroupName_ = new QLineEdit(
        logsquirl::valuenames::freeName( tr( "Value Names" ), groupNames( groups_ ) ), this );
    newGroupName_->setObjectName( QStringLiteral( "newGroupName" ) );
    auto* groupLine = new QHBoxLayout;
    groupLine->addWidget( group_, 1 );
    groupLine->addWidget( newGroupName_, 1 );
    form->addRow( tr( "Naming Group:" ), groupLine );

    // The table: a new one, named after the text before the value, or one
    // of the group's.
    table_ = new QComboBox( this );
    table_->setObjectName( QStringLiteral( "nameTable" ) );
    newTableName_ = new QLineEdit(
        proposal_.tableName.isEmpty() ? tr( "Values" ) : proposal_.tableName, this );
    newTableName_->setObjectName( QStringLiteral( "newTableName" ) );
    auto* tableRow = new QHBoxLayout;
    tableRow->addWidget( table_, 1 );
    tableRow->addWidget( newTableName_, 1 );
    form->addRow( tr( "Name Table:" ), tableRow );

    pattern_ = new QLineEdit( proposal_.pattern, this );
    pattern_->setObjectName( QStringLiteral( "rulePattern" ) );
    pattern_->setToolTip( tr( "The regex of the Naming Rule; its first capture group is the "
                              "value looked up in the Name Table" ) );
    form->addRow( tr( "Naming Rule:" ), pattern_ );

    template_ = new QLineEdit( proposal_.displayTemplate, this );
    template_->setObjectName( QStringLiteral( "ruleTemplate" ) );
    template_->setToolTip( tr( "How a named value is shown: {name} is its name, {value} the "
                               "value itself" ) );
    form->addRow( tr( "Template:" ), template_ );

    rows_ = new QTableWidget( 0, 2, this );
    rows_->setObjectName( QStringLiteral( "nameRows" ) );
    rows_->setHorizontalHeaderLabels( { tr( "Key" ), tr( "Name" ) } );
    rows_->horizontalHeader()->setStretchLastSection( true );
    rows_->verticalHeader()->setVisible( false );
    rows_->setEditTriggers( QAbstractItemView::DoubleClicked | QAbstractItemView::EditKeyPressed
                            | QAbstractItemView::AnyKeyPressed
                            | QAbstractItemView::SelectedClicked );
    addRowButton_ = new QPushButton( tr( "Add Row" ), this );
    removeRowButton_ = new QPushButton( tr( "Remove Row" ), this );
    addRowButton_->setObjectName( QStringLiteral( "addRow" ) );
    removeRowButton_->setObjectName( QStringLiteral( "removeRow" ) );
    auto* rowButtons = new QHBoxLayout;
    rowButtons->addWidget( addRowButton_ );
    rowButtons->addWidget( removeRowButton_ );
    rowButtons->addStretch( 1 );
    auto* rowsColumn = new QVBoxLayout;
    rowsColumn->addWidget( rows_ );
    rowsColumn->addLayout( rowButtons );
    form->addRow( tr( "Rows:" ), rowsColumn );

    preview_ = new QLabel( this );
    preview_->setObjectName( QStringLiteral( "preview" ) );
    preview_->setTextFormat( Qt::RichText );
    preview_->setWordWrap( true );
    preview_->setTextInteractionFlags( Qt::TextSelectableByMouse );
    warning_ = new QLabel( this );
    warning_->setObjectName( QStringLiteral( "previewWarning" ) );
    warning_->setWordWrap( true );
    auto* previewColumn = new QVBoxLayout;
    previewColumn->addWidget( preview_ );
    previewColumn->addWidget( warning_ );
    form->addRow( tr( "Preview:" ), previewColumn );

    auto* buttons = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this );
    auto* layout = new QVBoxLayout( this );
    layout->addLayout( form );
    layout->addWidget( buttons );

    // The group that has a table of the name proposed, else the first one.
    int groupRow = groups_.isEmpty() ? 0 : -1;
    for ( int row = 0; row < groups_.size() && groupRow < 0; ++row ) {
        if ( groups_[ row ].table( proposal_.tableName ) != nullptr ) {
            groupRow = row;
        }
    }
    group_->setCurrentIndex( groupRow < 0 ? 0 : groupRow );
    groupChosen();

    connect( group_, &QComboBox::currentIndexChanged, this, &NameValueDialog::groupChosen );
    connect( table_, &QComboBox::currentIndexChanged, this, &NameValueDialog::tableChosen );
    connect( newTableName_, &QLineEdit::textChanged, this, &NameValueDialog::updatePreview );
    connect( pattern_, &QLineEdit::textChanged, this, &NameValueDialog::updatePreview );
    connect( template_, &QLineEdit::textChanged, this, &NameValueDialog::updatePreview );
    connect( rows_, &QTableWidget::itemChanged, this, &NameValueDialog::updatePreview );
    connect( addRowButton_, &QPushButton::clicked, this, &NameValueDialog::addRow );
    connect( removeRowButton_, &QPushButton::clicked, this, &NameValueDialog::removeRow );
    connect( buttons, &QDialogButtonBox::accepted, this, &NameValueDialog::accept );
    connect( buttons, &QDialogButtonBox::rejected, this, &NameValueDialog::reject );

    resize( 640, sizeHint().height() );
}

int NameValueDialog::chosenGroup() const
{
    const auto row = group_->currentIndex();
    return row >= 0 && row < groups_.size() ? row : -1;
}

void NameValueDialog::groupChosen()
{
    newGroupName_->setEnabled( chosenGroup() < 0 );
    populateTables();
}

void NameValueDialog::populateTables()
{
    const QSignalBlocker blocker( table_ );
    table_->clear();
    table_->addItem( tr( "New Name Table" ), QString{} );
    int chosen = 0;
    if ( const auto row = chosenGroup(); row >= 0 ) {
        for ( const auto& table : groups_[ row ].tables() ) {
            table_->addItem( table.name, table.name );
            if ( chosen == 0 && table.name == proposal_.tableName ) {
                chosen = table_->count() - 1;
            }
        }
    }
    table_->setCurrentIndex( chosen );
    tableChosen();
}

void NameValueDialog::tableChosen()
{
    newTableName_->setEnabled( table_->currentIndex() <= 0 );
    populateRows();
    updatePreview();
}

void NameValueDialog::populateRows()
{
    QList<NameRow> rows;
    bool caseSensitive = false;
    const auto row = chosenGroup();
    const auto tableName = table_->currentData().toString();
    if ( row >= 0 && !tableName.isEmpty() ) {
        if ( const auto* table = groups_[ row ].table( tableName ) ) {
            rows = table->rows;
            caseSensitive = table->caseSensitive;
        }
    }

    // The value, as a key waiting for its name, unless a row has its key.
    const auto valueKey = logsquirl::valuenames::keyIdentity( proposal_.key, caseSensitive );
    auto valueRow = -1;
    for ( int index = 0; index < rows.size(); ++index ) {
        if ( logsquirl::valuenames::keyIdentity( rows[ index ].key, caseSensitive ) == valueKey ) {
            valueRow = index;
            break;
        }
    }
    if ( valueRow < 0 ) {
        rows.append( NameRow{ proposal_.key, QString{} } );
        valueRow = static_cast<int>( rows.size() ) - 1;
    }

    const QSignalBlocker blocker( rows_ );
    rows_->setRowCount( static_cast<int>( rows.size() ) );
    for ( int index = 0; index < rows.size(); ++index ) {
        rows_->setItem( index, RowKey, new QTableWidgetItem( rows[ index ].key ) );
        rows_->setItem( index, RowName, new QTableWidgetItem( rows[ index ].name ) );
    }
    // The name is what the user types next.
    rows_->setCurrentCell( valueRow, RowName );
    rows_->setFocus();
}

void NameValueDialog::addRow()
{
    const auto row = rows_->rowCount();
    {
        const QSignalBlocker blocker( rows_ );
        rows_->insertRow( row );
        rows_->setItem( row, RowKey, new QTableWidgetItem );
        rows_->setItem( row, RowName, new QTableWidgetItem );
    }
    rows_->setCurrentCell( row, RowKey );
    rows_->editItem( rows_->item( row, RowKey ) );
}

void NameValueDialog::removeRow()
{
    const auto row = rows_->currentRow();
    if ( row < 0 ) {
        return;
    }
    rows_->removeRow( row );
    updatePreview();
}

RuleWithTable NameValueDialog::ruleWithTable() const
{
    RuleWithTable added;

    const auto existing = table_->currentData().toString();
    added.existingTable = !existing.isEmpty();
    added.table.name = added.existingTable ? existing : newTableName_->text().trimmed();
    if ( const auto row = chosenGroup(); row >= 0 && added.existingTable ) {
        if ( const auto* table = groups_[ row ].table( existing ) ) {
            added.table.caseSensitive = table->caseSensitive;
        }
    }
    for ( int row = 0; row < rows_->rowCount(); ++row ) {
        const auto* key = rows_->item( row, RowKey );
        const auto* name = rows_->item( row, RowName );
        NameRow nameRow{ key != nullptr ? key->text() : QString{},
                         name != nullptr ? name->text() : QString{} };
        // A row left blank is none.
        if ( nameRow.key.isEmpty() && nameRow.name.isEmpty() ) {
            continue;
        }
        added.table.rows.append( nameRow );
    }

    added.rule.name = added.table.name;
    added.rule.pattern = pattern_->text();
    added.rule.displayTemplate = template_->text().isEmpty()
                                     ? logsquirl::valuenames::proposedTemplate()
                                     : template_->text();
    // A regex without a capture group looks up its whole match.
    const QRegularExpression regex( added.rule.pattern );
    added.captureGroup = regex.isValid() && regex.captureCount() == 0 ? QStringLiteral( "0" )
                                                                      : QStringLiteral( "1" );
    return added;
}

NamingGroup NameValueDialog::groupAfter() const
{
    const auto row = chosenGroup();
    auto group = row >= 0 ? groups_[ row ]
                          : NamingGroup::createNewGroup( newGroupName_->text().trimmed() );
    // What is named shows at once: the group is checked, as the new rule is.
    group.setEnabled( true );
    logsquirl::valuenames::addRuleWithTable( group, ruleWithTable(), tr( "Rule" ), tr( "Values" ) );
    return group;
}

QString NameValueDialog::previewWarning() const
{
    return warning_->text();
}

void NameValueDialog::updatePreview()
{
    const QRegularExpression regex( pattern_->text() );
    QString warning;
    if ( !regex.isValid() ) {
        warning = tr( "The regex is not valid: %1" ).arg( regex.errorString() );
    }
    else if ( !regex.match( logLine_ ).hasMatch() ) {
        warning = tr( "The regex does not match this Log Line, so it names nothing here." );
    }
    warning_->setText( warning );
    warning_->setVisible( !warning.isEmpty() );

    const logsquirl::valuenames::ValueNamer namer( { groupAfter() } );
    const auto namedValues = namer.namedValues( logLine_ );
    shownPreview_ = logsquirl::valuenames::shownLine( logLine_, namedValues );

    // The Named Values underlined, as the views mark them.
    QString html = QStringLiteral( "<span style=\"white-space: pre-wrap\">" );
    qsizetype position = 0;
    for ( const auto& namedValue : namedValues ) {
        html += logLine_.mid( position, namedValue.start - position ).toHtmlEscaped();
        html += QStringLiteral( "<u>" ) + namedValue.shown.toHtmlEscaped()
                + QStringLiteral( "</u>" );
        position = namedValue.end();
    }
    html += logLine_.mid( position ).toHtmlEscaped() + QStringLiteral( "</span>" );
    preview_->setText( html );
}

void NameValueDialog::accept()
{
    auto groups = groups_;
    const auto row = chosenGroup();
    if ( row >= 0 ) {
        groups[ row ] = groupAfter();
    }
    else {
        auto group = groupAfter();
        group.setName( logsquirl::valuenames::freeName(
            group.name().isEmpty() ? tr( "Value Names" ) : group.name(), groupNames( groups ) ) );
        groups.append( group );
    }

    auto& collection = ValueNamesCollection::get();
    collection.setGroups( groups );
    collection.save();
    QDialog::accept();
}
