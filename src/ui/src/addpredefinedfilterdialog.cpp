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
#include <QDialogButtonBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>

#include <algorithm>
#include <climits>
#include <optional>

#include "addtogrouplayout.h"
#include "groupchooser.h"
#include "groupedits.h"
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

QList<GroupChooser::Group> choicesOf( const QList<PredefinedFilterSet>& groups )
{
    QList<GroupChooser::Group> choices;
    for ( const auto& group : groups ) {
        QStringList names;
        for ( const auto& filter : group.filters() ) {
            names.append( filter.name );
        }
        choices.append( GroupChooser::Group{ group.id(), group.name(), names } );
    }
    return choices;
}

} // namespace

AddPredefinedFilterDialog::AddPredefinedFilterDialog( const QString& selectedText, QWidget* parent )
    : QDialog( parent )
    , selectedText_( selectedText )
    , ownGroups_( PredefinedFiltersCollection::getSynced().filterSets() )
{
    setWindowTitle( tr( "Add as Predefined Filter" ) );

    // What is added, where it goes, its preview: the layout Name This Value
    // has too (#958).
    layout_ = std::make_unique<AddToGroupLayout>( this );
    using Section = AddToGroupLayout::Section;

    name_ = new QLineEdit( this );
    name_->setObjectName( QStringLiteral( "filterName" ) );
    name_->setToolTip( tr( "The name the Filters panel lists the Predefined Filter by" ) );
    layout_->addRow( Section::What, tr( "Name:" ), name_ );

    pattern_ = new QLineEdit( selectedText, this );
    pattern_->setObjectName( QStringLiteral( "filterPattern" ) );
    pattern_->setToolTip( tr( "What the Predefined Filter searches for; proposed from the "
                              "selection, it finds the selected text as it is" ) );
    layout_->addRow( Section::What, tr( "Pattern:" ), pattern_ );

    regex_ = new QCheckBox( tr( "Regular expression" ), this );
    regex_->setObjectName( QStringLiteral( "filterRegex" ) );
    regex_->setToolTip( tr( "Read the pattern as a regular expression; checking it escapes the "
                            "pattern so that it still finds the same text" ) );
    layout_->addRow( Section::What, QString{}, regex_ );

    // The user's own groups and, once the dialog is given them, the Team's;
    // the Default Filter Group chosen. The plugins' groups are read-only.
    GroupChooser::Texts texts;
    texts.ownSection = tr( "My Filter Groups" );
    texts.teamSection = tr( "Team Filter Groups" );
    texts.newGroup = tr( "New Filter Group..." );
    texts.newTeamGroup = tr( "New Team Filter Group..." );
    texts.newGroupName = tr( "New Filter Group" );
    texts.teamHint = tr( "OK shares this Filter Group with the team." );
    texts.nameEmpty = tr( "The new Filter Group has no name." );
    texts.nameTaken = tr( "A Filter Group named \"%1\" exists already." );
    group_ = new GroupChooser( texts, this );
    group_->setObjectName( QStringLiteral( "filterGroup" ) );
    layout_->addRow( Section::Where, tr( "Filter Group:" ), group_ );

    applyNow_ = new QCheckBox( tr( "Search with it now" ), this );
    applyNow_->setObjectName( QStringLiteral( "applyNow" ) );
    applyNow_->setToolTip( tr( "Replace the current search with the Predefined Filter" ) );
    layout_->addRow( Section::Where, QString{}, applyNow_ );

    layout_->preview()->setRemark( tr( "No Log File to search in." ) );

    connect( name_, &QLineEdit::textChanged, this, &AddPredefinedFilterDialog::updateState );
    connect( pattern_, &QLineEdit::textChanged, this, &AddPredefinedFilterDialog::updateState );
    connect( pattern_, &QLineEdit::textChanged, this, &AddPredefinedFilterDialog::searchPreview );
    connect( regex_, &QCheckBox::toggled, this, &AddPredefinedFilterDialog::regexToggled );
    connect( group_, &GroupChooser::choiceChanged, this, &AddPredefinedFilterDialog::groupChanged );

    offerGroups( defaultFilterSetId() );

    name_->setFocus();
    name_->selectAll();
    layout_->fitDialog();
}

AddPredefinedFilterDialog::~AddPredefinedFilterDialog() = default;

void AddPredefinedFilterDialog::previewIn( std::shared_ptr<const LogData> logData, bool matchCase )
{
    matchCase_ = matchCase;
    previewSearch_ = std::make_unique<FilterPreviewSearch>( std::move( logData ) );
    connect( previewSearch_.get(), &FilterPreviewSearch::resultChanged, this,
             &AddPredefinedFilterDialog::showPreview );
    searchPreview();
}

RegularExpressionPattern AddPredefinedFilterDialog::previewPattern() const
{
    // As the Search Line reads the filter: as a regex or as plain text,
    // matching case as the tab's Search does.
    return RegularExpressionPattern( pattern_->text(), matchCase_, false, false,
                                     !regex_->isChecked() );
}

void AddPredefinedFilterDialog::searchPreview()
{
    if ( !previewSearch_ ) {
        return;
    }
    // A regex that is not valid finds nothing: the warning says why.
    auto pattern = previewPattern();
    if ( regex_->isChecked() && !QRegularExpression( pattern_->text() ).isValid() ) {
        pattern.pattern.clear();
    }
    previewSearch_->setPattern( pattern );
}

