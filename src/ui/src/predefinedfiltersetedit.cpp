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

#include "iconloader.h"
#include "log.h"
#include "theme.h"

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

        useWindowColorAsBase();
        Theme::whenApplied( this, [ this ] { useWindowColorAsBase(); } );
    }

    bool isChecked() const
    {
        return checkbox_->isChecked();
    }
    void setChecked( bool checked )
    {
        checkbox_->setChecked( checked );
    }
    QCheckBox* checkBox() const
    {
        return checkbox_;
    }

private:
    // The check box shows its cell's window color as its base. A role set
    // explicitly no longer follows the application palette, so it is set
    // again after every Theme switch.
    void useWindowColorAsBase()
    {
        QPalette checkBoxPalette;
        checkBoxPalette.setColor( QPalette::Base, palette().color( QPalette::Window ) );
        checkbox_->setPalette( checkBoxPalette );
    }

    QCheckBox* checkbox_;
};

} // namespace

PredefinedFilterSetEdit::PredefinedFilterSetEdit( QWidget* parent )
    : QWidget( parent )
{
    setupUi( this );
    editTriggers_ = filtersTableWidget->editTriggers();

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
    connect( testFilterButton, &QPushButton::clicked, this, &PredefinedFilterSetEdit::testFilter );

    loadIcons();
    Theme::whenApplied( this, [ this ] { loadIcons(); } );

    reset();
}

void PredefinedFilterSetEdit::loadIcons()
{
    loadListEditIcons( addFilterButton, removeFilterButton, upFilterButton, downFilterButton );
}

void PredefinedFilterSetEdit::reset()
{
    addFilterButton->setEnabled( false );
    removeFilterButton->setEnabled( false );
    upFilterButton->setEnabled( false );
    downFilterButton->setEnabled( false );

    testFilterButton->setEnabled( false );

    nameEdit->clear();
    nameEdit->setEnabled( false );
    filtersTableWidget->clearContents();
    filtersTableWidget->setRowCount( 0 );
}

void PredefinedFilterSetEdit::setRegexLabAccess( RegexLabAccess access )
{
    regexLabAccess_ = std::move( access );
}

