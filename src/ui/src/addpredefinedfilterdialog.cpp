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
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>

#include <algorithm>
#include <climits>

#include "addtogrouplayout.h"
#include "groupchooser.h"
#include "groupedits.h"
#include "highlightercoloring.h"
#include "regularexpression.h"
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

// The names of the filters of a group, which the Group Chooser lists.
QStringList filterNamesOf( const PredefinedFilterSet& group )
{
    QStringList names;
    for ( const auto& filter : group.filters() ) {
        names.append( filter.name );
    }
    return names;
}

// What a Highlighter Set holds, as its tooltip lists it: the patterns of its
// Highlighters, which have no names.
QStringList highlighterPatternsOf( const HighlighterSet& set )
{
    QStringList patterns;
    for ( const auto& highlighter : set.highlighters() ) {
        patterns.append( highlighter.pattern() );
    }
    return patterns;
}

} // namespace

QString AddPredefinedFilterDialog::highlighterPattern( const PredefinedFilter& filter )
{
    // Escaped as the Search escapes a pattern it reads as plain text
    // (RegularExpressionPattern), so that both find the same text.
    return filter.useRegex ? filter.pattern : QRegularExpression::escape( filter.pattern );
}

AddPredefinedFilterDialog::AddPredefinedFilterDialog( const QString& pattern, Reading reading,
                                                      QWidget* parent )
    : QDialog( parent )
    , openedPattern_( pattern )
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

    pattern_ = new QLineEdit( pattern, this );
    pattern_->setObjectName( QStringLiteral( "filterPattern" ) );
    pattern_->setToolTip( tr( "What the Predefined Filter searches for, proposed from the "
                              "selection or the current search" ) );
    layout_->addRow( Section::What, tr( "Pattern:" ), pattern_ );

    regex_ = new QCheckBox( tr( "Regular expression" ), this );
    regex_->setObjectName( QStringLiteral( "filterRegex" ) );
    regex_->setToolTip( tr( "Read the pattern as a regular expression; checking it escapes the "
                            "pattern so that it still finds the same text" ) );
    // A regex is taken as it is: checked before regexToggled() listens, it
    // is not escaped again.
    regex_->setChecked( reading == Reading::Regex );
    layout_->addRow( Section::What, QString{}, regex_ );

    // Shown only once noteSearchLineSettings() finds a setting not kept.
    unkeptNote_ = new QLabel( this );
    unkeptNote_->setObjectName( QStringLiteral( "unkeptSettings" ) );
    unkeptNote_->setWordWrap( true );
    unkeptNote_->setTextFormat( Qt::PlainText );
    layout_->addRow( Section::What, QString{}, unkeptNote_ );
    layout_->form()->setRowVisible( unkeptNote_, false );

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

    // Also highlight it (#986): the rows of Highlight This and the set the
    // Highlighter goes into, shown only while it is checked.
    alsoHighlight_ = new QCheckBox( tr( "Also highlight it" ), this );
    alsoHighlight_->setObjectName( QStringLiteral( "alsoHighlight" ) );
    alsoHighlight_->setToolTip( tr( "Also add a Highlighter with the same pattern, which colors "
                                    "what the filter finds while it is not searched with" ) );
    layout_->addRow( Section::Also, QString{}, alsoHighlight_ );
    coloring_ = new HighlighterColoring( this );
    layout_->addRow( Section::Also, tr( "Colors:" ), coloring_->colorsField() );
    layout_->addRow( Section::Also, tr( "Highlight:" ), coloring_->highlightField() );
    GroupChooser::Texts setTexts;
    setTexts.ownSection = tr( "My Highlighter Sets" );
    setTexts.teamSection = tr( "Team Highlighter Sets" );
    setTexts.newGroup = tr( "New Highlighter Set..." );
    setTexts.newTeamGroup = tr( "New Team Highlighter Set..." );
    setTexts.newGroupName = tr( "New Highlighter Set" );
    setTexts.teamHint = tr( "OK shares this Highlighter Set with the team." );
    setTexts.nameEmpty = tr( "The new Highlighter Set has no name." );
    setTexts.nameTaken = tr( "A Highlighter Set named \"%1\" exists already." );
    highlighterSet_ = new GroupChooser( setTexts, this );
    highlighterSet_->setObjectName( QStringLiteral( "highlighterSet" ) );
    layout_->addRow( Section::Also, tr( "Highlighter Set:" ), highlighterSet_ );
    alsoHighlightToggled( false );

    layout_->preview()->setRemark( tr( "No Log File to search in." ) );

    connect( name_, &QLineEdit::textChanged, this, &AddPredefinedFilterDialog::updateState );
    connect( pattern_, &QLineEdit::textChanged, this, &AddPredefinedFilterDialog::updateState );
    connect( pattern_, &QLineEdit::textChanged, this, &AddPredefinedFilterDialog::searchPreview );
    connect( pattern_, &QLineEdit::textChanged, this,
             &AddPredefinedFilterDialog::updateUnkeptNote );
    connect( regex_, &QCheckBox::toggled, this, &AddPredefinedFilterDialog::regexToggled );
    connect( group_, &GroupChooser::choiceChanged, this, &AddPredefinedFilterDialog::groupChanged );
    connect( alsoHighlight_, &QCheckBox::toggled, this,
             &AddPredefinedFilterDialog::alsoHighlightToggled );
    connect( highlighterSet_, &GroupChooser::choiceChanged, this,
             &AddPredefinedFilterDialog::highlighterSetChanged );
    connect( coloring_, &HighlighterColoring::changed, this, [ this ] {
        if ( previewSearch_ ) {
            showPreview();
        }
    } );

    groups_ = std::make_unique<ChooserGroups<PredefinedFilterSet>>(
        group_, PredefinedFiltersCollection::getSynced().filterSets(), filterNamesOf,
        [ this ]( const QString& id ) { proposeGroup( id ); } );
    groups_->offer( defaultFilterSetId() );

    // The Team's and the plugins' sets are not among these: the Team's come
    // with showTeamHighlighterSets(), the plugins' are read-only.
    highlighterSets_ = std::make_unique<ChooserGroups<HighlighterSet>>(
        highlighterSet_, HighlighterSetCollection::getSynced().highlighterSets(),
        highlighterPatternsOf, [ this ]( const QString& id ) { proposeHighlighterSet( id ); } );
    highlighterSets_->offer( QString{} );

    name_->setFocus();
    name_->selectAll();
    layout_->fitDialog();
}

