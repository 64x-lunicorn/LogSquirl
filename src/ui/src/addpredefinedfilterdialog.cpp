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

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QVBoxLayout>

#include "groupedits.h"
#include "predefinedfilters.h"
#include "valueproposal.h"

QString AddPredefinedFilterDialog::proposedName( const QString& selectedText )
{
    auto name = selectedText.simplified();
    if ( name.size() > MaxProposedNameLength ) {
        name = name.left( MaxProposedNameLength - 3 ) + QStringLiteral( "..." );
    }
    return name;
}

namespace {

// The names of a Filter Group's filters, held by its entry of the combo box.
constexpr int FilterNamesRole = Qt::UserRole + 1;

QStringList filterNames( const PredefinedFilterSet& group )
{
    QStringList names;
    for ( const auto& filter : group.filters() ) {
        names.append( filter.name );
    }
    return names;
}

} // namespace

AddPredefinedFilterDialog::AddPredefinedFilterDialog( const QString& selectedText, QWidget* parent )
    : QDialog( parent )
{
    setWindowTitle( tr( "Add as Predefined Filter" ) );

    auto* form = new QFormLayout;

    name_ = new QLineEdit( this );
    name_->setObjectName( QStringLiteral( "filterName" ) );
    name_->setToolTip( tr( "The name the Filters panel lists the Predefined Filter by" ) );
    form->addRow( tr( "Name:" ), name_ );

    pattern_ = new QLineEdit( selectedText, this );
    pattern_->setObjectName( QStringLiteral( "filterPattern" ) );
    pattern_->setToolTip( tr( "What the Predefined Filter searches for; proposed from the "
                              "selection, it finds the selected text as it is" ) );
    form->addRow( tr( "Pattern:" ), pattern_ );

    regex_ = new QCheckBox( tr( "Regular expression" ), this );
    regex_->setObjectName( QStringLiteral( "filterRegex" ) );
    regex_->setToolTip( tr( "Read the pattern as a regular expression; checking it escapes the "
                            "pattern so that it still finds the same text" ) );
    form->addRow( QString{}, regex_ );

    // The user's own groups, the Default Filter Group chosen. The Team's and
    // the plugins' groups are not the user's to add to.
    group_ = new QComboBox( this );
    group_->setObjectName( QStringLiteral( "filterGroup" ) );
    const auto groups = PredefinedFiltersCollection::getSynced().filterSets();
    int chosen = 0;
    for ( int row = 0; row < groups.size(); ++row ) {
        group_->addItem( groups[ row ].name(), groups[ row ].id() );
        group_->setItemData( row, filterNames( groups[ row ] ), FilterNamesRole );
        if ( groups[ row ].id() == defaultFilterSetId() ) {
            chosen = row;
        }
    }
    group_->setCurrentIndex( chosen );
    form->addRow( tr( "Filter Group:" ), group_ );

    // A name no filter of the group has: the Filters panel tells the filters
    // of a group apart by their names.
    name_->setText(
        logsquirl::valuenames::freeName( proposedName( selectedText ), chosenGroupNames() ) );

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
    connect( regex_, &QCheckBox::toggled, this, &AddPredefinedFilterDialog::regexToggled );
    connect( group_, &QComboBox::currentIndexChanged, this,
             &AddPredefinedFilterDialog::updateState );
    connect( buttons_, &QDialogButtonBox::accepted, this, &AddPredefinedFilterDialog::accept );
    connect( buttons_, &QDialogButtonBox::rejected, this, &AddPredefinedFilterDialog::reject );
    updateState();

    name_->setFocus();
    name_->selectAll();
    resize( 480, sizeHint().height() );
}

PredefinedFilter AddPredefinedFilterDialog::filter() const
{
    return PredefinedFilter{ name_->text().trimmed(), pattern_->text(), regex_->isChecked() };
}

QString AddPredefinedFilterDialog::chosenGroupId() const
{
    return group_->currentData().toString();
}

QStringList AddPredefinedFilterDialog::chosenGroupNames() const
{
    return group_->currentData( FilterNamesRole ).toStringList();
}

bool AddPredefinedFilterDialog::applyNow() const
{
    return applyNow_->isChecked();
}

QString AddPredefinedFilterDialog::warning() const
{
    return warning_->text();
}

void AddPredefinedFilterDialog::regexToggled( bool regex )
{
    if ( regex ) {
        plainPattern_ = pattern_->text();
        pattern_->setText( logsquirl::valuenames::escapedForRegex( plainPattern_ ) );
    }
    // Unless the regex was edited, it was plain text before.
    else if ( pattern_->text() == logsquirl::valuenames::escapedForRegex( plainPattern_ ) ) {
        pattern_->setText( plainPattern_ );
    }
    updateState();
}

void AddPredefinedFilterDialog::updateState()
{
    const auto name = name_->text().trimmed();
    QString warning;
    if ( pattern_->text().isEmpty() ) {
        warning = tr( "The pattern is empty." );
    }
    else if ( const QRegularExpression regex( pattern_->text() );
              regex_->isChecked() && !regex.isValid() ) {
        warning = tr( "The regex is not valid: %1" ).arg( regex.errorString() );
    }
    else if ( chosenGroupNames().contains( name ) ) {
        warning = tr( "The Filter Group already has a filter named \"%1\"." ).arg( name );
    }
    warning_->setText( warning );
    warning_->setVisible( !warning.isEmpty() );
    buttons_->button( QDialogButtonBox::Ok )->setEnabled( warning.isEmpty() && !name.isEmpty() );
}

void AddPredefinedFilterDialog::accept()
{
    auto& collection = PredefinedFiltersCollection::get();
    collection.addFilter( chosenGroupId(), filter() );
    collection.save();
    QDialog::accept();
}
