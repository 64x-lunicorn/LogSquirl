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
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

#include "addtogrouplayout.h"
#include "groupchooser.h"
#include "valuenamer.h"
#include "valuenamescollection.h"

using logsquirl::valuenames::CapturedValue;
using logsquirl::valuenames::LineRange;
using logsquirl::valuenames::NameRow;
using logsquirl::valuenames::NameTable;
using logsquirl::valuenames::NamingGroup;
using logsquirl::valuenames::NamingProposal;
using logsquirl::valuenames::NamingRule;
using logsquirl::valuenames::RuleWithTable;

namespace {

enum RowColumn { RowKey = 0, RowName = 1 };

} // namespace

NameValueDialog::NameValueDialog( const QString& logLine, LineRange selection, QWidget* parent )
    : QDialog( parent )
    , logLine_( logLine )
    // The selection holds a value: the context menu offers the entry only
    // then, so there is always a proposal.
    , proposal_(
          logsquirl::valuenames::proposeNaming( logLine, selection ).value_or( NamingProposal{} ) )
{
    build( ValueNamesCollection::getSynced().ownGroups() );
}

NameValueDialog::NameValueDialog( const QString& logLine, const CapturedValue& captured,
                                  QWidget* parent )
    : QDialog( parent )
    , logLine_( logLine )
{
    auto own = ValueNamesCollection::getSynced().ownGroups();
    // The rule and the table, as the groups are now.
    const NamingRule* rule = nullptr;
    for ( const auto& group : own ) {
        if ( group.id() != captured.groupId || group.table( captured.table ) == nullptr ) {
            continue;
        }
        for ( const auto& groupRule : group.rules() ) {
            if ( groupRule.name == captured.rule ) {
                rule = &groupRule;
                break;
            }
        }
    }

    if ( rule == nullptr ) {
        proposal_ = logsquirl::valuenames::proposeNaming(
                        logLine, LineRange{ captured.start, captured.length } )
                        .value_or( NamingProposal{} );
    }
    else {
        captured_ = captured;
        // The row naming it as the groups are now: a sync may have moved it.
        const auto now = logsquirl::valuenames::capturedValueIn( own, logLine, captured.start,
                                                                 captured.length );
        captured_->row = now && now->groupId == captured.groupId && now->table == captured.table
                             ? now->row
                             : -1;
        proposal_.value = captured.value;
        proposal_.valueRange = LineRange{ captured.start, captured.length };
        proposal_.pattern = rule->pattern;
        proposal_.tableName = captured.table;
        proposal_.key = logsquirl::valuenames::escapedForRegex( captured.value );
        proposal_.displayTemplate = rule->displayTemplate;
    }
    build( std::move( own ) );
}