AddPredefinedFilterDialog::~AddPredefinedFilterDialog() = default;

void AddPredefinedFilterDialog::previewIn( std::shared_ptr<const LogData> logData,
                                           const SearchLine::Flags& searchFlags,
                                           FilterPreviewSearch::PreparePattern prepare )
{
    searchFlags_ = searchFlags;
    previewSearch_
        = std::make_unique<FilterPreviewSearch>( std::move( logData ), std::move( prepare ) );
    connect( previewSearch_.get(), &FilterPreviewSearch::resultChanged, this,
             &AddPredefinedFilterDialog::showPreview );
    searchPreview();
}

RegularExpressionPattern AddPredefinedFilterDialog::previewPattern() const
{
    const auto text = pattern_->text();
    // A filter with Field Terms is a logical combination of its own, as the
    // Search Line uses it (SearchLine::useFilters()): as written, its
    // sub-patterns read as the tab's Search reads them (#798).
    if ( hasFieldTerms( text ) ) {
        return RegularExpressionPattern( QChar( '(' ) + text + QChar( ')' ), searchFlags_.matchCase,
                                         false, true, !searchFlags_.useRegexp );
    }
    // As the Search Line reads the filter: as a regex or as plain text,
    // matching case as the tab's Search does.
    return RegularExpressionPattern( text, searchFlags_.matchCase, false, false,
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
    // As the Highlighter will color it, while Also highlight it is checked.
    if ( alsoHighlights() ) {
        preview->setColoredLine( result.firstLine, marks,
                                 LinePreview::Coloring{ coloring_->foreColor(),
                                                        coloring_->backColor(),
                                                        !coloring_->onlyMatch() } );
    }
    else {
        preview->setLine( result.firstLine, marks, LinePreview::MarkStyle::Highlight );
    }
}

void AddPredefinedFilterDialog::showTeamGroups( const QList<PredefinedFilterSet>& groups,
                                                bool editable,
                                                const QHash<QString, QString>& revisions )
{
    groups_->showTeam( groups, editable, revisions );
}

void AddPredefinedFilterDialog::proposeGroup( const QString& groupId )
{
    group_->choose( groupId, defaultFilterSetId() );
}

PredefinedFilter AddPredefinedFilterDialog::filter() const
{
    return PredefinedFilter{ name_->text().trimmed(), pattern_->text(), regex_->isChecked() };
}

QString AddPredefinedFilterDialog::chosenGroupId() const
{
    return groups_->chosenId();
}

QStringList AddPredefinedFilterDialog::chosenGroupNames() const
{
    return group_->itemNames();
}

bool AddPredefinedFilterDialog::applyNow() const
{
    return applyNow_->isChecked();
}

void AddPredefinedFilterDialog::setAlsoHighlight( bool alsoHighlight )
{
    alsoHighlight_->setChecked( alsoHighlight );
}

bool AddPredefinedFilterDialog::alsoHighlights() const
{
    return alsoHighlight_->isChecked();
}

void AddPredefinedFilterDialog::showTeamHighlighterSets( const QList<HighlighterSet>& sets,
                                                         bool editable,
                                                         const QHash<QString, QString>& revisions )
{
    highlighterSets_->showTeam( sets, editable, revisions );
}

void AddPredefinedFilterDialog::proposeHighlighterSet( const QString& setId )
{
    highlighterSet_->choose( setId, QString{} );
}

Highlighter AddPredefinedFilterDialog::highlighter() const
{
    return Highlighter( highlighterPattern( filter() ), !searchFlags_.matchCase,
                        coloring_->onlyMatch(), coloring_->foreColor(), coloring_->backColor() );
}

QString AddPredefinedFilterDialog::chosenHighlighterSetId() const
{
    return highlighterSets_->chosenId();
}

void AddPredefinedFilterDialog::alsoHighlightToggled( bool alsoHighlight )
{
    auto* form = layout_->form();
    form->setRowVisible( coloring_->colorsField(), alsoHighlight );
    form->setRowVisible( coloring_->highlightField(), alsoHighlight );
    form->setRowVisible( highlighterSet_, alsoHighlight );
    // The rows shown, the dialog grows to show them whole.
    if ( isVisible() && height() < sizeHint().height() ) {
        resize( width(), sizeHint().height() );
    }
    updateState();
    if ( previewSearch_ ) {
        showPreview();
    }
}

void AddPredefinedFilterDialog::highlighterSetChanged()
{
    const auto* chosen = highlighterSets_->chosen();
    coloring_->followSet( chosen != nullptr ? chosen->highlighters().size() : 0 );
    updateState();
}

QString AddPredefinedFilterDialog::highlighterWarning() const
{
    if ( !alsoHighlights() ) {
        return {};
    }
    // A filter with Field Terms finds by the fields of a Log Line, which a
    // Highlighter does not know of.
    if ( hasFieldTerms( pattern_->text() ) ) {
        return tr( "A pattern with Field Terms cannot be highlighted." );
    }
    return highlighterSet_->warning();
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
            = logsquirl::valuenames::freeName( proposedName( openedPattern_ ), chosenGroupNames() );
        name_->setText( proposedFilterName_ );
    }
    updateState();
}

