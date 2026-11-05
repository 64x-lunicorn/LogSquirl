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

#include "addpredefinedfilterdialog.h"

#include <algorithm>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

QString AddPredefinedFilterDialog::proposedName( const QString& selectedText )
{
    auto name = selectedText.simplified();
    if ( name.size() > MaxProposedNameLength ) {
        name = name.left( MaxProposedNameLength - 3 ) + QStringLiteral( "..." );
    }
    return name;
}

AddPredefinedFilterDialog::AddPredefinedFilterDialog( const QString& selectedText, QWidget* parent )
    : QDialog( parent )
    , groups_( PredefinedFiltersCollection::getSynced().filterSets() )
{
    setWindowTitle( tr( "Add as Predefined Filter" ) );

    auto* form = new QFormLayout;

    name_ = new QLineEdit( proposedName( selectedText ), this );
    name_->setObjectName( QStringLiteral( "filterName" ) );
    name_->setToolTip( tr( "The name the Filters panel lists the Predefined Filter by" ) );
    form->addRow( tr( "Name:" ), name_ );

    pattern_ = new QLineEdit( QRegularExpression::escape( selectedText ), this );
    pattern_->setObjectName( QStringLiteral( "filterPattern" ) );
    pattern_->setToolTip( tr( "The regex of the Predefined Filter; proposed from the selection, "
                              "it matches the selected text as it is" ) );
    form->addRow( tr( "Pattern:" ), pattern_ );

    // The user's own groups, the Default Filter Group chosen. The Team's and
    // the plugins' groups are not the user's to add to.
    group_ = new QComboBox( this );
    group_->setObjectName( QStringLiteral( "filterGroup" ) );
    int chosen = 0;
    for ( int row = 0; row < groups_.size(); ++row ) {
        group_->addItem( groups_[ row ].name(), groups_[ row ].id() );
        if ( groups_[ row ].id() == defaultFilterSetId() ) {
            chosen = row;
        }
    }
    group_->setCurrentIndex( chosen );
    form->addRow( tr( "Filter Group:" ), group_ );

    applyNow_ = new QCheckBox( tr( "Search with it now" ), this );
    applyNow_->setObjectName( QStringLiteral( "applyNow" ) );
    applyNow_->setToolTip( tr( "Replace the current search with the Predefined Filter" ) );
    form->addRow( QString{}, applyNow_ );

    warning_ = new QLabel( this );
    warning_->setObjectName( QStringLiteral( "warning" ) );
    warning_->setWordWrap( true );

    buttons_ = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this );
    auto* layout = new QVBoxLayout( this );
    layout->addLayout( form );
    layout->addWidget( warning_ );
    layout->addWidget( buttons_ );

    connect( name_, &QLineEdit::textChanged, this, &AddPredefinedFilterDialog::updateState );
    connect( pattern_, &QLineEdit::textChanged, this, &AddPredefinedFilterDialog::updateState );
    connect( buttons_, &QDialogButtonBox::accepted, this, &AddPredefinedFilterDialog::accept );
    connect( buttons_, &QDialogButtonBox::rejected, this, &AddPredefinedFilterDialog::reject );
    updateState();

    name_->setFocus();
    name_->selectAll();
    resize( 480, sizeHint().height() );
}

PredefinedFilter AddPredefinedFilterDialog::filter() const
{
    return PredefinedFilter{ name_->text().trimmed(), pattern_->text(), true };
}

QString AddPredefinedFilterDialog::chosenGroupId() const
{
    return group_->currentData().toString();
}

bool AddPredefinedFilterDialog::applyNow() const
{
    return applyNow_->isChecked();
}

QString AddPredefinedFilterDialog::warning() const
{
    return warning_->text();
}

void AddPredefinedFilterDialog::updateState()
{
    const QRegularExpression regex( pattern_->text() );
    QString warning;
    if ( pattern_->text().isEmpty() ) {
        warning = tr( "The pattern is empty." );
    }
    else if ( !regex.isValid() ) {
        warning = tr( "The regex is not valid: %1" ).arg( regex.errorString() );
    }
    warning_->setText( warning );
    warning_->setVisible( !warning.isEmpty() );
    buttons_->button( QDialogButtonBox::Ok )
        ->setEnabled( warning.isEmpty() && !name_->text().trimmed().isEmpty() );
}

void AddPredefinedFilterDialog::accept()
{
    auto& collection = PredefinedFiltersCollection::get();
    // Setting the groups makes sure the Default Filter Group is among them.
    collection.setFilterSets( collection.filterSets() );
    auto groups = collection.filterSets();

    // A group the user chose may have gone meanwhile: the filter is not lost,
    // it goes into the Default Filter Group.
    const auto byId = []( const QString& id ) {
        return [ id ]( const PredefinedFilterSet& group ) { return group.id() == id; };
    };
    auto chosen = std::find_if( groups.begin(), groups.end(), byId( chosenGroupId() ) );
    if ( chosen == groups.end() ) {
        chosen = std::find_if( groups.begin(), groups.end(), byId( defaultFilterSetId() ) );
    }
    if ( chosen != groups.end() ) {
        chosen->addFilter( filter() );
    }

    collection.setFilterSets( groups );
    collection.save();
    QDialog::accept();
}