void NameValueDialog::build( QList<NamingGroup> own )
{
    if ( !captured_ ) {
        setWindowTitle( tr( "Name This Value" ) );
    }
    else if ( captured_->named() ) {
        setWindowTitle( tr( "Rename %1 %2" ).arg( captured_->table, captured_->value ) );
    }
    else {
        setWindowTitle( tr( "Name %1 in %2" ).arg( captured_->value, captured_->table ) );
    }

    // What is added, where it goes, its preview: the layout Add as Predefined
    // Filter... has too (#958).
    layout_ = std::make_unique<AddToGroupLayout>( this );
    using Section = AddToGroupLayout::Section;

    // The value as a key and its name, in one row; More rows... shows the
    // rows of the Name Table instead, to add and remove some.
    valueKey_ = new QLineEdit( this );
    valueKey_->setObjectName( QStringLiteral( "valueKey" ) );
    valueKey_->setToolTip( tr( "The key of the value in the Name Table" ) );
    valueName_ = new QLineEdit( this );
    valueName_->setObjectName( QStringLiteral( "valueName" ) );
    valueName_->setPlaceholderText( tr( "Its name" ) );
    valueName_->setToolTip( tr( "The name the value is shown with" ) );
    moreRows_ = new QPushButton( tr( "More rows..." ), this );
    moreRows_->setObjectName( QStringLiteral( "moreRows" ) );
    moreRows_->setToolTip( tr( "Show every row of the Name Table, to add and remove rows" ) );
    valueRow_ = new QWidget( this );
    auto* valueRow = new QHBoxLayout( valueRow_ );
    valueRow->setContentsMargins( 0, 0, 0, 0 );
    valueRow->addWidget( valueKey_, 1 );
    valueRow->addWidget( valueName_, 2 );
    valueRow->addWidget( moreRows_ );

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
    rowsBox_ = new QWidget( this );
    rowsBox_->setObjectName( QStringLiteral( "rowsBox" ) );
    auto* rowButtons = new QHBoxLayout;
    rowButtons->addWidget( addRowButton_ );
    rowButtons->addWidget( removeRowButton_ );
    rowButtons->addStretch( 1 );
    auto* rowsColumn = new QVBoxLayout( rowsBox_ );
    rowsColumn->setContentsMargins( 0, 0, 0, 0 );
    rowsColumn->addWidget( rows_ );
    rowsColumn->addLayout( rowButtons );
    rowsBox_->hide();

    auto* valueColumn = new QVBoxLayout;
    valueColumn->addWidget( valueRow_ );
    valueColumn->addWidget( rowsBox_ );
    layout_->addRow( Section::What, tr( "Value:" ), valueColumn );

    pattern_ = new QLineEdit( proposal_.pattern, this );
    pattern_->setObjectName( QStringLiteral( "rulePattern" ) );
    pattern_->setToolTip( tr( "The regex of the Naming Rule; its first capture group is the "
                              "value looked up in the Name Table" ) );
    layout_->addRow( Section::What, tr( "Naming Rule:" ), pattern_ );

    template_ = new QLineEdit( proposal_.displayTemplate, this );
    template_->setObjectName( QStringLiteral( "ruleTemplate" ) );
    template_->setToolTip( tr( "How a named value is shown: {name} is its name, {value} the "
                               "value itself" ) );
    layout_->addRow( Section::What, tr( "Template:" ), template_ );

    // The group: one of the user's own, a Team Naming Group once the dialog
    // is given them, or a new one of either, named here. The plugins' groups
    // are read-only.
    GroupChooser::Texts texts;
    texts.ownSection = tr( "My Naming Groups" );
    texts.teamSection = tr( "Team Naming Groups" );
    texts.newGroup = tr( "New Naming Group..." );
    texts.newTeamGroup = tr( "New Team Naming Group..." );
    texts.newGroupName = tr( "Value Names" );
    texts.teamHint = tr( "OK shares this Naming Group with the team." );
    texts.nameEmpty = tr( "The new Naming Group has no name." );
    texts.nameTaken = tr( "A Naming Group named \"%1\" exists already." );
    group_ = new GroupChooser( texts, this );
    group_->setObjectName( QStringLiteral( "namingGroup" ) );
    layout_->addRow( Section::Where, tr( "Naming Group:" ), group_ );

    // The table: a new one, named after the text before the value -- without
    // any, Name Table N, as the Value Names dialog names a new one -- or one
    // of the group's.
    table_ = new QComboBox( this );
    table_->setObjectName( QStringLiteral( "nameTable" ) );
    newTableName_ = new QLineEdit( proposal_.tableName, this );
    newTableName_->setObjectName( QStringLiteral( "newTableName" ) );
    auto* tableRow = new QHBoxLayout;
    tableRow->addWidget( table_, 1 );
    tableRow->addWidget( newTableName_, 1 );
    layout_->addRow( Section::Where, tr( "Name Table:" ), tableRow );

    groups_ = std::make_unique<ChooserGroups<NamingGroup>>(
        group_, std::move( own ),
        []( const NamingGroup& group ) {
            return logsquirl::valuenames::tableNames( group.tables() );
        },
        [ this ]( const QString& id ) { proposeGroup( id ); } );
    connect( group_, &GroupChooser::choiceChanged, this, &NameValueDialog::groupChosen );
    groups_->offer( {} );

    // The rule and its table are those of the group: only the rows change.
    if ( captured_ ) {
        group_->setEnabled( false );
        table_->setEnabled( false );
        newTableName_->hide();
        pattern_->setReadOnly( true );
        template_->setReadOnly( true );
    }

    connect( table_, &QComboBox::currentIndexChanged, this, &NameValueDialog::tableChosen );
    connect( newTableName_, &QLineEdit::textChanged, this, &NameValueDialog::updatePreview );
    connect( pattern_, &QLineEdit::textChanged, this, &NameValueDialog::updatePreview );
    connect( template_, &QLineEdit::textChanged, this, &NameValueDialog::updatePreview );
    connect( rows_, &QTableWidget::itemChanged, this, &NameValueDialog::rowChanged );
    connect( valueKey_, &QLineEdit::textChanged, this, &NameValueDialog::valueRowEdited );
    connect( valueName_, &QLineEdit::textChanged, this, &NameValueDialog::valueRowEdited );
    connect( moreRows_, &QPushButton::clicked, this, &NameValueDialog::showRows );
    connect( addRowButton_, &QPushButton::clicked, this, &NameValueDialog::addRow );
    connect( removeRowButton_, &QPushButton::clicked, this, &NameValueDialog::removeRow );

    layout_->fitDialog();
    focusName();
}

