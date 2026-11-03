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

#include "grouplist.h"

#include <QAction>
#include <QBoxLayout>
#include <QHeaderView>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QToolButton>
#include <QTreeWidget>

#include "iconloader.h"
#include "theme.h"

namespace {

using logsquirl::groupexchange::GroupKind;

// What the list says of a kind of group: the same words for every kind,
// apart from the kind's name. Spelled out whole, so that a translation can
// inflect the kind's name.
struct KindTexts {
    QString ownSection;
    QString teamSection;
    QString pluginSection;
    QString newGroup;
    QString newTeamGroup;
    QString deleteGroup;
    QString moveUp;
    QString moveDown;
    QString deleteTeamGroup;
    QString exportTip;
    QString importTip;
};

KindTexts textsOf( GroupKind kind )
{
    KindTexts texts;
    switch ( kind ) {
    case GroupKind::Filter:
        texts.ownSection = GroupList::tr( "My Filter Groups" );
        texts.teamSection = GroupList::tr( "Team Filter Groups" );
        texts.pluginSection = GroupList::tr( "Plugin Filter Groups" );
        texts.newGroup = GroupList::tr( "New Filter Group" );
        texts.newTeamGroup = GroupList::tr( "New Team Filter Group" );
        texts.deleteGroup = GroupList::tr( "Delete Filter Group" );
        texts.moveUp = GroupList::tr( "Move Filter Group Up" );
        texts.moveDown = GroupList::tr( "Move Filter Group Down" );
        texts.deleteTeamGroup = GroupList::tr( "Delete Team Filter Group" );
        texts.exportTip = GroupList::tr( "Writes the selected Filter Group to a file of its own." );
        texts.importTip = GroupList::tr( "Adds the Filter Groups of files to your own." );
        break;
    case GroupKind::Highlighter:
        texts.ownSection = GroupList::tr( "My Highlighter Sets" );
        texts.teamSection = GroupList::tr( "Team Highlighter Sets" );
        texts.pluginSection = GroupList::tr( "Plugin Highlighter Sets" );
        texts.newGroup = GroupList::tr( "New Highlighter Set" );
        texts.newTeamGroup = GroupList::tr( "New Team Highlighter Set" );
        texts.deleteGroup = GroupList::tr( "Delete Highlighter Set" );
        texts.moveUp = GroupList::tr( "Move Highlighter Set Up" );
        texts.moveDown = GroupList::tr( "Move Highlighter Set Down" );
        texts.deleteTeamGroup = GroupList::tr( "Delete Team Highlighter Set" );
        texts.exportTip
            = GroupList::tr( "Writes the selected Highlighter Set to a file of its own." );
        texts.importTip = GroupList::tr( "Adds the Highlighter Sets of files to your own." );
        break;
    case GroupKind::ValueNames:
        texts.ownSection = GroupList::tr( "My Naming Groups" );
        texts.teamSection = GroupList::tr( "Team Naming Groups" );
        texts.pluginSection = GroupList::tr( "Plugin Naming Groups" );
        texts.newGroup = GroupList::tr( "New Naming Group" );
        texts.newTeamGroup = GroupList::tr( "New Team Naming Group" );
        texts.deleteGroup = GroupList::tr( "Delete Naming Group" );
        texts.moveUp = GroupList::tr( "Move Naming Group Up" );
        texts.moveDown = GroupList::tr( "Move Naming Group Down" );
        texts.deleteTeamGroup = GroupList::tr( "Delete Team Naming Group" );
        texts.exportTip = GroupList::tr( "Writes the selected Naming Group to a file of its own." );
        texts.importTip = GroupList::tr( "Adds the Naming Groups of files to your own." );
        break;
    }
    return texts;
}

constexpr int SectionRole = Qt::UserRole;

QToolButton* iconButton( const QString& toolTip, QWidget* parent )
{
    auto* button = new QToolButton( parent );
    button->setToolTip( toolTip );
    button->setAccessibleName( toolTip );
    return button;
}

} // namespace