void AddPredefinedFilterDialog::noteSearchLineSettings( const SearchLine::Flags& searchLineFlags )
{
    searchLineFlags_ = searchLineFlags;
    updateUnkeptNote();
}

void AddPredefinedFilterDialog::updateUnkeptNote()
{
    if ( !searchLineFlags_ ) {
        return;
    }
    QStringList notes;
    if ( searchLineFlags_->matchCase ) {
        notes.append( tr( "Match case is not kept: the filter matches as the Search it is used "
                          "in says." ) );
    }
    if ( searchLineFlags_->inverse ) {
        notes.append( tr( "Inverse match is not kept: the filter finds the matching Log Lines, "
                          "not the others." ) );
    }
    // A combination with Field Terms is used as written (#798); any other is
    // escaped as one string.
    if ( searchLineFlags_->booleanCombination && !hasFieldTerms( pattern_->text() ) ) {
        notes.append( tr( "Boolean combining is not kept: the filter searches for the pattern "
                          "as one string and will not find what the Search Line found." ) );
    }
    unkeptNote_->setText( notes.join( u'\n' ) );
    layout_->form()->setRowVisible( unkeptNote_, !notes.isEmpty() );
    // Editing the pattern may show the note while the dialog is open: it
    // grows to show it whole.
    if ( isVisible() && height() < sizeHint().height() ) {
        resize( width(), sizeHint().height() );
    }
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
    else if ( !group_->warning().isEmpty() ) {
        warning = group_->warning();
    }
    else {
        warning = highlighterWarning();
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
    const auto* before = groups_->chosen();
    auto group = before != nullptr ? *before : PredefinedFilterSet::createNewSet( chosen.name );
    group.addFilter( added );
    auto requests = groups_->put( group );
    // The Highlighter is added as Highlight This adds one; a Team
    // Highlighter Set is published in the same requests as the filter's Team
    // group, whichever of them is the Team's.
    if ( alsoHighlights() ) {
        requests.append( addHighlighter() );
    }

    // Published as the Predefined Filters dialog publishes a Team group:
    // what changed against the group as given, based on its revision.
    if ( !requests.isEmpty() ) {
        Q_EMIT publishRequested( requests );
    }
    if ( chosen.section != GroupChooser::Section::Team ) {
        auto& collection = PredefinedFiltersCollection::get();
        if ( chosen.isNew() ) {
            auto groups = collection.filterSets();
            groups.append( group );
            collection.setFilterSets( groups );
        }
        else {
            collection.addFilter( chosen.id, added );
        }
        collection.save();
    }
    QDialog::accept();
}

QList<logsquirl::teamfolder::PublishRequest> AddPredefinedFilterDialog::addHighlighter()
{
    const auto chosen = highlighterSet_->choice();
    const auto* before = highlighterSets_->chosen();
    auto set = before != nullptr ? *before : HighlighterSet::createNewSet( chosen.name );
    set.addHighlighter( highlighter() );
    const auto requests = highlighterSets_->put( set );

    auto& collection = HighlighterSetCollection::get();
    if ( chosen.section == GroupChooser::Section::Team ) {
        // The Team Folder has the set once it is published and synced; until
        // then the collection has it as the dialog made it, so that it colors
        // at once and can be active. The next sync replaces it.
        collection.setTeamHighlighterSets( highlighterSets_->editableTeam(), false );
    }
    else {
        // The sets as stored now, not as read when the dialog opened: the
        // Highlighter is added to the set as it is. A set that has gone
        // meanwhile is made again, so that the Highlighter is not lost.
        auto sets = collection.highlighterSets();
        const auto stored = std::find_if( sets.begin(), sets.end(), [ &set ]( const auto& other ) {
            return other.id() == set.id();
        } );
        if ( stored != sets.end() ) {
            stored->addHighlighter( highlighter() );
        }
        else {
            sets.append( set );
        }
        collection.setHighlighterSets( sets );
    }
    // What is added shows at once: the set is active.
    if ( !collection.activeSetIds().contains( set.id() ) ) {
        collection.activateSet( set.id() );
    }
    collection.save();
    return requests;
}
