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

#include "predefinedfiltersetedit.h"

#include <QCheckBox>
#include <QHBoxLayout>

#include "dispatch_to.h"
#include "iconloader.h"
#include "log.h"

namespace {

// Centered checkbox widget reused from predefinedfiltersdialog.cpp pattern.
class CenteredCheckbox : public QWidget {
  public:
    explicit CenteredCheckbox( QWidget* parent = nullptr )
        : QWidget( parent )
    {
        auto* layout = new QHBoxLayout;
        layout->setAlignment( Qt::AlignCenter );
        checkbox_ = new QCheckBox;
        layout->addWidget( checkbox_ );
        this->setLayout( layout );

        QPalette pal = this->palette();
        pal.setColor( QPalette::Base, pal.color( QPalette::Window ) );
        checkbox_->setPalette( pal );
    }

    bool isChecked() const { return checkbox_->isChecked(); }
    void setChecked( bool checked ) { checkbox_->setChecked( checked ); }

  private:
    QCheckBox* checkbox_;
};

} // namespace

PredefinedFilterSetEdit::PredefinedFilterSetEdit( QWidget* parent )
    : QWidget( parent )
{
    setupUi( this );

    connect( nameEdit, &QLineEdit::textEdited, this, &PredefinedFilterSetEdit::setName );

    connect( addFilterButton, &QToolButton::clicked, this, &PredefinedFilterSetEdit::addFilter );
    connect( removeFilterButton, &QToolButton::clicked, this,
             &PredefinedFilterSetEdit::removeFilter );
    connect( upFilterButton, &QToolButton::clicked, this, &PredefinedFilterSetEdit::moveFilterUp );
    connect( downFilterButton, &QToolButton::clicked, this,
             &PredefinedFilterSetEdit::moveFilterDown );

    connect( filtersTableWidget, &QTableWidget::currentCellChanged, this,
             &PredefinedFilterSetEdit::onCurrentCellChanged );
    connect( filtersTableWidget, &QTableWidget::cellChanged, this,
             &PredefinedFilterSetEdit::onCellChanged );

    dispatchToMainThread( [ this ] {
        IconLoader iconLoader( this );
        addFilterButton->setIcon( iconLoader.load( "icons8-plus-16" ) );
        removeFilterButton->setIcon( iconLoader.load( "icons8-minus-16" ) );
        upFilterButton->setIcon( iconLoader.load( "icons8-up-16" ) );
        downFilterButton->setIcon( iconLoader.load( "icons8-down-arrow-16" ) );
    } );

    reset();
}

void PredefinedFilterSetEdit::reset()
{
    addFilterButton->setEnabled( false );
    removeFilterButton->setEnabled( false );
    upFilterButton->setEnabled( false );
    downFilterButton->setEnabled( false );

    nameEdit->clear();
    nameEdit->setEnabled( false );
    filtersTableWidget->clearContents();
    filtersTableWidget->setRowCount( 0 );
}

PredefinedFilterSet PredefinedFilterSetEdit::filterSet() const
{
    return filterSet_;
}

void PredefinedFilterSetEdit::setFilterSet( PredefinedFilterSet set )
{
    filterSet_ = std::move( set );
    populateTable();

    nameEdit->setEnabled( true );
    nameEdit->setText( filterSet_.name() );

    // Disable renaming the Default set.
    if ( filterSet_.id() == defaultFilterSetId() ) {
        nameEdit->setEnabled( false );
    }

    addFilterButton->setEnabled( true );
}

void PredefinedFilterSetEdit::setName( const QString& name )
{
    filterSet_.name_ = name;
    Q_EMIT changed();
}