NameValueDialog::~NameValueDialog() = default;

bool NameValueDialog::showsRows() const
{
    return !rowsBox_->isHidden();
}

void NameValueDialog::showRows()
{
    rowsAskedFor_ = true;
    setRowsShown( true );
}

void NameValueDialog::setRowsShown( bool shown )
{
    if ( showsRows() == shown ) {
        return;
    }
    valueRow_->setVisible( !shown );
    rowsBox_->setVisible( shown );
    if ( shown ) {
        // Taller by the table, as wide as it was.
        resize( width(), std::max( height(), sizeHint().height() ) );
    }
    focusName();
}

void NameValueDialog::focusName()
{
    // The name is what the user types next.
    if ( showsRows() ) {
        rows_->setFocus();
    }
    else {
        valueName_->setFocus();
    }
}

void NameValueDialog::rowChanged( QTableWidgetItem* item )
{
    // The value's row shows in the one row too.
    if ( item != nullptr && item->row() == valueRowIndex_ ) {
        syncValueRow();
    }
    updatePreview();
}

void NameValueDialog::syncValueRow()
{
    const auto* key = valueRowIndex_ >= 0 ? rows_->item( valueRowIndex_, RowKey ) : nullptr;
    const auto* name = valueRowIndex_ >= 0 ? rows_->item( valueRowIndex_, RowName ) : nullptr;
    const QSignalBlocker keyBlocker( valueKey_ );
    const QSignalBlocker nameBlocker( valueName_ );
    valueKey_->setText( key != nullptr ? key->text() : QString{} );
    valueName_->setText( name != nullptr ? name->text() : QString{} );
}

void NameValueDialog::valueRowEdited()
{
    if ( valueRowIndex_ < 0 ) {
        return;
    }
    // Into the table, which OK reads, as though the row were edited there.
    const QSignalBlocker blocker( rows_ );
    rows_->item( valueRowIndex_, RowKey )->setText( valueKey_->text() );
    rows_->item( valueRowIndex_, RowName )->setText( valueName_->text() );
    updatePreview();
}

void NameValueDialog::showMessageOnly( LineRange message )
{
    // Only a message the value lies in: the preview marks the value.
    if ( message.start <= proposal_.valueRange.start
         && proposal_.valueRange.end() <= message.end() ) {
        message_ = message;
        updatePreview();
    }
}

void NameValueDialog::showTeamGroups( const QList<NamingGroup>& groups, bool editable,
                                      const QHash<QString, QString>& revisions )
{
    groups_->showTeam( groups, editable, revisions );
}

void NameValueDialog::proposeGroup( const QString& groupId )
{
    const auto fixed = fixedGroupId();
    group_->choose( fixed.isEmpty() ? groupId : fixed, {} );
    focusName();
}