GroupList::GroupList( GroupKind kind, QWidget* parent )
    : QWidget( parent )
    , kind_( kind )
{
    const auto texts = textsOf( kind );

    auto* layout = new QVBoxLayout( this );
    layout->setContentsMargins( 0, 0, 0, 0 );

    tree_ = new QTreeWidget( this );
    tree_->setObjectName( QStringLiteral( "groupList" ) );
    tree_->setHeaderHidden( true );
    tree_->setColumnCount( 1 );
    tree_->setRootIsDecorated( false );
    tree_->setItemsExpandable( false );
    tree_->setIndentation( 12 );
    tree_->setSelectionMode( QAbstractItemView::SingleSelection );
    tree_->setSizePolicy( QSizePolicy::MinimumExpanding, QSizePolicy::Expanding );
    layout->addWidget( tree_ );
    sectionItem( Section::Own, true );

    newButton_ = new QToolButton( this );
    newButton_->setObjectName( QStringLiteral( "groupNew" ) );
    newButton_->setText( texts.newGroup );
    newButton_->setToolButtonStyle( Qt::ToolButtonTextBesideIcon );
    // With Team groups that can be changed, the arrow beside it offers a new
    // Team group.
    newMenu_ = new QMenu( newButton_ );
    newOwnAction_ = newMenu_->addAction( texts.newGroup );
    newTeamAction_ = newMenu_->addAction( texts.newTeamGroup );
    newTeamAction_->setObjectName( QStringLiteral( "teamAdd" ) );
    newTeamAction_->setVisible( false );
    deleteButton_ = iconButton( texts.deleteGroup, this );
    deleteButton_->setObjectName( QStringLiteral( "groupDelete" ) );
    upButton_ = iconButton( texts.moveUp, this );
    upButton_->setObjectName( QStringLiteral( "groupUp" ) );
    downButton_ = iconButton( texts.moveDown, this );
    downButton_->setObjectName( QStringLiteral( "groupDown" ) );
    shareButton_ = new QPushButton( tr( "Share with team" ), this );
    shareButton_->setObjectName( QStringLiteral( "teamShare" ) );
    shareButton_->setToolTip( tr( "Adds a Team copy of the selected group of your own." ) );
    copyButton_ = new QPushButton( tr( "Copy to my groups" ), this );
    copyButton_->setObjectName( QStringLiteral( "groupCopy" ) );
    copyButton_->setToolTip( tr( "Adds a copy of the selected group to your own." ) );
    auto* buttons = new QHBoxLayout;
    buttons->setSpacing( 6 );
    for ( auto* button : { newButton_, deleteButton_, upButton_, downButton_ } ) {
        buttons->addWidget( button );
    }
    buttons->addStretch();
    buttons->addWidget( shareButton_ );
    buttons->addWidget( copyButton_ );
    layout->addLayout( buttons );

    exportButton_ = new QPushButton( tr( "Export..." ), this );
    exportButton_->setObjectName( QStringLiteral( "exportGroup" ) );
    exportButton_->setToolTip( texts.exportTip );
    importButton_ = new QPushButton( tr( "Import..." ), this );
    importButton_->setObjectName( QStringLiteral( "importGroups" ) );
    importButton_->setToolTip( texts.importTip );
    auto* exchange = new QHBoxLayout;
    exchange->addWidget( exportButton_ );
    exchange->addWidget( importButton_ );
    exchange->addStretch();
    layout->addLayout( exchange );

    connect( tree_, &QTreeWidget::itemSelectionChanged, this, &GroupList::selectionChanged );
    connect( newButton_, &QToolButton::clicked, this,
             [ this ] { Q_EMIT newRequested( Section::Own ); } );
    connect( newOwnAction_, &QAction::triggered, this,
             [ this ] { Q_EMIT newRequested( Section::Own ); } );
    connect( newTeamAction_, &QAction::triggered, this, [ this ] {
        if ( teamEditable_ ) {
            Q_EMIT newRequested( Section::Team );
        }
    } );
    connect( deleteButton_, &QToolButton::clicked, this, &GroupList::deleteSelected );
    connect( upButton_, &QToolButton::clicked, this, [ this ] {
        if ( section_ == Section::Own && row_ > 0 ) {
            Q_EMIT moveRequested( row_, -1 );
        }
    } );
    connect( downButton_, &QToolButton::clicked, this, [ this ] {
        if ( section_ == Section::Own && row_ >= 0 && row_ < count( Section::Own ) - 1 ) {
            Q_EMIT moveRequested( row_, 1 );
        }
    } );
    connect( shareButton_, &QPushButton::clicked, this, [ this ] {
        if ( teamEditable_ && section_ == Section::Own && row_ >= 0 ) {
            Q_EMIT shareRequested( row_ );
        }
    } );
    connect( copyButton_, &QPushButton::clicked, this, [ this ] {
        if ( section_ != Section::Own && row_ >= 0 ) {
            Q_EMIT copyRequested( section_, row_ );
        }
    } );
    connect( exportButton_, &QPushButton::clicked, this, [ this ] {
        if ( row_ >= 0 ) {
            Q_EMIT exportRequested( section_, row_ );
        }
    } );
    connect( importButton_, &QPushButton::clicked, this, &GroupList::importRequested );

    updateButtons();
    loadIcons();
    Theme::whenApplied( this, [ this ] { loadIcons(); } );
}

