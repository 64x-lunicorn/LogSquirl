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

#include "valuenamespanel.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSet>
#include <QTimer>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include "valuenamescollection.h"

namespace {

// What an item keeps of the group or rule it shows.
constexpr int GroupIdRole = Qt::UserRole;
constexpr int RuleNameRole = Qt::UserRole + 1;

} // namespace

ValueNamesPanel::ValueNamesPanel( QWidget* parent )
    : QWidget( parent )
{
    auto* mainLayout = new QVBoxLayout( this );
    mainLayout->setContentsMargins( 6, 6, 6, 6 );

    searchBox_ = new QLineEdit( this );
    searchBox_->setPlaceholderText( tr( "Search Value Names..." ) );
    searchBox_->setClearButtonEnabled( true );
    mainLayout->addWidget( searchBox_ );

    tree_ = new QTreeWidget( this );
    tree_->setHeaderHidden( true );
    tree_->setRootIsDecorated( true );
    mainLayout->addWidget( tree_ );

    emptyHint_ = new QLabel( tr( "No Naming Groups yet: Edit... adds one." ), this );
    emptyHint_->setWordWrap( true );
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

    commitTimer_ = new QTimer( this );
    commitTimer_->setSingleShot( true );
    commitTimer_->setInterval( 0 );
    connect( commitTimer_, &QTimer::timeout, this, &ValueNamesPanel::commitChecks );

    connect( searchBox_, &QLineEdit::textChanged, this, &ValueNamesPanel::onSearchTextChanged );
    connect( tree_, &QTreeWidget::itemChanged, this, &ValueNamesPanel::onItemChanged );
    connect( tree_, &QTreeWidget::itemDoubleClicked, this, &ValueNamesPanel::onItemDoubleClicked );
    connect( selectAllButton_, &QPushButton::clicked, this, &ValueNamesPanel::selectAll );
    connect( deselectAllButton_, &QPushButton::clicked, this, &ValueNamesPanel::deselectAll );
    connect( editButton_, &QPushButton::clicked, this, &ValueNamesPanel::editRequested );

    refresh();
}

void ValueNamesPanel::refresh()
{
    const auto& collection = ValueNamesCollection::get();
    if ( collection.generation() == shownGeneration_ ) {
        return;
    }
    // A check changed and not yet written is overtaken by what the
    // collection holds now.
    commitTimer_->stop();
    populateTree();
}

void ValueNamesPanel::populateTree()
{
    const auto& collection = ValueNamesCollection::get();
    const auto& groups = collection.groups();

    updatingTree_ = true;
    tree_->clear();

    // The Team groups come last, after the user's own.
    const auto firstTeamGroup = groups.size() - collection.teamGroupCount();
    for ( qsizetype index = 0; index < groups.size(); ++index ) {
        const auto& group = groups[ index ];
        auto* groupItem = new QTreeWidgetItem( tree_ );
        groupItem->setText( 0, group.name() );
        if ( index >= firstTeamGroup ) {
            groupItem->setText( 0, tr( "%1 (Team)" ).arg( group.name() ) );
            groupItem->setToolTip( 0, tr( "A Team group, shared through the Team Folder" ) );
        }
        groupItem->setFlags( groupItem->flags() | Qt::ItemIsAutoTristate
                             | Qt::ItemIsUserCheckable );
        groupItem->setData( 0, GroupIdRole, group.id() );
        // A group without rules has a check of its own; that of a group with
        // rules is theirs.
        groupItem->setCheckState( 0, group.isEnabled() ? Qt::Checked : Qt::Unchecked );

        for ( const auto& rule : group.rules() ) {
            auto* ruleItem = new QTreeWidgetItem( groupItem );
            ruleItem->setText( 0, rule.name.isEmpty()
                                      ? rule.pattern
                                      : rule.name + QStringLiteral( "  —  " ) + rule.pattern );
            ruleItem->setToolTip( 0, rule.pattern );
            ruleItem->setFlags( ruleItem->flags() | Qt::ItemIsUserCheckable );
            ruleItem->setData( 0, GroupIdRole, group.id() );
            ruleItem->setData( 0, RuleNameRole, rule.name );
            ruleItem->setCheckState( 0, group.isEnabled() && rule.enabled ? Qt::Checked
                                                                          : Qt::Unchecked );
        }
        groupItem->setExpanded( true );
    }

    applySearch();
    updatingTree_ = false;

    emptyHint_->setVisible( groups.isEmpty() );
    shownGeneration_ = collection.generation();
}