void PredefinedFilterSetEdit::populateTable()
{
    updatingTable_ = true;

    filtersTableWidget->clearContents();
    const auto& filters = filterSet_.filters_;
    filtersTableWidget->setRowCount( static_cast<int>( filters.size() ) );
    filtersTableWidget->setColumnCount( 3 );
    filtersTableWidget->setHorizontalHeaderLabels(
        QStringList() << tr( "Name" ) << tr( "Pattern" ) << tr( "Regex" ) );

    for ( int i = 0; i < filters.size(); ++i ) {
        filtersTableWidget->setItem( i, 0, new QTableWidgetItem( filters[ i ].name ) );
        filtersTableWidget->setItem( i, 1, new QTableWidgetItem( filters[ i ].pattern ) );
        auto* regexCheckbox = new CenteredCheckbox;
        regexCheckbox->setChecked( filters[ i ].useRegex );
        filtersTableWidget->setCellWidget( i, 2, regexCheckbox );
    }

    filtersTableWidget->horizontalHeader()->setSectionResizeMode( 0,
                                                                  QHeaderView::ResizeToContents );
    filtersTableWidget->horizontalHeader()->setSectionResizeMode( 1, QHeaderView::Stretch );
    filtersTableWidget->verticalHeader()->setSectionResizeMode( QHeaderView::ResizeToContents );
    filtersTableWidget->setWordWrap( false );

    updatingTable_ = false;

    updateButtons( filtersTableWidget->currentRow() );
}

void PredefinedFilterSetEdit::syncTableToSet()
{
    QList<PredefinedFilter> filters;
    const int rows = filtersTableWidget->rowCount();
    filters.reserve( rows );

    for ( int i = 0; i < rows; ++i ) {
        auto* nameItem = filtersTableWidget->item( i, 0 );
        auto* patternItem = filtersTableWidget->item( i, 1 );
        if ( !nameItem || !patternItem ) {
            continue;
        }
        auto* regexWidget
            = static_cast<CenteredCheckbox*>( filtersTableWidget->cellWidget( i, 2 ) );
        const bool useRegex = regexWidget ? regexWidget->isChecked() : false;

        filters.append( { nameItem->text(), patternItem->text(), useRegex } );
    }

    filterSet_.filters_ = filters;
}

void PredefinedFilterSetEdit::updateButtons( int currentRow )
{
    const int rowCount = filtersTableWidget->rowCount();
    removeFilterButton->setEnabled( currentRow >= 0 );
    upFilterButton->setEnabled( currentRow > 0 );
    downFilterButton->setEnabled( currentRow >= 0 && currentRow < rowCount - 1 );
}

void PredefinedFilterSetEdit::addFilter()
{
    const int newRow = filtersTableWidget->rowCount();
    filtersTableWidget->setRowCount( newRow + 1 );
    filtersTableWidget->setItem( newRow, 0, new QTableWidgetItem( "" ) );
    filtersTableWidget->setItem( newRow, 1, new QTableWidgetItem( "" ) );
    auto* regexCheckbox = new CenteredCheckbox;
    filtersTableWidget->setCellWidget( newRow, 2, regexCheckbox );

    filtersTableWidget->scrollToItem( filtersTableWidget->item( newRow, 0 ) );
    filtersTableWidget->setCurrentCell( newRow, 0 );
    filtersTableWidget->editItem( filtersTableWidget->item( newRow, 0 ) );

    syncTableToSet();
    Q_EMIT changed();
}

void PredefinedFilterSetEdit::removeFilter()
{
    const int row = filtersTableWidget->currentRow();
    if ( row < 0 ) {
        return;
    }

    filtersTableWidget->removeRow( row );
    syncTableToSet();
    updateButtons( filtersTableWidget->currentRow() );
    Q_EMIT changed();
}

void PredefinedFilterSetEdit::moveFilterUp()
{
    const int row = filtersTableWidget->currentRow();
    if ( row <= 0 ) {
        return;
    }

    syncTableToSet();
    filterSet_.filters_.move( row, row - 1 );
    populateTable();
    filtersTableWidget->setCurrentCell( row - 1, 0 );
    Q_EMIT changed();
}

void PredefinedFilterSetEdit::moveFilterDown()
{
    const int row = filtersTableWidget->currentRow();
    if ( row < 0 || row >= filtersTableWidget->rowCount() - 1 ) {
        return;
    }

    syncTableToSet();
    filterSet_.filters_.move( row, row + 1 );
    populateTable();
    filtersTableWidget->setCurrentCell( row + 1, 0 );
    Q_EMIT changed();
}

void PredefinedFilterSetEdit::onCurrentCellChanged( int currentRow, int /*currentColumn*/,
                                                    int /*previousRow*/, int /*previousColumn*/ )
{
    updateButtons( currentRow );
}

void PredefinedFilterSetEdit::onCellChanged( int /*row*/, int /*column*/ )
{
    if ( updatingTable_ ) {
        return;
    }

    syncTableToSet();
    Q_EMIT changed();
}