void GroupList::loadIcons()
{
    loadListEditIcons( newButton_, deleteButton_, upButton_, downButton_ );
}

// --- The sections ---

const QTreeWidgetItem* GroupList::sectionItem( Section section ) const
{
    for ( int index = 0; index < tree_->topLevelItemCount(); ++index ) {
        const auto* item = tree_->topLevelItem( index );
        if ( item->data( 0, SectionRole ).toInt() == static_cast<int>( section ) ) {
            return item;
        }
    }
    return nullptr;
}

QTreeWidgetItem* GroupList::sectionItem( Section section, bool create )
{
    int place = 0;
    for ( ; place < tree_->topLevelItemCount(); ++place ) {
        auto* item = tree_->topLevelItem( place );
        const auto itemSection = item->data( 0, SectionRole ).toInt();
        if ( itemSection == static_cast<int>( section ) ) {
            return item;
        }
        if ( itemSection > static_cast<int>( section ) ) {
            break;
        }
    }
    if ( !create ) {
        return nullptr;
    }

    const auto texts = textsOf( kind_ );
    auto* item = new QTreeWidgetItem;
    item->setData( 0, SectionRole, static_cast<int>( section ) );
    switch ( section ) {
    case Section::Own:
        item->setText( 0, texts.ownSection );
        break;
    case Section::Team:
        item->setText( 0, texts.teamSection );
        item->setToolTip(
            0, tr( "Shared through the Team Folder: they change when the team changes them." ) );
        break;
    case Section::Plugin:
        item->setText( 0, texts.pluginSection );
        item->setToolTip( 0, tr( "Shipped by the enabled plugins: read-only, and gone when the "
                                 "plugin is disabled." ) );
        break;
    }
    // A title, not a group: never selected. Enabled, as a disabled item
    // disables its children too.
    item->setFlags( Qt::ItemIsEnabled );
    auto font = item->font( 0 );
    font.setBold( true );
    item->setFont( 0, font );
    tree_->insertTopLevelItem( place, item );
    item->setExpanded( true );
    return item;
}

bool GroupList::hasSection( Section section ) const
{
    return sectionItem( section ) != nullptr;
}

int GroupList::count( Section section ) const
{
    const auto* item = sectionItem( section );
    return item != nullptr ? item->childCount() : 0;
}

QString GroupList::name( Section section, int row ) const
{
    const auto* item = sectionItem( section );
    if ( item == nullptr || row < 0 || row >= item->childCount() ) {
        return {};
    }
    return item->child( row )->text( 0 );
}

void GroupList::setName( Section section, int row, const QString& name )
{
    auto* item = sectionItem( section, false );
    if ( item != nullptr && row >= 0 && row < item->childCount() ) {
        item->child( row )->setText( 0, name );
    }
}

void GroupList::setOwnGroups( const QStringList& names, const QSet<int>& undeletable )
{
    undeletable_ = undeletable;
    setGroups( Section::Own, names );
}

void GroupList::setTeamGroups( const QStringList& names, bool editable )
{
    teamEditable_ = editable;
    setGroups( Section::Team, names );
}

void GroupList::setPluginGroupNames( const QStringList& names )
{
    if ( names.isEmpty() && !hasSection( Section::Plugin ) ) {
        return;
    }
    setGroups( Section::Plugin, names );
    if ( names.isEmpty() ) {
        delete sectionItem( Section::Plugin, false );
    }
}

