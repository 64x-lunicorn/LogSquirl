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
#include <QGridLayout>
#include <QMessageBox>
#include <QTimer>

#include <qcheckbox.h>
#include <qcolor.h>
#include <qlineedit.h>
#include <qnamespace.h>
#include <qpushbutton.h>
#include <utility>

#include "containers.h"
#include "groupexchange.h"
#include "groupimportprompt.h"
#include "highlightersdialog.h"
#include "highlighterset.h"
#include "iconloader.h"
#include "log.h"
#include "plaintext.h"
#include "theme.h"

static constexpr QLatin1String DEFAULT_NAME = QLatin1String( "New Highlighter set", 19 );

// Construct the box, including a copy of the global highlighterSet
// to handle ok/cancel/apply
HighlightersDialog::HighlightersDialog( QWidget* parent )
    : QDialog( parent )
{
    setupUi( this );

    groupList_ = new GroupList( logsquirl::groupexchange::GroupKind::Highlighter, layoutWidget );
    verticalLayout->addWidget( groupList_ );

    highlighterSetEdit_ = new HighlighterSetEdit( this );
    highlighterSetEdit_->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );
    highlighterLayout->addWidget( highlighterSetEdit_ );

    splitter->setStretchFactor( 0, 0 );
    splitter->setStretchFactor( 1, 1 );

    // Reload the highlighter list from disk (in case it has been changed
    // by another glogg instance) and copy it to here.
    highlighterSetCollection_ = HighlighterSetCollection::getSynced();

    listOwnGroups();

    // The plugins' sets below the user's own, read-only: they are copied,
    // never edited, and OK or Apply stores none of them (#865). Whether one
    // is active is chosen in the Highlighters menu.
    groupList_->setPluginGroups( highlighterSetCollection_.pluginHighlighterSets() );

    connect( groupList_, &GroupList::currentChanged, this, &HighlightersDialog::showGroup );
    connect( groupList_, &GroupList::newRequested, this, &HighlightersDialog::addGroup );
    connect( groupList_, &GroupList::deleteRequested, this, &HighlightersDialog::deleteGroup );
    connect( groupList_, &GroupList::moveRequested, this, &HighlightersDialog::moveGroup );
    connect( groupList_, &GroupList::shareRequested, this, &HighlightersDialog::shareGroup );
    connect( groupList_, &GroupList::copyRequested, this, &HighlightersDialog::copyGroup );
    connect( groupList_, &GroupList::exportRequested, this,
             &HighlightersDialog::exportHighlighters );
    connect( groupList_, &GroupList::importRequested, this,
             &HighlightersDialog::importHighlighters );

    connect( highlighterSetEdit_, &HighlighterSetEdit::changed, this,
             &HighlightersDialog::updateHighlighterProperties );

    connect( buttonBox, &QDialogButtonBox::clicked, this, &HighlightersDialog::resolveDialog );

    if ( !highlighterSetCollection_.highlighters_.empty() ) {
        groupList_->setCurrent( GroupList::Section::Own, 0 );
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

    Theme::whenApplied( this, [ this ] { showColorLabelsOfTheme(); } );
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

// --- The Group List ---

void HighlightersDialog::listOwnGroups()
{
    groupList_->setOwnGroups( GroupList::namesOf( highlighterSetCollection_.highlighters_ ) );
}

void HighlightersDialog::showGroup( GroupList::Section section, int row )
{
    // One set is shown at a time: one of the user's own, a Team set or a
    // plugin's set. What was changed in the one shown before stays in this
    // dialog's copy until OK, Apply or Cancel.
    selectedRow_ = section == GroupList::Section::Own ? row : -1;
    selectedTeamRow_ = section == GroupList::Section::Team ? row : -1;
    selectedPluginRow_ = section == GroupList::Section::Plugin ? row : -1;

    LOG_DEBUG << "showGroup(), row = " << row;

    if ( selectedRow_ >= 0 ) {
        highlighterSetEdit_->setEnabled( true );
        highlighterSetEdit_->setHighlighters(
            highlighterSetCollection_.highlighters_.at( selectedRow_ ) );
    }
    else if ( selectedTeamRow_ >= 0 ) {
        highlighterSetEdit_->setHighlighters( teamEdits_.groups().at( selectedTeamRow_ ) );
        highlighterSetEdit_->setEnabled( teamEdits_.isEditable() );
    }
    else if ( selectedPluginRow_ >= 0 ) {
        highlighterSetEdit_->setHighlighters(
            highlighterSetCollection_.pluginSets_.at( selectedPluginRow_ ).group );
        highlighterSetEdit_->setEnabled( false );
    }
    else {
        highlighterSetEdit_->setEnabled( true );
        highlighterSetEdit_->reset();
    }
}

void HighlightersDialog::addGroup( GroupList::Section section )
{
    LOG_DEBUG << "addGroup()";

    if ( section == GroupList::Section::Team ) {
        if ( !teamEdits_.isEditable() ) {
            return;
        }
        teamEdits_.groups().append( HighlighterSet::createNewSet( DEFAULT_NAME ) );
        groupList_->setTeamGroups( teamEdits_.names(), true );
        groupList_->setCurrent( section, groupList_->count( section ) - 1 );
        return;
    }

    highlighterSetCollection_.highlighters_.append( HighlighterSet::createNewSet( DEFAULT_NAME ) );
    listOwnGroups();
    groupList_->setCurrent( section, groupList_->count( section ) - 1 );
}

void HighlightersDialog::deleteGroup( GroupList::Section section, int row )
{
    LOG_DEBUG << "deleteGroup() row " << row;

    if ( section == GroupList::Section::Team ) {
        // The Group List asked whether it goes for the whole team.
        if ( !teamEdits_.isEditable() || row < 0 || row >= teamEdits_.groups().size() ) {
            return;
        }
        teamEdits_.groups().removeAt( row );
        groupList_->setTeamGroups( teamEdits_.names(), true );
        return;
    }

    auto& sets = highlighterSetCollection_.highlighters_;
    if ( section != GroupList::Section::Own || row < 0 || row >= sets.size() ) {
        return;
    }
    highlighterSetCollection_.deactivateSet( sets.at( row ).id() );
    sets.removeAt( row );
    listOwnGroups();
    // Select the set now at the same row, or the one before it at the end.
    const auto count = groupList_->count( section );
    groupList_->setCurrent( section, row < count ? row : count - 1 );
}

void HighlightersDialog::moveGroup( int row, int offset )
{
    LOG_DEBUG << "moveGroup() row " << row << " by " << offset;

    auto& sets = highlighterSetCollection_.highlighters_;
    const auto to = row + offset;
    if ( row < 0 || row >= sets.size() || to < 0 || to >= sets.size() ) {
        return;
    }
    sets.move( row, to );
    listOwnGroups();
    groupList_->setCurrent( GroupList::Section::Own, to );
}

void HighlightersDialog::shareGroup( int row )
{
    auto& sets = highlighterSetCollection_.highlighters_;
    if ( !teamEdits_.isEditable() || row < 0 || row >= sets.size() ) {
        return;
    }
    // What was typed in the set so far is shared.
    if ( row == selectedRow_ ) {
        sets[ row ] = highlighterSetEdit_->highlighters();
    }

    teamEdits_.share( sets[ row ] );
    groupList_->setTeamGroups( teamEdits_.names(), true );
    // The Team copy is shown; the set of the user's own stays as it is.
    groupList_->setCurrent( GroupList::Section::Team,
                            groupList_->count( GroupList::Section::Team ) - 1 );
}

void HighlightersDialog::copyGroup( GroupList::Section section, int row )
{
    auto& sets = highlighterSetCollection_.highlighters_;
    HighlighterSet copy;
    if ( section == GroupList::Section::Team && row >= 0 && row < teamEdits_.groups().size() ) {
        if ( teamEdits_.isEditable() && row == selectedTeamRow_ ) {
            teamEdits_.groups()[ row ] = highlighterSetEdit_->highlighters();
        }
        copy = teamEdits_.copyFor( row, sets );
    }
    else if ( section == GroupList::Section::Plugin && row >= 0
              && row < highlighterSetCollection_.pluginSets_.size() ) {
        copy = logsquirl::teamfolder::copyOfGroup(
            highlighterSetCollection_.pluginSets_.at( row ).group, GroupList::namesOf( sets ) );
    }
    else {
        return;
    }

    sets.append( copy );
    listOwnGroups();
    groupList_->setCurrent( GroupList::Section::Own,
                            groupList_->count( GroupList::Section::Own ) - 1 );
}

// --- Import / Export ---

void HighlightersDialog::exportHighlighters()
{
    if ( selectedRow_ < 0 && selectedTeamRow_ < 0 && selectedPluginRow_ < 0 ) {
        return;
    }

    const HighlighterSet group = [ this ] {
        if ( selectedRow_ >= 0 ) {
            return highlighterSetCollection_.highlighters_.at( selectedRow_ );
        }
        if ( selectedTeamRow_ >= 0 ) {
            return teamEdits_.groups().at( selectedTeamRow_ );
        }
        return highlighterSetCollection_.pluginSets_.at( selectedPluginRow_ ).group;
    }();
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
        logsquirl::plaintext::warning( this, tr( "Export highlighters configuration" ),
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
    // A set of a Team set's id is a copy of it, not that set, and so is one of
    // a plugin's set's id.
    auto reserved = idsOf( teamEdits_.groups() );
    for ( const auto& set : logsquirl::as_const( highlighterSetCollection_.pluginSets_ ) ) {
        reserved.insert( set.group.id() );
    }
    ImportSession session( askUser( this, title ), reserved );

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
    listOwnGroups();
    groupList_->setCurrent( GroupList::Section::Own,
                            row >= 0 ? row : groupList_->count( GroupList::Section::Own ) - 1 );
}

void HighlightersDialog::resolveDialog( QAbstractButton* button )
{
    LOG_DEBUG << "resolveDialog()";

    QDialogButtonBox::ButtonRole role = buttonBox->buttonRole( button );
    if ( role == QDialogButtonBox::RejectRole ) {
        reject();
        return;
    }

    // What was typed in a Team set is in the dialog's copy before it is sent.
    if ( selectedTeamRow_ >= 0 && teamEdits_.isEditable() ) {
        teamEdits_.groups()[ selectedTeamRow_ ] = highlighterSetEdit_->highlighters();
    }

    // persist it to disk
    auto& persistentHighlighterSet = HighlighterSetCollection::get();
    // The Team sets are not this dialog's: a sync may have changed them since
    // it opened, and they stay as the Team Folder has them.
    const auto teamSets = persistentHighlighterSet.teamHighlighterSets();
    // Nor are the plugins' sets: they stay as the enabled plugins ship them.
    const auto pluginSets = persistentHighlighterSet.pluginHighlighterSets();
    if ( role == QDialogButtonBox::AcceptRole ) {
        // Copied, not moved: the dialog keeps a set selected, and its editor
        // may still report a change into the dialog's copy while it closes.
        persistentHighlighterSet = highlighterSetCollection_;
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
    persistentHighlighterSet.setPluginHighlighterSets( pluginSets, false );
    persistentHighlighterSet.save();
    Q_EMIT optionsChanged();

    // What was done to the Team sets goes to the team.
    if ( teamEdits_.isEditable() ) {
        const auto requests = teamEdits_.takeRequests();
        if ( !requests.isEmpty() ) {
            Q_EMIT publishRequested( requests );
        }
    }
}

void HighlightersDialog::showTeamGroups( const QList<HighlighterSet>& groups, bool editable,
                                         const QHash<QString, QString>& revisions )
{
    teamEdits_.reset( groups, editable, revisions );
    // A Team set shown is not any more: the list says so.
    groupList_->setTeamGroups( teamEdits_.names(), editable );
}

void HighlightersDialog::setRegexLabAccess( RegexLabAccess access )
{
    highlighterSetEdit_->setRegexLabAccess( std::move( access ) );
}

void HighlightersDialog::updateTeamRevisions( const QStringList& ids,
                                              const QHash<QString, QString>& revisions )
{
    teamEdits_.updateRevisions( ids, revisions );
}

void HighlightersDialog::updateHighlighterProperties()
{
    LOG_DEBUG << "updateHighlighterProperties()";

    // If a row is selected
    if ( selectedRow_ >= 0 ) {
        HighlighterSet& currentSet = highlighterSetCollection_.highlighters_[ selectedRow_ ];
        currentSet = highlighterSetEdit_->highlighters();
        // Update the entry in the Group List
        groupList_->setName( GroupList::Section::Own, selectedRow_, currentSet.name() );
    }
    else if ( selectedTeamRow_ >= 0 && teamEdits_.isEditable() ) {
        auto& group = teamEdits_.groups()[ selectedTeamRow_ ];
        group = highlighterSetEdit_->highlighters();
        groupList_->setName( GroupList::Section::Team, selectedTeamRow_, group.name() );
    }
}
