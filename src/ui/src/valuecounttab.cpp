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

#include "valuecounttab.h"

#include <QAction>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QTableView>
#include <QToolBar>
#include <QVBoxLayout>

#include "abstractlogdata.h"
#include "valuecountmodel.h"

ValueCountTab::ValueCountTab( std::shared_ptr<const AbstractLogData> logData, QString description,
                              ValueOfLineFactory valueOfLine, QWidget* parent )
    : QWidget( parent )
    , logData_( std::move( logData ) )
    , description_( std::move( description ) )
    , valueOfLine_( std::move( valueOfLine ) )
{
    auto* layout = new QVBoxLayout( this );
    layout->setContentsMargins( 0, 0, 0, 0 );
    layout->setSpacing( 0 );

    auto* toolBar = new QToolBar;
    toolBar->setIconSize( QSize( 16, 16 ) );
    stopAction_ = toolBar->addAction( tr( "Stop" ) );
    stopAction_->setToolTip( tr( "Stop counting" ) );
    connect( stopAction_, &QAction::triggered, this, &ValueCountTab::stop );
    countAgainAction_ = toolBar->addAction( tr( "Count again" ) );
    countAgainAction_->setToolTip( tr( "Count the values again, from the Log File as it is now" ) );
    connect( countAgainAction_, &QAction::triggered, this, &ValueCountTab::countAgain );
    toolBar->addSeparator();
    statusLabel_ = new QLabel;
    statusLabel_->setContentsMargins( 4, 0, 4, 0 );
    toolBar->addWidget( statusLabel_ );
    layout->addWidget( toolBar );

    progressBar_ = new QProgressBar;
    progressBar_->setRange( 0, 100 );
    progressBar_->setMaximumHeight( 16 );
    progressBar_->setVisible( false );
    layout->addWidget( progressBar_ );

    model_ = new ValueCountModel( this );
    table_ = new QTableView;
    table_->setModel( model_ );
    table_->horizontalHeader()->setStretchLastSection( false );
    table_->horizontalHeader()->setSectionResizeMode( ValueCountModel::ValueColumn,
                                                      QHeaderView::Stretch );
    table_->verticalHeader()->setVisible( false );
    table_->verticalHeader()->setSectionResizeMode( QHeaderView::Fixed );
    table_->setEditTriggers( QAbstractItemView::NoEditTriggers );
    table_->setSelectionBehavior( QAbstractItemView::SelectRows );
    table_->setSelectionMode( QAbstractItemView::SingleSelection );
    connect( table_, &QTableView::clicked, this, [ this ]( const QModelIndex& index ) {
        const auto value = model_->valueAt( index.row() );
        // The value of a row that stands for no text, or for a Log Line where
        // the field is empty, is not something to search for.
        if ( !value.isEmpty() ) {
            Q_EMIT valueClicked( value );
        }
    } );
    layout->addWidget( table_, 1 );

    connect( &counter_, &ValueCounter::finished, this, &ValueCountTab::onFinished );

    progressTimer_.setInterval( 100 );
    connect( &progressTimer_, &QTimer::timeout, this, &ValueCountTab::showProgress );

    countAgain();
}

ValueCountTab::~ValueCountTab()
{
    // Stops the worker, which does not outlive the log data it reads.
    counter_.cancel();
}

const QString& ValueCountTab::description() const
{
    return description_;
}

bool ValueCountTab::isCounting() const
{
    return counter_.isRunning();
}

void ValueCountTab::countAgain()
{
    // A snapshot: nothing of the count before stays on show.
    model_->clear();
    showStatus( tr( "Counting values of %1…" ).arg( description_ ) );
    progressBar_->setValue( 0 );
    progressBar_->setVisible( true );
    stopAction_->setEnabled( true );
    countAgainAction_->setEnabled( false );
    progressTimer_.start();

    counter_.start( logData_, valueOfLine_() );
}

void ValueCountTab::stop()
{
    if ( !counter_.isRunning() ) {
        return;
    }
    counter_.cancel();
    progressTimer_.stop();
    progressBar_->setVisible( false );
    stopAction_->setEnabled( false );
    countAgainAction_->setEnabled( true );
    // A stopped count shows nothing partial.
    showStatus( tr( "Stopped: values of %1 not counted" ).arg( description_ ) );
}

void ValueCountTab::showProgress()
{
    progressBar_->setValue( counter_.progress() );
}

void ValueCountTab::showStatus( const QString& text )
{
    statusLabel_->setText( text );
}

void ValueCountTab::onFinished( const ValueCountResult& result )
{
    progressTimer_.stop();
    progressBar_->setVisible( false );
    stopAction_->setEnabled( false );
    countAgainAction_->setEnabled( true );

    if ( result.tooManyDistinctValues ) {
        showStatus( tr( "Too many distinct values of %1 (more than %2) to count" )
                        .arg( description_ )
                        .arg( MaxDistinctValues ) );
        return;
    }

    showStatus( tr( "%1: %n Log Line(s) counted", "", static_cast<int>( result.linesCounted ) )
                    .arg( description_ ) );

    model_->setResult( result );
}
