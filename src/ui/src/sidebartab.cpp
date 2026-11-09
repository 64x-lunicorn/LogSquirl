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

#include "sidebartab.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QShowEvent>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include "plugingroup.h"
#include "soloselection.h"

namespace {

// What an item keeps of the group or entry it shows. The name of an entry is
// at Qt::UserRole, that of a group as given, without its Source or "(Team)".
constexpr int NameRole = Qt::UserRole;
constexpr int KeyRole = Qt::UserRole + 1;
constexpr int PatternRole = Qt::UserRole + 2;

QString entryText( const SidebarTab::Entry& entry )
{
    if ( entry.name != entry.pattern && !entry.name.isEmpty() ) {
        return entry.name + QStringLiteral( "  —  " ) + entry.pattern;
    }
    return entry.pattern;
}

// Whether the group or entry has a check of its own.
bool isCheck( const QTreeWidgetItem* item )
{
    return item->childCount() == 0 && !item->data( 0, KeyRole ).toString().isEmpty();
}

} // namespace

SidebarTab::SidebarTab( const QString& searchPlaceholder, QWidget* parent )
    : QWidget( parent )
{
    auto* mainLayout = new QVBoxLayout( this );
    mainLayout->setContentsMargins( 6, 6, 6, 6 );

    searchBox_ = new QLineEdit( this );
    searchBox_->setPlaceholderText( searchPlaceholder );
    searchBox_->setClearButtonEnabled( true );
    mainLayout->addWidget( searchBox_ );

    tree_ = new QTreeWidget( this );
    tree_->setHeaderHidden( true );
    tree_->setRootIsDecorated( true );
    mainLayout->addWidget( tree_ );

    // The units are the entries, and a group without entries that is a check
    // of its own (#889).
    solo_ = new SoloSelection( tree_, []( const QTreeWidgetItem* item ) {
        return item->childCount() > 0 ? QString{} : item->data( 0, KeyRole ).toString();
    } );
    connect( solo_, &SoloSelection::checksChanged, this, &SidebarTab::tellChecksChanged );

    emptyHint_ = new QLabel( this );
    emptyHint_->setWordWrap( true );
    emptyHint_->hide();
    mainLayout->addWidget( emptyHint_ );

    auto* buttonsLayout = new QHBoxLayout;
    selectAllButton_ = new QPushButton( tr( "Select All" ), this );
    deselectAllButton_ = new QPushButton( tr( "Deselect All" ), this );
    editButton_ = new QPushButton( tr( "Edit..." ), this );
    buttonsLayout->addWidget( selectAllButton_ );
    buttonsLayout->addWidget( deselectAllButton_ );
    buttonsLayout->addStretch();
    buttonsLayout->addWidget( editButton_ );
    mainLayout->addLayout( buttonsLayout );

    checksTimer_ = new QTimer( this );
    checksTimer_->setSingleShot( true );
    checksTimer_->setInterval( 0 );
    connect( checksTimer_, &QTimer::timeout, this, &SidebarTab::tellChecksChanged );

    connect( searchBox_, &QLineEdit::textChanged, this, &SidebarTab::onSearchTextChanged );
    connect( tree_, &QTreeWidget::itemChanged, this, &SidebarTab::onItemChanged );
    connect( tree_, &QTreeWidget::itemDoubleClicked, this, &SidebarTab::onItemDoubleClicked );
    connect( selectAllButton_, &QPushButton::clicked, this, &SidebarTab::selectAll );
    connect( deselectAllButton_, &QPushButton::clicked, this, &SidebarTab::deselectAll );
    connect( editButton_, &QPushButton::clicked, this, &SidebarTab::editRequested );
}

void SidebarTab::setGroups( const QList<Group>& groups )
{
    checksTimer_->stop();
    updatingTree_ = true;
    tree_->clear();

    for ( const auto& group : groups ) {
        auto* groupItem = new QTreeWidgetItem( tree_ );
        switch ( group.kind ) {
        case Group::Kind::Own:
            groupItem->setText( 0, group.name );
            groupItem->setToolTip( 0, SoloSelection::doubleClickHint() );
            break;
        case Group::Kind::Plugin:
            groupItem->setText( 0, pluginGroupShownName( group.name, group.pluginName ) );
            groupItem->setToolTip( 0,
                                   SoloSelection::toolTipWithHint(
                                       tr( "Shipped by the plugin %1" ).arg( group.pluginName ) ) );
            break;
        case Group::Kind::Team:
            groupItem->setText( 0, tr( "%1 (Team)" ).arg( group.name ) );
            groupItem->setToolTip( 0, SoloSelection::toolTipWithHint(
                                          tr( "A Team group, shared through the Team Folder" ) ) );
            break;
        }
        groupItem->setFlags( groupItem->flags() | Qt::ItemIsAutoTristate
                             | Qt::ItemIsUserCheckable );
        groupItem->setData( 0, NameRole, group.name );
        groupItem->setData( 0, KeyRole, group.key );
        if ( group.entries.isEmpty() && !group.key.isEmpty() ) {
            groupItem->setCheckState( 0, group.checked ? Qt::Checked : Qt::Unchecked );
        }

        for ( const auto& entry : group.entries ) {
            auto* entryItem = new QTreeWidgetItem( groupItem );
            entryItem->setText( 0, entryText( entry ) );
            entryItem->setToolTip( 0, SoloSelection::toolTipWithHint( entry.pattern ) );
            entryItem->setFlags( entryItem->flags() | Qt::ItemIsUserCheckable );
            entryItem->setData( 0, NameRole, entry.name );
            entryItem->setData( 0, KeyRole, entry.key );
            entryItem->setData( 0, PatternRole, entry.pattern );
            entryItem->setCheckState( 0, entry.checked ? Qt::Checked : Qt::Unchecked );
        }
        groupItem->setExpanded( true );
    }

    applySearch();
    updatingTree_ = false;

    emptyHint_->setVisible( groups.isEmpty() && !emptyHint_->text().isEmpty() );
}

