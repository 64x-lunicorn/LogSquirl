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

#include "groupchooser.h"

#include <QComboBox>
#include <QFont>
#include <QLabel>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QStandardItemModel>
#include <QVBoxLayout>

#include <utility>

#include "groupedits.h"

namespace {

// What an entry of the combo box is.
enum class Entry { Heading, Group, New };

constexpr int EntryRole = Qt::UserRole + 1;
constexpr int SectionRole = Qt::UserRole + 2;
constexpr int IdRole = Qt::UserRole + 3;
constexpr int ItemNamesRole = Qt::UserRole + 4;

} // namespace

GroupChooser::GroupChooser( Texts texts, QWidget* parent )
    : QWidget( parent )
    , texts_( std::move( texts ) )
{
    combo_ = new QComboBox( this );
    combo_->setObjectName( QStringLiteral( "groupChoice" ) );
    combo_->setSizeAdjustPolicy( QComboBox::AdjustToContents );

    name_ = new QLineEdit( this );
    name_->setObjectName( QStringLiteral( "newGroupName" ) );
    name_->setPlaceholderText( texts_.newGroupName );
    name_->setAccessibleName( texts_.newGroupName );
    name_->hide();

    hint_ = new QLabel( texts_.teamHint, this );
    hint_->setObjectName( QStringLiteral( "teamHint" ) );
    hint_->setWordWrap( true );
    hint_->hide();

    auto* layout = new QVBoxLayout( this );
    layout->setContentsMargins( 0, 0, 0, 0 );
    layout->addWidget( combo_ );
    layout->addWidget( name_ );
    layout->addWidget( hint_ );

    connect( combo_, &QComboBox::currentIndexChanged, this, &GroupChooser::currentChanged );
    connect( name_, &QLineEdit::textChanged, this, &GroupChooser::choiceChanged );
}

void GroupChooser::setGroups( const QList<Group>& own, const std::optional<TeamGroups>& team )
{
    own_ = own;
    team_ = team;
    {
        const QSignalBlocker blocker( combo_ );
        combo_->clear();
        addHeading( texts_.ownSection );
        for ( const auto& group : own_ ) {
            addGroup( Section::Own, group, true );
        }
        if ( team_ ) {
            addHeading( texts_.teamSection );
            for ( const auto& group : team_->groups ) {
                addGroup( Section::Team, group, team_->editable );
            }
        }
        combo_->insertSeparator( combo_->count() );
        addNewEntry( Section::Own, texts_.newGroup );
        if ( team_ && team_->editable ) {
            addNewEntry( Section::Team, texts_.newTeamGroup );
        }
        combo_->setCurrentIndex( -1 );
    }
    choose( {}, {} );
}

void GroupChooser::choose( const QString& id, const QString& fallbackId )
{
    auto row = rowOf( id );
    if ( row < 0 ) {
        row = rowOf( fallbackId );
    }
    if ( row < 0 && !own_.isEmpty() ) {
        row = rowOf( own_.front().id );
    }
    if ( row < 0 ) {
        // No group of the user's own: a new one is all there is to choose.
        row = combo_->findData( QVariant::fromValue( static_cast<int>( Entry::New ) ), EntryRole );
    }
    if ( row == combo_->currentIndex() ) {
        currentChanged();
    }
    else {
        combo_->setCurrentIndex( row );
    }
}

GroupChooser::Choice GroupChooser::choice() const
{
    Choice chosen;
    const auto row = combo_->currentIndex();
    if ( row < 0 ) {
        return chosen;
    }
    chosen.section = static_cast<Section>( combo_->itemData( row, SectionRole ).toInt() );
    if ( static_cast<Entry>( combo_->itemData( row, EntryRole ).toInt() ) == Entry::New ) {
        chosen.name = name_->text().trimmed();
    }
    else {
        chosen.id = combo_->itemData( row, IdRole ).toString();
        chosen.name = combo_->itemText( row );
    }
    return chosen;
}