FilterPreviewSearch::Result AddPredefinedFilterDialog::previewResult() const
{
    return previewSearch_ ? previewSearch_->result() : FilterPreviewSearch::Result{};
}

void AddPredefinedFilterDialog::showPreview()
{
    auto* preview = layout_->preview();
    auto* note = layout_->previewNote();
    const auto result = previewSearch_->result();
    const auto count = static_cast<int>(
        std::min<LinesCount::UnderlyingType>( result.matchCount.get(), INT_MAX ) );

    using State = FilterPreviewSearch::Result::State;
    switch ( result.state ) {
    case State::Idle:
        note->hide();
        preview->setRemark( tr( "The pattern finds nothing." ) );
        return;
    case State::Failed:
        note->hide();
        preview->setRemark( result.error );
        return;
    case State::Searching:
        note->setText( tr( "Searching... %Ln Log Line(s) so far", "", count ) );
        break;
    case State::Done:
        note->setText( tr( "%Ln Log Line(s) found", "", count ) );
        break;
    }
    note->show();

    if ( !result.firstMatch ) {
        preview->setRemark( result.state == State::Done
                                ? tr( "The pattern finds no Log Line in this Log File." )
                                : tr( "Searching the Log File..." ) );
        return;
    }
    // The first Log Line found, its matches marked as the Search marks them.
    QList<LinePreview::Mark> marks;
    const auto regex = static_cast<QRegularExpression>( previewPattern() );
    auto matches = regex.globalMatch( result.firstLine );
    while ( matches.hasNext() && marks.size() < MaxPreviewMarks ) {
        const auto match = matches.next();
        if ( match.capturedLength() > 0 ) {
            marks.append( LinePreview::Mark{ match.capturedStart(), match.capturedLength() } );
        }
    }
    preview->setLine( result.firstLine, marks, LinePreview::MarkStyle::Highlight );
}

void AddPredefinedFilterDialog::showTeamGroups( const QList<PredefinedFilterSet>& groups,
                                                bool editable,
                                                const QHash<QString, QString>& revisions )
{
    const auto chosen = group_->choice().id;
    teamEdits_.reset( groups, editable, revisions );
    showsTeam_ = true;
    offerGroups( chosen );
}

void AddPredefinedFilterDialog::proposeGroup( const QString& groupId )
{
    group_->choose( groupId, defaultFilterSetId() );
}

void AddPredefinedFilterDialog::offerGroups( const QString& chosenId )
{
    std::optional<GroupChooser::TeamGroups> team;
    if ( showsTeam_ ) {
        team
            = GroupChooser::TeamGroups{ choicesOf( teamEdits_.groups() ), teamEdits_.isEditable() };
    }
    group_->setGroups( choicesOf( ownGroups_ ), team );
    group_->choose( chosenId, defaultFilterSetId() );
}

PredefinedFilter AddPredefinedFilterDialog::filter() const
{
    return PredefinedFilter{ name_->text().trimmed(), pattern_->text(), regex_->isChecked() };
}

QString AddPredefinedFilterDialog::chosenGroupId() const
{
    const auto chosen = group_->choice();
    return chosen.isNew() ? madeGroupId_ : chosen.id;
}

QStringList AddPredefinedFilterDialog::chosenGroupNames() const
{
    return group_->itemNames();
}

bool AddPredefinedFilterDialog::applyNow() const
{
    return applyNow_->isChecked();
}

QString AddPredefinedFilterDialog::warning() const
{
    return layout_->warning()->text();
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
    searchPreview();
}

void AddPredefinedFilterDialog::groupChanged()
{
    // A name no filter of the group has: the Filters panel tells the filters
    // of a group apart by their names.
    if ( name_->text() == proposedFilterName_ ) {
        proposedFilterName_
            = logsquirl::valuenames::freeName( proposedName( selectedText_ ), chosenGroupNames() );
        name_->setText( proposedFilterName_ );
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
    else {
        warning = group_->warning();
    }
    layout_->setWarning( warning );
    layout_->buttons()
        ->button( QDialogButtonBox::Ok )
        ->setEnabled( warning.isEmpty() && !name.isEmpty() );
}

void AddPredefinedFilterDialog::accept()
{
    const auto chosen = group_->choice();
    const auto added = filter();
    auto made = PredefinedFilterSet::createNewSet( chosen.name );
    made.addFilter( added );

    if ( chosen.section == GroupChooser::Section::Team ) {
        // Published as the Predefined Filters dialog publishes a Team group:
        // what changed against the group as given, based on its revision.
        auto& groups = teamEdits_.groups();
        if ( chosen.isNew() ) {
            madeGroupId_ = made.id();
            groups.append( made );
        }
        else {
            for ( auto& group : groups ) {
                if ( group.id() == chosen.id ) {
                    group.addFilter( added );
                }
            }
        }
        const auto requests = teamEdits_.takeRequests();
        if ( !requests.isEmpty() ) {
            Q_EMIT publishRequested( requests );
        }
    }
    else {
        auto& collection = PredefinedFiltersCollection::get();
        if ( chosen.isNew() ) {
            madeGroupId_ = made.id();
            auto groups = collection.filterSets();
            groups.append( made );
            collection.setFilterSets( groups );
        }
        else {
            collection.addFilter( chosen.id, added );
        }
        collection.save();
    }
    QDialog::accept();
}