void SidebarTab::showEvent( QShowEvent* event )
{
    QWidget::showEvent( event );
    // Rows laid out before the tab was placed in the sidebar keep the height
    // they had without the Theme's padding until laid out again (#924).
    tree_->doItemsLayout();
}

void SidebarTab::setEmptyHint( const QString& hint )
{
    emptyHint_->setText( hint );
    emptyHint_->setVisible( tree_->topLevelItemCount() == 0 && !hint.isEmpty() );
}

void SidebarTab::applySearch()
{
    const auto searchText = searchBox_->text().trimmed();
    const auto matches = [ &searchText ]( const QTreeWidgetItem* item, int role ) {
        return item->data( 0, role ).toString().contains( searchText, Qt::CaseInsensitive );
    };

    for ( int g = 0; g < tree_->topLevelItemCount(); ++g ) {
        auto* groupItem = tree_->topLevelItem( g );
        // A group whose name matches shows all its entries.
        const bool groupMatches = searchText.isEmpty() || matches( groupItem, NameRole );
        bool anyEntryShown = false;
        for ( int e = 0; e < groupItem->childCount(); ++e ) {
            auto* entryItem = groupItem->child( e );
            // An entry the search does not show keeps its check (#901).
            const bool shown = groupMatches || matches( entryItem, NameRole )
                               || matches( entryItem, PatternRole );
            entryItem->setHidden( !shown );
            anyEntryShown = anyEntryShown || shown;
        }
        // A group without entries shows while there is no search, or when it
        // is a check of its own whose name matches.
        const bool groupShown
            = searchText.isEmpty() || anyEntryShown || ( groupMatches && isCheck( groupItem ) );
        groupItem->setHidden( !groupShown );
    }
}

void SidebarTab::onItemChanged( QTreeWidgetItem* item, int column )
{
    Q_UNUSED( item );
    Q_UNUSED( column );
    if ( updatingTree_ ) {
        return;
    }
    checksTimer_->start();
}

void SidebarTab::onItemDoubleClicked( QTreeWidgetItem* item, int column )
{
    Q_UNUSED( column );
    if ( item == nullptr ) {
        return;
    }
    // Solo: only the entry double-clicked, or the entries of the group, stay
    // checked across all groups, whatever the search shows.
    solo_->showOnly( item );
}

void SidebarTab::onSearchTextChanged( const QString& text )
{
    Q_UNUSED( text );
    updatingTree_ = true;
    applySearch();
    updatingTree_ = false;
    Q_EMIT searchChanged();
}

void SidebarTab::selectAll()
{
    setShownChecked( Qt::Checked );
}

void SidebarTab::deselectAll()
{
    setShownChecked( Qt::Unchecked );
}

void SidebarTab::setShownChecked( Qt::CheckState state )
{
    // What the search hides keeps its check (#901).
    updatingTree_ = true;
    for ( int g = 0; g < tree_->topLevelItemCount(); ++g ) {
        auto* groupItem = tree_->topLevelItem( g );
        if ( groupItem->isHidden() ) {
            continue;
        }
        if ( isCheck( groupItem ) ) {
            groupItem->setCheckState( 0, state );
        }
        for ( int e = 0; e < groupItem->childCount(); ++e ) {
            if ( !groupItem->child( e )->isHidden() ) {
                groupItem->child( e )->setCheckState( 0, state );
            }
        }
    }
    updatingTree_ = false;
    tellChecksChanged();
}

QStringList SidebarTab::checkedKeys() const
{
    return keysWithCheck( true );
}

QStringList SidebarTab::uncheckedKeys() const
{
    return keysWithCheck( false );
}

QStringList SidebarTab::keysWithCheck( bool checked ) const
{
    const auto hasCheck = [ checked ]( const QTreeWidgetItem* item ) {
        return ( item->checkState( 0 ) == Qt::Checked ) == checked;
    };

    QStringList keys;
    for ( int g = 0; g < tree_->topLevelItemCount(); ++g ) {
        const auto* groupItem = tree_->topLevelItem( g );
        if ( isCheck( groupItem ) && hasCheck( groupItem ) ) {
            keys.append( groupItem->data( 0, KeyRole ).toString() );
        }
        for ( int e = 0; e < groupItem->childCount(); ++e ) {
            const auto* entryItem = groupItem->child( e );
            if ( hasCheck( entryItem ) ) {
                keys.append( entryItem->data( 0, KeyRole ).toString() );
            }
        }
    }
    return keys;
}

void SidebarTab::tellChecksChanged()
{
    checksTimer_->stop();
    Q_EMIT checksChanged();
}