QStringList GroupChooser::itemNames() const
{
    return combo_->currentData( ItemNamesRole ).toStringList();
}

QString GroupChooser::warning() const
{
    const auto chosen = choice();
    if ( !chosen.isNew() ) {
        return {};
    }
    if ( chosen.name.isEmpty() ) {
        return texts_.nameEmpty;
    }
    if ( namesOf( chosen.section ).contains( chosen.name ) ) {
        return texts_.nameTaken.arg( chosen.name );
    }
    return {};
}

bool GroupChooser::hasTeamSection() const
{
    return team_.has_value();
}

void GroupChooser::addHeading( const QString& text )
{
    combo_->addItem( text );
    const auto row = combo_->count() - 1;
    combo_->setItemData( row, static_cast<int>( Entry::Heading ), EntryRole );
    QFont bold = combo_->font();
    bold.setBold( true );
    combo_->setItemData( row, bold, Qt::FontRole );
    // A heading is shown, not chosen.
    if ( auto* model = qobject_cast<QStandardItemModel*>( combo_->model() ) ) {
        model->item( row )->setFlags( Qt::NoItemFlags );
    }
}

void GroupChooser::addGroup( Section section, const Group& group, bool enabled )
{
    combo_->addItem( group.name );
    const auto row = combo_->count() - 1;
    combo_->setItemData( row, static_cast<int>( Entry::Group ), EntryRole );
    combo_->setItemData( row, static_cast<int>( section ), SectionRole );
    combo_->setItemData( row, group.id, IdRole );
    combo_->setItemData( row, group.itemNames, ItemNamesRole );
    combo_->setItemData( row, group.itemNames.join( u'\n' ), Qt::ToolTipRole );
    if ( !enabled ) {
        if ( auto* model = qobject_cast<QStandardItemModel*>( combo_->model() ) ) {
            model->item( row )->setFlags( Qt::NoItemFlags );
        }
    }
}

void GroupChooser::addNewEntry( Section section, const QString& text )
{
    combo_->addItem( text );
    const auto row = combo_->count() - 1;
    combo_->setItemData( row, static_cast<int>( Entry::New ), EntryRole );
    combo_->setItemData( row, static_cast<int>( section ), SectionRole );
}

int GroupChooser::rowOf( const QString& id ) const
{
    if ( id.isEmpty() ) {
        return -1;
    }
    for ( int row = 0; row < combo_->count(); ++row ) {
        if ( static_cast<Entry>( combo_->itemData( row, EntryRole ).toInt() ) != Entry::Group
             || combo_->itemData( row, IdRole ).toString() != id ) {
            continue;
        }
        // A Team group that cannot be changed cannot be chosen either.
        const auto* model = qobject_cast<const QStandardItemModel*>( combo_->model() );
        if ( model == nullptr || model->item( row )->isEnabled() ) {
            return row;
        }
    }
    return -1;
}

QStringList GroupChooser::namesOf( Section section ) const
{
    QStringList names;
    const auto& groups = section == Section::Own ? own_ : team_->groups;
    for ( const auto& group : groups ) {
        names.append( group.name );
    }
    return names;
}

void GroupChooser::currentChanged()
{
    const auto chosen = choice();
    const auto row = combo_->currentIndex();
    const bool isNew
        = row >= 0
          && static_cast<Entry>( combo_->itemData( row, EntryRole ).toInt() ) == Entry::New;
    if ( isNew ) {
        // Proposed afresh each time: the names taken differ by section.
        const QSignalBlocker blocker( name_ );
        name_->setText(
            logsquirl::valuenames::freeName( texts_.newGroupName, namesOf( chosen.section ) ) );
    }
    name_->setVisible( isNew );
    hint_->setVisible( row >= 0 && chosen.section == Section::Team );
    combo_->setToolTip( combo_->currentData( Qt::ToolTipRole ).toString() );
    if ( isNew ) {
        name_->setFocus();
        name_->selectAll();
    }
    Q_EMIT choiceChanged();
}