void PredefinedFilterSetEdit::setReadOnly( bool readOnly )
{
    readOnly_ = readOnly;
    filtersTableWidget->setEditTriggers( readOnly_ ? QAbstractItemView::NoEditTriggers
                                                   : editTriggers_ );
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
    nameEdit->setReadOnly( readOnly_ );
    nameEdit->setText( filterSet_.name() );

    // Disable renaming the Default set.
    if ( filterSet_.id() == defaultFilterSetId() ) {
        nameEdit->setEnabled( false );
    }

    addFilterButton->setEnabled( !readOnly_ );
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
    filtersTableWidget->setHorizontalHeaderLabels( QStringList() << tr( "Name" ) << tr( "Pattern" )
                                                                 << tr( "Regex" ) );

    for ( int i = 0; i < filters.size(); ++i ) {
        filtersTableWidget->setItem( i, 0, new QTableWidgetItem( filters[ i ].name ) );
        filtersTableWidget->setItem( i, 1, new QTableWidgetItem( filters[ i ].pattern ) );
        auto* regexCheckbox = new CenteredCheckbox;
        regexCheckbox->setChecked( filters[ i ].useRegex );
        regexCheckbox->setEnabled( !readOnly_ );
        filtersTableWidget->setCellWidget( i, 2, regexCheckbox );
        keepRegexChoice( regexCheckbox->checkBox() );
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

    filterSet_.filters_ = std::move( filters );
}

// A filter's Regex box is no item of the table: checking it alone changes no
// cell, so it is kept as it is checked.
void PredefinedFilterSetEdit::keepRegexChoice( QCheckBox* regex )
{
    connect( regex, &QCheckBox::toggled, this, [ this ]() { onCellChanged( -1, 2 ); } );
}

void PredefinedFilterSetEdit::updateButtons( int currentRow )
{
    const int rowCount = filtersTableWidget->rowCount();
    removeFilterButton->setEnabled( !readOnly_ && currentRow >= 0 );
    // A Team group that cannot be changed is still tested, without Apply.
    testFilterButton->setEnabled( currentRow >= 0 && currentRow < rowCount );
    upFilterButton->setEnabled( !readOnly_ && currentRow > 0 );
    downFilterButton->setEnabled( !readOnly_ && currentRow >= 0 && currentRow < rowCount - 1 );
}

void PredefinedFilterSetEdit::addFilter()
{
    const int newRow = filtersTableWidget->rowCount();
    filtersTableWidget->setRowCount( newRow + 1 );
    filtersTableWidget->setItem( newRow, 0, new QTableWidgetItem( "" ) );
    filtersTableWidget->setItem( newRow, 1, new QTableWidgetItem( "" ) );
    auto* regexCheckbox = new CenteredCheckbox;
    filtersTableWidget->setCellWidget( newRow, 2, regexCheckbox );
    keepRegexChoice( regexCheckbox->checkBox() );

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

// Opens the Regex Lab over the editor, which waits for its answer, with the
// current filter's pattern read exactly as the Search Line of the tab in
// front reads it once the filter is used there (SearchLine::useFilters()):
// with the Search's engine and its Match case, as one pattern, no logical
// combination. A Search Line in regular expression mode reads a filter as
// the filter says; one that is not reads every filter as plain text. A filter
// keeps only whether it is a regular expression, and that only matters to a
// Search Line in regular expression mode; the rest is shown fixed.
void PredefinedFilterSetEdit::testFilter()
{
    using Option = RegexLabWindow::Option;

    const auto row = filtersTableWidget->currentRow();
    auto* patternItem = filtersTableWidget->item( row, 1 );
    if ( patternItem == nullptr ) {
        return;
    }
    const auto* regex = static_cast<CenteredCheckbox*>( filtersTableWidget->cellWidget( row, 2 ) );
    const auto isRegexp = regex != nullptr && regex->isChecked();
    const auto searchUsesRegexp = regexLabAccess_.searchUsesRegexp;

    auto* lab = new RegexLabWindow( regexLabAccess_.searchEngine, this );
    lab->setAttribute( Qt::WA_DeleteOnClose );
    lab->setWindowModality( Qt::WindowModal );
    lab->offerApply( !readOnly_ );
    if ( searchUsesRegexp ) {
        lab->setOptionsKept( Option::UseRegexp, Option::MatchCase );
    }
    else {
        lab->setOptionsKept( {}, Option::MatchCase | Option::UseRegexp );
    }
    lab->setPattern( RegularExpressionPattern( patternItem->text(),
                                               regexLabAccess_.searchMatchesCase, false, false,
                                               !( searchUsesRegexp && isRegexp ) ) );
    if ( regexLabAccess_.sampleSource ) {
        lab->setSampleSource( regexLabAccess_.sampleSource() );
    }
    connect( lab, &RegexLabWindow::applied, this,
             [ this, row, searchUsesRegexp ]( const RegularExpressionPattern& pattern ) {
                 applyTestedFilter( row, pattern, searchUsesRegexp );
             } );
    lab->show();
}

// Writes what the Lab applied into the filter's cells as if it was entered
// there, so that whatever takes in an edit takes it in.
void PredefinedFilterSetEdit::applyTestedFilter( int row, const RegularExpressionPattern& pattern,
                                                 bool isRegexpKept )
{
    auto* patternItem = filtersTableWidget->item( row, 1 );
    if ( patternItem == nullptr ) {
        return;
    }
    patternItem->setText( pattern.pattern );
    if ( !isRegexpKept ) {
        return;
    }
    if ( auto* regex = static_cast<CenteredCheckbox*>( filtersTableWidget->cellWidget( row, 2 ) );
         regex != nullptr ) {
        regex->setChecked( !pattern.isPlainText );
    }
}
