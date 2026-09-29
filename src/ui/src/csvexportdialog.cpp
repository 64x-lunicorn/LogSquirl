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

#include "csvexportdialog.h"

#include <utility>

#include <QButtonGroup>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QListWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

#include "configuration.h"

CsvExportDialog::CsvExportDialog( Setup setup, QWidget* parent )
    : QDialog( parent )
    , setup_( std::move( setup ) )
    , chooseFileName_( []( QWidget* dialogParent, const QString& proposed ) {
        return QFileDialog::getSaveFileName( dialogParent, tr( "Export as CSV" ), proposed,
                                             tr( "CSV files (*.csv)" ) );
    } )
{
    setWindowTitle( tr( "Export as CSV" ) );

    auto* rowsBox = new QGroupBox( tr( "Rows" ) );
    auto* rowsLayout = new QVBoxLayout( rowsBox );
    allRows_
        = new QRadioButton( setup_.allRowsText.isEmpty() ? tr( "All rows" ) : setup_.allRowsText );
    selectedRows_ = new QRadioButton( setup_.selectedRowsText.isEmpty() ? tr( "Selected rows" )
                                                                        : setup_.selectedRowsText );
    selectedRows_->setEnabled( setup_.hasSelection );
    ( setup_.hasSelection ? selectedRows_ : allRows_ )->setChecked( true );
    rowsLayout->addWidget( allRows_ );
    rowsLayout->addWidget( selectedRows_ );
    for ( const auto& option : setup_.rowOptions ) {
        auto* checkBox = new QCheckBox( option.text );
        checkBox->setChecked( option.checked );
        checkBox->setEnabled( option.enabled );
        rowsLayout->addWidget( checkBox );
        rowOptions_.push_back( checkBox );
    }

    auto* columnsBox = new QGroupBox( tr( "Columns" ) );
    auto* columnsLayout = new QVBoxLayout( columnsBox );
    columns_ = new QListWidget;
    for ( const auto& column : setup_.columns ) {
        auto* item = new QListWidgetItem( column.name, columns_ );
        item->setFlags( Qt::ItemIsEnabled | Qt::ItemIsUserCheckable );
        item->setCheckState( column.checked ? Qt::Checked : Qt::Unchecked );
    }
    columnsLayout->addWidget( columns_ );

    const auto& config = Configuration::get();

    auto* separatorBox = new QGroupBox( tr( "Separator" ) );
    auto* separatorLayout = new QHBoxLayout( separatorBox );
    comma_ = new QRadioButton( tr( "Comma" ) );
    semicolon_ = new QRadioButton( tr( "Semicolon" ) );
    tab_ = new QRadioButton( tr( "Tab" ) );
    auto* separators = new QButtonGroup( this );
    for ( auto* button : { comma_, semicolon_, tab_ } ) {
        separators->addButton( button );
        separatorLayout->addWidget( button );
    }
    separatorLayout->addStretch();
    const auto separator = config.csvSeparator();
    ( separator == QLatin1Char( ';' )    ? semicolon_
      : separator == QLatin1Char( '\t' ) ? tab_
                                         : comma_ )
        ->setChecked( true );

    header_ = new QCheckBox( tr( "Write column names as the first row" ) );
    header_->setChecked( config.csvHeader() );

    auto* buttons = new QDialogButtonBox;
    export_ = buttons->addButton( tr( "Export..." ), QDialogButtonBox::AcceptRole );
    buttons->addButton( QDialogButtonBox::Cancel );
    connect( export_, &QPushButton::clicked, this, [ this ]() { exportChosen(); } );
    connect( buttons, &QDialogButtonBox::rejected, this, &QDialog::reject );

    auto* layout = new QVBoxLayout( this );
    layout->addWidget( rowsBox );
    layout->addWidget( columnsBox );
    layout->addWidget( separatorBox );
    layout->addWidget( header_ );
    layout->addWidget( buttons );

    connect( columns_, &QListWidget::itemChanged, this, [ this ]() { updateExportButton(); } );
    updateExportButton();
}

void CsvExportDialog::setFileNameChooser( FileNameChooser chooser )
{
    chooseFileName_ = std::move( chooser );
}

CsvExportDialog::Choices CsvExportDialog::choices() const
{
    Choices chosen;
    chosen.selectedRowsOnly = selectedRows_->isChecked();
    for ( const auto* option : rowOptions_ ) {
        chosen.rowOptions.push_back( option->isChecked() );
    }
    for ( int row = 0; row < columns_->count(); ++row ) {
        if ( columns_->item( row )->checkState() == Qt::Checked ) {
            chosen.columns.push_back( static_cast<size_t>( row ) );
        }
    }
    chosen.separator = semicolon_->isChecked() ? QLatin1Char( ';' )
                       : tab_->isChecked()     ? QLatin1Char( '\t' )
                                               : QLatin1Char( ',' );
    chosen.header = header_->isChecked();
    chosen.fileName = fileName_;
    return chosen;
}

void CsvExportDialog::updateExportButton()
{
    bool anyChecked = false;
    for ( int row = 0; row < columns_->count(); ++row ) {
        anyChecked = anyChecked || columns_->item( row )->checkState() == Qt::Checked;
    }
    export_->setEnabled( anyChecked );
}

void CsvExportDialog::exportChosen()
{
    auto fileName = chooseFileName_( this, setup_.proposedFileName );
    if ( fileName.isEmpty() ) {
        return;
    }
    if ( !fileName.endsWith( QLatin1String( ".csv" ), Qt::CaseInsensitive ) ) {
        fileName += QLatin1String( ".csv" );
    }
    fileName_ = std::move( fileName );

    const auto chosen = choices();
    auto& config = Configuration::getSynced();
    config.setCsvSeparator( chosen.separator );
    config.setCsvHeader( chosen.header );
    config.save();

    accept();
}

std::optional<CsvExportDialog::Choices> CsvExportDialog::ask( QWidget* parent, Setup setup )
{
    CsvExportDialog dialog( std::move( setup ), parent );
    if ( dialog.exec() != QDialog::Accepted ) {
        return std::nullopt;
    }
    return dialog.choices();
}