void ValueNamesPanel::applySearch()
{
    const auto searchText = searchBox_->text().trimmed();
    const auto matches = [ &searchText ]( const QString& text ) {
        return text.contains( searchText, Qt::CaseInsensitive );
    };

    for ( int g = 0; g < tree_->topLevelItemCount(); ++g ) {
        auto* groupItem = tree_->topLevelItem( g );
        // A group whose name matches shows all its rules.
        const bool groupMatches = searchText.isEmpty() || matches( groupItem->text( 0 ) );
        bool anyRuleShown = false;
        for ( int r = 0; r < groupItem->childCount(); ++r ) {
            auto* ruleItem = groupItem->child( r );
            const bool shown = groupMatches || matches( ruleItem->text( 0 ) );
            ruleItem->setHidden( !shown );
            anyRuleShown = anyRuleShown || shown;
        }
        groupItem->setHidden( !groupMatches && !anyRuleShown );
    }
}

void ValueNamesPanel::onItemChanged( QTreeWidgetItem* item, int column )
{
    Q_UNUSED( item );
    Q_UNUSED( column );
    if ( updatingTree_ ) {
        return;
    }
    commitTimer_->start();
}

void ValueNamesPanel::onItemDoubleClicked( QTreeWidgetItem* item, int column )
{
    Q_UNUSED( column );
    if ( item == nullptr ) {
        return;
    }

    // Solo: only the rule double-clicked, or the rules of the group, stay
    // checked, whatever the search shows.
    updatingTree_ = true;
    for ( int g = 0; g < tree_->topLevelItemCount(); ++g ) {
        auto* groupItem = tree_->topLevelItem( g );
        const bool soloGroup = groupItem == item;
        if ( groupItem->childCount() == 0 ) {
            groupItem->setCheckState( 0, soloGroup ? Qt::Checked : Qt::Unchecked );
        }
        for ( int r = 0; r < groupItem->childCount(); ++r ) {
            auto* ruleItem = groupItem->child( r );
            ruleItem->setCheckState( 0,
                                     soloGroup || ruleItem == item ? Qt::Checked : Qt::Unchecked );
        }
    }
    updatingTree_ = false;
    commitChecks();
}

void ValueNamesPanel::onSearchTextChanged( const QString& text )
{
    Q_UNUSED( text );
    applySearch();
}

void ValueNamesPanel::selectAll()
{
    setShownChecked( true );
}

void ValueNamesPanel::deselectAll()
{
    setShownChecked( false );
}

void ValueNamesPanel::setShownChecked( bool checked )
{
    const auto state = checked ? Qt::Checked : Qt::Unchecked;
    updatingTree_ = true;
    for ( int g = 0; g < tree_->topLevelItemCount(); ++g ) {
        auto* groupItem = tree_->topLevelItem( g );
        if ( groupItem->isHidden() ) {
            continue;
        }
        if ( groupItem->childCount() == 0 ) {
            groupItem->setCheckState( 0, state );
        }
        for ( int r = 0; r < groupItem->childCount(); ++r ) {
            if ( !groupItem->child( r )->isHidden() ) {
                groupItem->child( r )->setCheckState( 0, state );
            }
        }
    }
    updatingTree_ = false;
    commitChecks();
}

void ValueNamesPanel::commitChecks()
{
    commitTimer_->stop();

    QSet<QString> unchecked;
    for ( int g = 0; g < tree_->topLevelItemCount(); ++g ) {
        const auto* groupItem = tree_->topLevelItem( g );
        const auto groupId = groupItem->data( 0, GroupIdRole ).toString();
        if ( groupItem->childCount() == 0 ) {
            if ( groupItem->checkState( 0 ) == Qt::Unchecked ) {
                unchecked.insert( ValueNamesCollection::groupCheckKey( groupId ) );
            }
            continue;
        }
        for ( int r = 0; r < groupItem->childCount(); ++r ) {
            const auto* ruleItem = groupItem->child( r );
            if ( ruleItem->checkState( 0 ) != Qt::Checked ) {
                unchecked.insert( ValueNamesCollection::ruleCheckKey(
                    groupId, ruleItem->data( 0, RuleNameRole ).toString() ) );
            }
        }
    }

    auto& collection = ValueNamesCollection::get();
    if ( !collection.setUncheckedKeys( unchecked ) ) {
        return;
    }
    collection.saveChecks();
    // The tree shows what the collection holds: refresh() leaves it alone.
    shownGeneration_ = collection.generation();
    Q_EMIT valueNamesChanged();
}
