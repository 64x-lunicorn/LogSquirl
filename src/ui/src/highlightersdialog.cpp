/*
 * Copyright (C) 2009, 2010 Nicolas Bonnefon and other contributors
 *
 * This file is part of glogg.
 *
 * glogg is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * glogg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with glogg.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * Copyright (C) 2019 Anton Filimonov and other contributors
 *
 * This file is part of logsquirl.
 *
 * logsquirl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * logsquirl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with logsquirl.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <QDir>
#include <QFileDialog>
#include <QMessageBox>
#include <QTimer>

#include <qcheckbox.h>
#include <qcolor.h>
#include <qlineedit.h>
#include <qnamespace.h>
#include <qpushbutton.h>
#include <utility>

#include "containers.h"
#include "dispatch_to.h"
#include "groupexchange.h"
#include "groupimportprompt.h"
#include "highlightersdialog.h"
#include "highlighterset.h"
#include "iconloader.h"
#include "log.h"
#include "theme.h"

static constexpr QLatin1String DEFAULT_NAME = QLatin1String( "New Highlighter set", 19 );

// Construct the box, including a copy of the global highlighterSet
// to handle ok/cancel/apply
HighlightersDialog::HighlightersDialog( QWidget* parent )
    : QDialog( parent )
{
    setupUi( this );

    highlighterSetEdit_ = new HighlighterSetEdit( this );
    highlighterSetEdit_->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );
    highlighterLayout->addWidget( highlighterSetEdit_ );

    splitter->setStretchFactor( 0, 0 );
    splitter->setStretchFactor( 1, 1 );

    // Reload the highlighter list from disk (in case it has been changed
    // by another glogg instance) and copy it to here.
    highlighterSetCollection_ = HighlighterSetCollection::getSynced();

    populateHighlighterList();

    // Start with all buttons disabled except 'add'
    removeHighlighterButton->setEnabled( false );
    upHighlighterButton->setEnabled( false );
    downHighlighterButton->setEnabled( false );
    exportButton->setEnabled( false );

    connect( addHighlighterButton, &QToolButton::clicked, this,
             &HighlightersDialog::addHighlighterSet );
    connect( removeHighlighterButton, &QToolButton::clicked, this,
             &HighlightersDialog::removeHighlighterSet );

    connect( upHighlighterButton, &QToolButton::clicked, this,
             &HighlightersDialog::moveHighlighterSetUp );
    connect( downHighlighterButton, &QToolButton::clicked, this,
             &HighlightersDialog::moveHighlighterSetDown );

    connect( exportButton, &QPushButton::clicked, this, &HighlightersDialog::exportHighlighters );
    connect( importButton, &QPushButton::clicked, this, &HighlightersDialog::importHighlighters );

    // No highlighter selected by default
    selectedRow_ = -1;

    connect( highlighterListWidget, &QListWidget::itemSelectionChanged, this,
             &HighlightersDialog::updatePropertyFields );

    connect( highlighterSetEdit_, &HighlighterSetEdit::changed, this,
             &HighlightersDialog::updateHighlighterProperties );

    connect( buttonBox, &QDialogButtonBox::clicked, this, &HighlightersDialog::resolveDialog );

    if ( !highlighterSetCollection_.highlighters_.empty() ) {
        setCurrentRow( 0 );
    }

    quickHighlightLayout->removeWidget( colorLabelsPlaceholder );
    quickHighlightLayout->removeWidget( colorLabelsForeColor );
    quickHighlightLayout->removeWidget( colorLabelsBackColor );
    quickHighlightLayout->removeWidget( colorLabelsCycle );

    quickHighlightLayout->addWidget( colorLabelsPlaceholder, 0, 0 );
    quickHighlightLayout->addWidget( colorLabelsForeColor, 0, 1, Qt::AlignCenter );
    quickHighlightLayout->addWidget( colorLabelsBackColor, 0, 2, Qt::AlignCenter );
    quickHighlightLayout->addWidget( colorLabelsCycle, 0, 3, Qt::AlignCenter );

    const auto quickHighlighters = highlighterSetCollection_.quickHighlighters();
    for ( int i = 0; i < quickHighlighters.size(); ++i ) {
        const auto row = i + 1;
        auto nameEdit = new QLineEdit( quickHighlighters[ i ].name );
        auto foreButton = new QPushButton;
        auto backButton = new QPushButton;
        auto cycleCheckbox = new QCheckBox;

        HighlighterEdit::updateIcon( foreButton, quickHighlighters[ i ].color.foreColor );
        HighlighterEdit::updateIcon( backButton, quickHighlighters[ i ].color.backColor );
        colorLabelForeButtons_.push_back( foreButton );
        colorLabelBackButtons_.push_back( backButton );
        cycleCheckbox->setChecked( quickHighlighters[ i ].useInCycle );

        connect( nameEdit, &QLineEdit::textChanged, nameEdit,
                 [ this, index = i ]( const QString& newName ) {
                     auto highlighters = highlighterSetCollection_.quickHighlighters();
                     if ( !newName.isEmpty() ) {
                         highlighters[ index ].name = newName;
                         highlighterSetCollection_.setQuickHighlighters( highlighters );
                     }
                 } );

        connect( foreButton, &QPushButton::clicked, foreButton, [ foreButton, this, index = i ]() {
            auto highlighters = highlighterSetCollection_.quickHighlighters();
            QColor newColor;
            if ( HighlighterEdit::showColorPicker( highlighters[ index ].color.foreColor,
                                                   newColor ) ) {
                highlighters[ index ].color.foreColor = newColor;
                highlighterSetCollection_.setQuickHighlighters( highlighters );
                HighlighterEdit::updateIcon( foreButton, newColor );
            }
        } );

        connect( backButton, &QPushButton::clicked, backButton, [ backButton, this, index = i ]() {
            auto highlighters = highlighterSetCollection_.quickHighlighters();
            QColor newColor;
            if ( HighlighterEdit::showColorPicker( highlighters[ index ].color.backColor,
                                                   newColor ) ) {
                highlighters[ index ].color.backColor = newColor;
                highlighterSetCollection_.setQuickHighlighters( highlighters );
                HighlighterEdit::updateIcon( backButton, newColor );
            }
        } );

        connect( cycleCheckbox, &QCheckBox::clicked, cycleCheckbox,
                 [ this, index = i ]( bool isChecked ) {
                     auto highlighters = highlighterSetCollection_.quickHighlighters();
                     highlighters[ index ].useInCycle = isChecked;
                     highlighterSetCollection_.setQuickHighlighters( highlighters );
                 } );

        quickHighlightLayout->addWidget( nameEdit, row, 0 );
        quickHighlightLayout->addWidget( foreButton, row, 1 );
        quickHighlightLayout->addWidget( backButton, row, 2 );
        quickHighlightLayout->addWidget( cycleCheckbox, row, 3, Qt::AlignCenter );
    }

    loadIcons();
    Theme::whenApplied( this, [ this ] {
        loadIcons();
        showColorLabelsOfTheme();
    } );
}

void HighlightersDialog::showColorLabelsOfTheme()
{
    const auto labels = highlighterSetCollection_.colorLabelsOfTheme();
    if ( !labels ) {
        return;
    }

    highlighterSetCollection_.setQuickHighlighters( *labels );

    const auto slots = std::min( { colorLabelForeButtons_.size(), colorLabelBackButtons_.size(),
                                   static_cast<std::size_t>( labels->size() ) } );
    for ( std::size_t slot = 0; slot < slots; ++slot ) {
        const auto& color = ( *labels )[ static_cast<int>( slot ) ].color;
        HighlighterEdit::updateIcon( colorLabelForeButtons_[ slot ], color.foreColor );
        HighlighterEdit::updateIcon( colorLabelBackButtons_[ slot ], color.backColor );
    }
}

void HighlightersDialog::loadIcons()
{
    loadListEditIcons( addHighlighterButton, removeHighlighterButton, upHighlighterButton,
                       downHighlighterButton );
}

//
// Q_SLOTS:
//

void HighlightersDialog::exportHighlighters()
{
    if ( selectedRow_ < 0 && selectedTeamRow_ < 0 ) {
        return;
    }

    const HighlighterSet group = selectedRow_ >= 0
                                     ? highlighterSetCollection_.highlighters_.at( selectedRow_ )
                                     : teamGroups_.at( selectedTeamRow_ );
    using namespace logsquirl::groupexchange;

    const auto proposed
        = QDir( exportFolder() )
              .filePath( suggestedFileName( group.name(), GroupKind::Highlighter ) );
    QString file = QFileDialog::getSaveFileName( this, tr( "Export highlighters configuration" ),
                                                 proposed, tr( "Highlighters (*.conf)" ) );

    if ( file.isEmpty() ) {
        return;
    }
    file = withConfSuffix( file );

    if ( !writeGroup( file, group ) ) {
        QMessageBox::warning( this, tr( "Export highlighters configuration" ),
                              tr( "The file %1 could not be written." ).arg( file ) );
        return;
    }
    rememberExportFolder( file );
}

void HighlightersDialog::importHighlighters()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr( "Select one or more files to open" ), "", tr( "Highlighters (*.conf)" ) );

    if ( files.isEmpty() ) {
        return;
    }

    using namespace logsquirl::groupexchange;
    const auto title = tr( "Import highlighters configuration" );
    ImportSession session( askUser( this, title ) );

    // The imported sets are only in this dialog's copy: OK / Apply take them
    // over, Cancel discards them. A replaced set keeps its id, so it stays
    // active when it was.
    for ( const auto& file : files ) {
        LOG_INFO << "Loading highlighters from " << file;
        reportImportError( this, title, file,
                           importFile( file, highlighterSetCollection_.highlighters_, session ) );
    }

    // Show the list as it is now; a replaced set is read again from it.
    const int row = selectedRow_;
    populateHighlighterList();
    setCurrentRow( row >= 0 ? row : highlighterListWidget->count() - 1 );
}

void HighlightersDialog::addHighlighterSet()
{
    LOG_DEBUG << "addHighlighter()";

    highlighterSetCollection_.highlighters_.append( HighlighterSet::createNewSet( DEFAULT_NAME ) );

    // Add and select the newly created highlighter
    highlighterListWidget->addItem( DEFAULT_NAME );

    setCurrentRow( highlighterListWidget->count() - 1 );
}

void HighlightersDialog::removeHighlighterSet()
{
    int index = highlighterListWidget->currentRow();
    LOG_DEBUG << "removeHighlighter() index " << index;

    if ( index >= 0 ) {
        setCurrentRow( -1 );
        dispatchToMainThread( [ this, index ] {
            {
                const auto& set = highlighterSetCollection_.highlighters_.at( index );
                highlighterSetCollection_.deactivateSet( set.id() );
            }

            highlighterSetCollection_.highlighters_.removeAt( index );
            delete highlighterListWidget->takeItem( index );

            int count = highlighterListWidget->count();
            if ( index < count ) {
                // Select the new item at the same index
                setCurrentRow( index );
            }
            else {
                // or the previous index if it is at the end
                setCurrentRow( count - 1 );
            }
        } );
    }
}

void HighlightersDialog::moveHighlighterSetUp()
{
    int index = highlighterListWidget->currentRow();
    LOG_DEBUG << "moveHighlighterUp() index " << index;

    if ( index > 0 ) {
        highlighterSetCollection_.highlighters_.move( index, index - 1 );

        dispatchToMainThread( [ this, index ] {
            QListWidgetItem* item = highlighterListWidget->takeItem( index );
            highlighterListWidget->insertItem( index - 1, item );

            setCurrentRow( index - 1 );
        } );
    }
}

void HighlightersDialog::moveHighlighterSetDown()
{
    int index = highlighterListWidget->currentRow();
    LOG_DEBUG << "moveHighlighterDown() index " << index;

    if ( ( index >= 0 ) && ( index < ( highlighterListWidget->count() - 1 ) ) ) {
        highlighterSetCollection_.highlighters_.move( index, index + 1 );

        dispatchToMainThread( [ this, index ] {
            QListWidgetItem* item = highlighterListWidget->takeItem( index );
            highlighterListWidget->insertItem( index + 1, item );

            setCurrentRow( index + 1 );
        } );
    }
}

void HighlightersDialog::resolveDialog( QAbstractButton* button )
{
    LOG_DEBUG << "resolveDialog()";

    QDialogButtonBox::ButtonRole role = buttonBox->buttonRole( button );
    if ( role == QDialogButtonBox::RejectRole ) {
        reject();
        return;
    }

    // persist it to disk
    auto& persistentHighlighterSet = HighlighterSetCollection::get();
    // The Team sets are not this dialog's: a sync may have changed them since
    // it opened, and they stay as the Team Folder has them.
    const auto teamSets = persistentHighlighterSet.teamHighlighterSets();
    if ( role == QDialogButtonBox::AcceptRole ) {
        persistentHighlighterSet = std::move( highlighterSetCollection_ );
        accept();
    }
    else if ( role == QDialogButtonBox::ApplyRole ) {
        persistentHighlighterSet = highlighterSetCollection_;
    }
    else {
        LOG_ERROR << "unhandled role : " << role;
        return;
    }
    persistentHighlighterSet.setTeamHighlighterSets( teamSets );
    persistentHighlighterSet.save();
    Q_EMIT optionsChanged();
}

void HighlightersDialog::setCurrentRow( int row )
{
    // ugly hack for mac
    dispatchToMainThread( [ this, row ]() { highlighterListWidget->setCurrentRow( row ); } );
}

void HighlightersDialog::updatePropertyFields()
{
    if ( highlighterListWidget->selectedItems().count() >= 1 )
        selectedRow_ = highlighterListWidget->row( highlighterListWidget->selectedItems().at( 0 ) );
    else
        selectedRow_ = -1;

    LOG_DEBUG << "updatePropertyFields(), row = " << selectedRow_;

    if ( selectedRow_ >= 0 ) {
        // One set is shown at a time: one of the user's own, or a Team set.
        if ( teamGroupsList_ ) {
            teamGroupsList_->clearSelection();
        }
        selectedTeamRow_ = -1;
        highlighterSetEdit_->setEnabled( true );

        const HighlighterSet& currentSet
            = highlighterSetCollection_.highlighters_.at( selectedRow_ );
        highlighterSetEdit_->setHighlighters( currentSet );

        // Enable the buttons if needed
        removeHighlighterButton->setEnabled( true );
        upHighlighterButton->setEnabled( selectedRow_ > 0 );
        downHighlighterButton->setEnabled( selectedRow_ < ( highlighterListWidget->count() - 1 ) );
        exportButton->setEnabled( true );
    }
    else if ( selectedTeamRow_ < 0 ) {
        highlighterSetEdit_->setEnabled( true );
        highlighterSetEdit_->reset();
        exportButton->setEnabled( false );

        removeHighlighterButton->setEnabled( false );
        upHighlighterButton->setEnabled( false );
        downHighlighterButton->setEnabled( false );
    }
}

void HighlightersDialog::showTeamGroups( const QList<HighlighterSet>& groups )
{
    teamGroups_ = groups;

    if ( !teamGroupsList_ ) {
        teamGroupsLabel_ = new QLabel( tr( "Team highlighter sets" ), layoutWidget );
        teamGroupsLabel_->setAlignment( Qt::AlignCenter );
        teamGroupsLabel_->setToolTip(
            tr( "Shared through the Team Folder: they change when the team changes them." ) );
        teamGroupsList_ = new QListWidget( layoutWidget );
        teamGroupsList_->setSizePolicy( QSizePolicy::MinimumExpanding, QSizePolicy::Expanding );
        verticalLayout->addWidget( teamGroupsLabel_ );
        verticalLayout->addWidget( teamGroupsList_ );
        connect( teamGroupsList_, &QListWidget::itemSelectionChanged, this,
                 &HighlightersDialog::showSelectedTeamGroup );
    }

    selectedTeamRow_ = -1;
    teamGroupsList_->clear();
    for ( const auto& group : teamGroups_ ) {
        teamGroupsList_->addItem( group.name() );
    }
}

void HighlightersDialog::showSelectedTeamGroup()
{
    const auto selected = teamGroupsList_->selectedItems();
    if ( selected.isEmpty() ) {
        return;
    }
    const auto row = teamGroupsList_->row( selected.at( 0 ) );

    // Leaves the user's own set; what was changed in it stays in this
    // dialog's copy until OK, Apply or Cancel.
    highlighterListWidget->clearSelection();

    selectedTeamRow_ = row;
    highlighterSetEdit_->setHighlighters( teamGroups_.at( row ) );
    highlighterSetEdit_->setEnabled( false );
    removeHighlighterButton->setEnabled( false );
    upHighlighterButton->setEnabled( false );
    downHighlighterButton->setEnabled( false );
    exportButton->setEnabled( true );
}

void HighlightersDialog::updateHighlighterProperties()
{
    LOG_DEBUG << "updateHighlighterProperties()";

    // If a row is selected
    if ( selectedRow_ >= 0 ) {
        HighlighterSet& currentSet = highlighterSetCollection_.highlighters_[ selectedRow_ ];
        currentSet = highlighterSetEdit_->highlighters();
        // Update the entry in the highlighterList widget
        highlighterListWidget->currentItem()->setText( currentSet.name() );
    }
}

//
// Private functions
//

void HighlightersDialog::populateHighlighterList()
{
    highlighterListWidget->clear();
    for ( const HighlighterSet& highlighterSet :
          logsquirl::as_const( highlighterSetCollection_.highlighters_ ) ) {
        auto* new_item = new QListWidgetItem( highlighterSet.name() );
        // new_item->setFlags( Qt::ItemIsSelectable | Qt::ItemIsEditable | Qt::ItemIsEnabled );
        highlighterListWidget->addItem( new_item );
    }
}
