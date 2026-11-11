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

#include <algorithm>

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

// The groups as the Group Chooser offers them, each with the names of its
// Name Tables.
QList<GroupChooser::Group> choicesOf( const QList<NamingGroup>& groups )
{
    QList<GroupChooser::Group> choices;
    for ( const auto& group : groups ) {
        choices.append( GroupChooser::Group{
            group.id(), group.name(), logsquirl::valuenames::tableNames( group.tables() ) } );
    }
    return choices;
}

const NamingGroup* groupOf( const QList<NamingGroup>& groups, const QString& id )
{
    for ( const auto& group : groups ) {
        if ( group.id() == id ) {
            return &group;
        }
    }
    return nullptr;
}

} // namespace

NameValueDialog::NameValueDialog( const QString& logLine, LineRange selection, QWidget* parent )
    : QDialog( parent )
    , logLine_( logLine )
    // The selection holds a value: the context menu offers the entry only
    // then, so there is always a proposal.
    , proposal_(
          logsquirl::valuenames::proposeNaming( logLine, selection ).value_or( NamingProposal{} ) )
    , groups_( ValueNamesCollection::getSynced().ownGroups() )
{
    build();
}

NameValueDialog::NameValueDialog( const QString& logLine, const CapturedValue& captured,
                                  QWidget* parent )
    : QDialog( parent )
    , logLine_( logLine )
    , groups_( ValueNamesCollection::getSynced().ownGroups() )
{
    // The rule and the table, as the groups are now.
    const NamingRule* rule = nullptr;
    for ( const auto& group : groups_ ) {
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
        const auto now = logsquirl::valuenames::capturedValueIn( groups_, logLine, captured.start,
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
    build();
}

void NameValueDialog::build()
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

    auto* form = new QFormLayout;

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
    form->addRow( tr( "Naming Group:" ), group_ );

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

    buttons_ = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this );
    auto* layout = new QVBoxLayout( this );
    layout->addLayout( form );
    layout->addWidget( buttons_ );

    connect( group_, &GroupChooser::choiceChanged, this, &NameValueDialog::groupChosen );
    offerGroups( {} );

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
    connect( rows_, &QTableWidget::itemChanged, this, &NameValueDialog::updatePreview );
    connect( addRowButton_, &QPushButton::clicked, this, &NameValueDialog::addRow );
    connect( removeRowButton_, &QPushButton::clicked, this, &NameValueDialog::removeRow );
    connect( buttons_, &QDialogButtonBox::accepted, this, &NameValueDialog::accept );
    connect( buttons_, &QDialogButtonBox::rejected, this, &NameValueDialog::reject );

    // The name is what the user types next.
    rows_->setFocus();
    resize( 640, sizeHint().height() );
}

void NameValueDialog::showTeamGroups( const QList<NamingGroup>& groups, bool editable,
                                      const QHash<QString, QString>& revisions )
{
    const auto chosen = group_->choice().id;
    teamEdits_.reset( groups, editable, revisions );
    showsTeam_ = true;
    offerGroups( chosen );
}

void NameValueDialog::proposeGroup( const QString& groupId )
{
    const auto fixed = fixedGroupId();
    group_->choose( fixed.isEmpty() ? groupId : fixed, {} );
    rows_->setFocus();
}

void NameValueDialog::offerGroups( const QString& chosenId )
{
    std::optional<GroupChooser::TeamGroups> team;
    if ( showsTeam_ ) {
        team
            = GroupChooser::TeamGroups{ choicesOf( teamEdits_.groups() ), teamEdits_.isEditable() };
    }
    group_->setGroups( choicesOf( groups_ ), team );
    const auto fixed = fixedGroupId();
    group_->choose( fixed.isEmpty() ? chosenId : fixed, {} );
    rows_->setFocus();
}

QString NameValueDialog::fixedGroupId() const
{
    if ( captured_ ) {
        return captured_->groupId;
    }
    for ( const auto& group : groups_ ) {
        if ( group.table( proposal_.tableName ) != nullptr ) {
            return group.id();
        }
    }
    if ( showsTeam_ && teamEdits_.isEditable() ) {
        for ( const auto& group : teamEdits_.groups() ) {
            if ( group.table( proposal_.tableName ) != nullptr ) {
                return group.id();
            }
        }
    }
    return {};
}

const NamingGroup* NameValueDialog::chosenGroup() const
{
    const auto chosen = group_->choice();
    if ( chosen.isNew() ) {
        return nullptr;
    }
    return groupOf( chosen.section == GroupChooser::Section::Team ? teamEdits_.groups() : groups_,
                    chosen.id );
}

QString NameValueDialog::chosenGroupId() const
{
    const auto chosen = group_->choice();
    return chosen.isNew() ? madeGroupId_ : chosen.id;
}

void NameValueDialog::groupChosen()
{
    const auto chosen = group_->choice();
    const std::pair<int, QString> tablesOf{ static_cast<int>( chosen.section ), chosen.id };
    // Typing a new group's name keeps the table and the rows as edited.
    if ( tablesOf_ != tablesOf ) {
        tablesOf_ = tablesOf;
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

void NameValueDialog::updateWarning()
{
    // A new group's name that will not do comes first: OK waits for it.
    const auto groupWarning = group_->warning();
    const auto warning = groupWarning.isEmpty() ? previewWarning_ : groupWarning;
    warning_->setText( warning );
    warning_->setVisible( !warning.isEmpty() );
    buttons_->button( QDialogButtonBox::Ok )->setEnabled( groupWarning.isEmpty() );
}

void NameValueDialog::accept()
{
    const auto chosen = group_->choice();
    auto group = groupAfter();
    if ( chosen.isNew() ) {
        madeGroupId_ = group.id();
    }

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
        auto& teamGroups = teamEdits_.groups();
        if ( chosen.isNew() ) {
            teamGroups.append( group );
        }
        else {
            for ( auto& teamGroup : teamGroups ) {
                if ( teamGroup.id() == chosen.id ) {
                    teamGroup = group;
                }
            }
        }
        const auto requests = teamEdits_.takeRequests();
        if ( !requests.isEmpty() ) {
            Q_EMIT publishRequested( requests );
        }
    }
    else {
        auto groups = groups_;
        if ( chosen.isNew() ) {
            groups.append( group );
        }
        else {
            for ( auto& ownGroup : groups ) {
                if ( ownGroup.id() == chosen.id ) {
                    ownGroup = group;
                }
            }
        }
        collection.setGroups( groups );
    }
    collection.save();
    QDialog::accept();
}