QString NameValueDialog::fixedGroupId() const
{
    if ( captured_ ) {
        return captured_->groupId;
    }
    for ( const auto& groups : { groups_->own(), groups_->editableTeam() } ) {
        for ( const auto& group : groups ) {
            if ( group.table( proposal_.tableName ) != nullptr ) {
                return group.id();
            }
        }
    }
    return {};
}

const NamingGroup* NameValueDialog::chosenGroup() const
{
    return groups_->chosen();
}

QString NameValueDialog::chosenGroupId() const
{
    return groups_->chosenId();
}

void NameValueDialog::groupChosen()
{
    const auto chosen = group_->choice();
    // Typing a new group's name keeps the table and the rows as edited.
    if ( !tablesOf_ || !tablesOf_->isSameGroup( chosen ) ) {
        tablesOf_ = chosen;
        populateTables();
    }
    else {
        updatePreview();
    }
}

void NameValueDialog::populateTables()
{
    const QSignalBlocker blocker( table_ );
    table_->clear();
    table_->addItem( tr( "New Name Table" ), QString{} );
    int chosen = 0;
    const auto* group = chosenGroup();
    // Name Table N unique in the group chosen, unless the user named it.
    if ( proposal_.tableName.isEmpty() && !newTableName_->isModified() ) {
        newTableName_->setText( logsquirl::valuenames::newTableName(
            group != nullptr ? logsquirl::valuenames::tableNames( group->tables() )
                             : QStringList{} ) );
    }
    if ( group != nullptr ) {
        for ( const auto& table : group->tables() ) {
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
    const auto* group = chosenGroup();
    const auto tableName = table_->currentData().toString();
    if ( group != nullptr && !tableName.isEmpty() ) {
        if ( const auto* table = group->table( tableName ) ) {
            rows = table->rows;
            caseSensitive = table->caseSensitive;
        }
    }
    const auto tableRowCount = rows.size();

    // The value, as a key waiting for its name, unless a row has its key or,
    // for a Named Value, is the one naming it.
    const auto valueKey = logsquirl::valuenames::keyIdentity( proposal_.key, caseSensitive );
    auto valueRow = -1;
    if ( captured_ && tableName == captured_->table && captured_->row < rows.size() ) {
        valueRow = captured_->row;
    }
    for ( int index = 0; index < rows.size() && valueRow < 0; ++index ) {
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
    valueRowIndex_ = valueRow;
    syncValueRow();
    // A Name Table of more than one row shows them all; another, the value's
    // one row, unless the user asked for the rows with More rows....
    setRowsShown( tableRowCount > 1 || rowsAskedFor_ );
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
    if ( row == valueRowIndex_ ) {
        valueRowIndex_ = -1;
    }
    else if ( row < valueRowIndex_ ) {
        --valueRowIndex_;
    }
    updatePreview();
}

QList<NameRow> NameValueDialog::editedRows() const
{
    QList<NameRow> rows;
    for ( int row = 0; row < rows_->rowCount(); ++row ) {
        const auto* key = rows_->item( row, RowKey );
        const auto* name = rows_->item( row, RowName );
        NameRow nameRow{ key != nullptr ? key->text() : QString{},
                         name != nullptr ? name->text() : QString{} };
        // A row left blank is none.
        if ( nameRow.key.isEmpty() && nameRow.name.isEmpty() ) {
            continue;
        }
        rows.append( nameRow );
    }
    return rows;
}

RuleWithTable NameValueDialog::ruleWithTable() const
{
    RuleWithTable added;

    const auto existing = table_->currentData().toString();
    added.existingTable = !existing.isEmpty();
    added.table.name = added.existingTable ? existing : newTableName_->text().trimmed();
    if ( const auto* group = chosenGroup(); group != nullptr && added.existingTable ) {
        if ( const auto* table = group->table( existing ) ) {
            added.table.caseSensitive = table->caseSensitive;
        }
    }
    added.table.rows = editedRows();

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
    const auto* chosen = chosenGroup();
    // A value an existing rule captures: the rows of its table, nothing else.
    if ( captured_ && chosen != nullptr ) {
        auto group = *chosen;
        auto tables = group.tables();
        for ( auto& table : tables ) {
            if ( table.name == captured_->table ) {
                table.rows = editedRows();
                break;
            }
        }
        group.setTables( tables );
        return group;
    }
    auto group = chosen != nullptr ? *chosen : NamingGroup::createNewGroup( group_->choice().name );
    // What is named shows at once: the group is checked, as the new rule is.
    group.setEnabled( true );
    logsquirl::valuenames::addRuleWithTable( group, ruleWithTable(), tr( "Rule" ) );
    return group;
}

QString NameValueDialog::previewWarning() const
{
    return previewWarning_;
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
    previewWarning_ = warning;
    updateWarning();

    const logsquirl::valuenames::ValueNamer namer( { groupAfter() } );
    const auto namedValues = namer.namedValues( logLine_ );

    // The message, or the whole Log Line, as it will be shown: the Named
    // Values in it underlined, as the views mark them, and the value named
    // here marked too while it has no name yet.
    const auto message = message_.value_or( LineRange{ 0, logLine_.size() } );
    const auto& value = proposal_.valueRange;
    QString shown;
    QList<LinePreview::Mark> marks;
    bool valueMarked = false;
    auto position = message.start;
    const auto markRaw = [ & ]( qsizetype end ) {
        // The value, unnamed, where it lies before end.
        if ( !valueMarked && value.length > 0 && position <= value.start && value.end() <= end ) {
            shown += logLine_.mid( position, value.start - position );
            marks.append( LinePreview::Mark{ shown.size(), value.length } );
            shown += logLine_.mid( value.start, value.length );
            position = value.end();
            valueMarked = true;
        }
    };
    for ( const auto& namedValue : namedValues ) {
        if ( namedValue.start < position || namedValue.end() > message.end() ) {
            continue;
        }
        markRaw( namedValue.start );
        shown += logLine_.mid( position, namedValue.start - position );
        marks.append( LinePreview::Mark{ shown.size(), namedValue.shown.size() } );
        shown += namedValue.shown;
        position = namedValue.end();
        if ( namedValue.start <= value.start && value.end() <= namedValue.end() ) {
            valueMarked = true;
        }
    }
    markRaw( message.end() );
    shown += logLine_.mid( position, message.end() - position );
    shownPreview_ = shown;
    layout_->preview()->setLine( shown, marks, LinePreview::MarkStyle::Underline );
}

void NameValueDialog::updateWarning()
{
    // A new group's name that will not do comes first: OK waits for it.
    const auto groupWarning = group_->warning();
    const auto warning = groupWarning.isEmpty() ? previewWarning_ : groupWarning;
    layout_->setWarning( warning );
    layout_->buttons()->button( QDialogButtonBox::Ok )->setEnabled( groupWarning.isEmpty() );
}

void NameValueDialog::accept()
{
    const auto chosen = group_->choice();
    const auto group = groupAfter();

    auto& collection = ValueNamesCollection::get();
    if ( chosen.section == GroupChooser::Section::Team && !captured_ ) {
        // What is named shows at once: the group and the new rule are
        // checked. A Team group's checks are the user's own, kept in the
        // collection; the rules the user unchecked stay so.
        auto unchecked = collection.uncheckedKeys();
        unchecked.remove( ValueNamesCollection::groupCheckKey( group.id() ) );
        const auto* before = chosenGroup();
        for ( const auto& rule : group.rules() ) {
            const auto had
                = before != nullptr
                  && std::any_of( before->rules().cbegin(), before->rules().cend(),
                                  [ &rule ]( const auto& old ) { return old.name == rule.name; } );
            if ( !had ) {
                unchecked.remove( ValueNamesCollection::ruleCheckKey( group.id(), rule.name ) );
            }
        }
        collection.setUncheckedKeys( unchecked );

        // Published as the Value Names dialog publishes a Team group: what
        // changed against the group as given, based on its revision. It comes
        // back, to every window, through the Team Folder's sync.
        const auto requests = groups_->put( group );
        if ( !requests.isEmpty() ) {
            Q_EMIT publishRequested( requests );
        }
    }
    else {
        groups_->put( group );
        collection.setGroups( groups_->own() );
    }
    collection.save();
    QDialog::accept();
}