void GroupList::setGroups( Section section, const QStringList& names )
{
    const bool wasSelected = row_ >= 0 && section_ == section;

    updating_ = true;
    auto* item = sectionItem( section, true );
    if ( wasSelected ) {
        tree_->clearSelection();
        tree_->setCurrentItem( nullptr );
    }
    qDeleteAll( item->takeChildren() );
    for ( const auto& name : names ) {
        auto* child = new QTreeWidgetItem( item, { name } );
        child->setFlags( Qt::ItemIsEnabled | Qt::ItemIsSelectable );
    }
    item->setExpanded( true );
    updating_ = false;

    if ( wasSelected ) {
        row_ = -1;
        updateButtons();
        Q_EMIT currentChanged( section, -1 );
    }
    else {
        updateButtons();
    }
}

// --- What is selected ---

GroupList::Section GroupList::currentSection() const
{
    return section_;
}

int GroupList::currentRow() const
{
    return row_;
}

void GroupList::setCurrent( Section section, int row )
{
    auto* item = sectionItem( section, false );
    const bool exists = item != nullptr && row >= 0 && row < item->childCount();

    updating_ = true;
    if ( exists ) {
        auto* child = item->child( row );
        tree_->setCurrentItem( child );
        child->setSelected( true );
        tree_->scrollToItem( child );
    }
    else {
        tree_->clearSelection();
        tree_->setCurrentItem( nullptr );
    }
    updating_ = false;

    section_ = section;
    row_ = exists ? row : -1;
    updateButtons();
    Q_EMIT currentChanged( section_, row_ );
}

void GroupList::selectionChanged()
{
    if ( updating_ ) {
        return;
    }
    const auto selected = tree_->selectedItems();
    const auto* parent = selected.isEmpty() ? nullptr : selected.front()->parent();
    if ( parent == nullptr ) {
        row_ = -1;
    }
    else {
        section_ = static_cast<Section>( parent->data( 0, SectionRole ).toInt() );
        row_ = parent->indexOfChild( selected.front() );
    }
    updateButtons();
    Q_EMIT currentChanged( section_, row_ );
}

void GroupList::updateButtons()
{
    const bool ownSelected = section_ == Section::Own && row_ >= 0;
    const bool teamSelected = section_ == Section::Team && row_ >= 0;
    const bool pluginSelected = section_ == Section::Plugin && row_ >= 0;
    const bool teamChangeable = teamEditable_ && hasSection( Section::Team );

    newTeamAction_->setVisible( teamChangeable );
    if ( teamChangeable != ( newButton_->menu() != nullptr ) ) {
        newButton_->setMenu( teamChangeable ? newMenu_ : nullptr );
        newButton_->setPopupMode( teamChangeable ? QToolButton::MenuButtonPopup
                                                 : QToolButton::DelayedPopup );
    }

    deleteButton_->setEnabled( ( ownSelected && !undeletable_.contains( row_ ) )
                               || ( teamSelected && teamEditable_ ) );
    upButton_->setEnabled( ownSelected && row_ > 0 );
    downButton_->setEnabled( ownSelected && row_ < count( Section::Own ) - 1 );

    shareButton_->setVisible( teamChangeable );
    shareButton_->setEnabled( ownSelected );
    copyButton_->setVisible( hasSection( Section::Team ) || hasSection( Section::Plugin ) );
    copyButton_->setEnabled( teamSelected || pluginSelected );

    exportButton_->setEnabled( row_ >= 0 );
}

void GroupList::deleteSelected()
{
    if ( row_ < 0 ) {
        return;
    }
    if ( section_ == Section::Own && !undeletable_.contains( row_ ) ) {
        Q_EMIT deleteRequested( section_, row_ );
    }
    else if ( section_ == Section::Team && teamEditable_ ) {
        const auto answer = QMessageBox::question(
            this, textsOf( kind_ ).deleteTeamGroup,
            tr( "This deletes the group for the whole team." ), QMessageBox::Yes | QMessageBox::No,
            QMessageBox::No );
        if ( answer == QMessageBox::Yes ) {
            Q_EMIT deleteRequested( section_, row_ );
        }
    }
}
