/*
 * Copyright (C) 2009, 2010, 2011, 2013 Nicolas Bonnefon and other contributors
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
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
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

#include <QClipboard>
#include <QColorDialog>
#include <QDesktopServices>
#include <QFontDatabase>
#include <QKeySequenceEdit>
#include <QMessageBox>
#include <QToolButton>
#include <QtGui>

#include "encodings.h"
#include "fileassociationchoice.h"
#include "filetypechoices.h"
#include "fontutils.h"
#include "highlighteredit.h"
#include "installoptout.h"
#include "log.h"
#include "logformatcatalog.h"
#include "mainwindow.h"
#include "recentfiles.h"
#include "savedsearches.h"
#include "shortcuts.h"
#include "theme.h"

#include "optionsdialog.h"

static constexpr int PollIntervalMin = 10;
static constexpr int PollIntervalMax = 3600000;

// Constructor
OptionsDialog::OptionsDialog( const LogFormatCatalog& logFormatCatalog, QWidget* parent )
    : QDialog( parent )
{
    setupUi( this );

    setupTabs();
    setupFontList();
    setupRegexp();
    setupStyles();
    setupEncodings();
    setupLanguageList();

    // Validators
    QValidator* pollingIntervalValidator = new QIntValidator( PollIntervalMin, PollIntervalMax );
    pollIntervalLineEdit->setValidator( pollingIntervalValidator );

    connect( buttonBox, &QDialogButtonBox::clicked, this, &OptionsDialog::onButtonBoxClicked );
    connect( fontFamilyBox, &QComboBox::currentTextChanged, this, &OptionsDialog::updateFontSize );
    connect( pollingCheckBox, &QCheckBox::toggled, [ this ]( auto ) { this->setupPolling(); } );
    connect( searchResultsCacheCheckBox, &QCheckBox::toggled,
             [ this ]( auto ) { this->setupSearchResultsCache(); } );
    connect( loggingCheckBox, &QCheckBox::toggled, [ this ]( auto ) { this->setupLogging(); } );
    connect( indexCacheCheckBox, &QCheckBox::toggled,
             [ this ]( auto ) { this->setupIndexCache(); } );

    connect( extractArchivesCheckBox, &QCheckBox::toggled,
             [ this ]( auto ) { this->setupArchives(); } );

    connect( teamFolderCheckBox, &QCheckBox::toggled,
             [ this ]( auto ) { this->setupTeamFolder(); } );
    connect( teamFolderUrlEdit, &QLineEdit::textChanged, this,
             &OptionsDialog::updateTeamFolderStatus );

    // Beta checkbox is only enabled when version checking is on
    connect( checkForNewVersionCheckBox, &QCheckBox::toggled, checkForBetaVersionCheckBox,
             &QWidget::setEnabled );

    connect( mainSearchColorButton, &QPushButton::clicked, this, &OptionsDialog::changeMainColor );
    connect( quickFindColorButton, &QPushButton::clicked, this, &OptionsDialog::changeQfColor );

    auto shortcutRecorder = new ShortcutRecordingDelegate( shortcutsTable );
    shortcutsTable->setItemDelegateForColumn( 1, shortcutRecorder );
    shortcutsTable->setItemDelegateForColumn( 2, shortcutRecorder );
    connect( shortcutRecorder, &ShortcutRecordingDelegate::edited, this,
             &OptionsDialog::checkShortcutsOnDuplicate );

    connect( restoreShortcutsDefaults, &QPushButton::clicked, this, [ this ]() {
        auto ret = QMessageBox::question(
            this, "Restore Default Shortcuts", "Do you want to restore default shortcuts?",
            QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel );
        if ( ret == QMessageBox::Yes )
            buildShortcutsTable( true );
    } );

    // Auto-show table view is only meaningful when auto-detect is enabled
    connect( autoDetectLogFormatsCheckBox, &QCheckBox::toggled, autoShowTableViewCheckBox,
             &QWidget::setEnabled );

    updateDialogFromConfig();

    // Sync enabled state after config has been loaded
    autoShowTableViewCheckBox->setEnabled( autoDetectLogFormatsCheckBox->isChecked() );

    setupPolling();
    setupSearchResultsCache();
    setupLogging();
    setupArchives();
    setupIndexCache();
    setupTeamFolderStatus();
    setupTeamFolder();
    setupLogFormats( logFormatCatalog );
    setupFileAssociations();
}

//
// Private functions
//

// Setups the tabs depending on the configuration
void OptionsDialog::setupTabs()
{
#ifndef Q_OS_WIN
    keepFileClosedCheckBox->setVisible( false );
#endif

#ifdef Q_OS_MAC
    minimizeToTrayCheckBox->setVisible( false );
#endif

#ifndef LOGSQUIRL_HAS_HS
    regexpEngineLabel->setVisible( false );
    regexpEngineComboBox->setVisible( false );
#endif
}

// Populates the 'family' ComboBox
void OptionsDialog::setupFontList()
{
    const auto families = FontUtils::availableFonts();
    for ( const QString& str : families ) {
        fontFamilyBox->addItem( str );
    }
}

// Populate the regexp ComboBoxes
void OptionsDialog::setupRegexp()
{
    QStringList regexpTypes;
    regexpTypes << tr( "Extended Regexp" ) << tr( "Fixed Strings" );

    mainSearchBox->addItems( regexpTypes );
    quickFindSearchBox->addItems( regexpTypes );

    QStringList regexpEngines;
    regexpEngines << tr( "Vectorscan" ) << tr( "Qt" );

    regexpEngineComboBox->addItems( regexpEngines );
}

void OptionsDialog::setupStyles()
{
    styleComboBox->addItems( Theme::availableThemes() );
}

void OptionsDialog::setupEncodings()
{
    const auto availableEncodings = EncodingMenu::supportedEncodings();
    encodingComboBox->addItem( "Auto", -1 );

    std::map<QString, int> allMibs;

    for ( const auto& group : availableEncodings ) {
        for ( const auto& mib : group.second ) {
            auto codec = TextEncoding::forMib( mib );
            if ( codec ) {
                allMibs.emplace( codec->name(), mib );
            }
        }
    }

    for ( const auto& codec : allMibs ) {
        encodingComboBox->addItem( codec.first, codec.second );
    }
}

void OptionsDialog::setupLanguageList()
{
    QResource resource( ":/i18n/Languages.xml" );
    QByteArray bytes( reinterpret_cast<const char*>( resource.data() ), (int)resource.size() );
    QXmlStreamReader xml( bytes );

    while ( !xml.atEnd() ) {
        QXmlStreamReader::TokenType token = xml.readNext();
        if ( xml.hasError() ) {
            LOG_ERROR << "load language error";
            return;
        }

        if ( xml.name() == QString( "language" ) && token == QXmlStreamReader::StartElement ) {
            QXmlStreamAttributes attributes = xml.attributes();
            languageComboBox->addItem( attributes.value( "name" ).toString(),
                                       attributes.value( "ietfCode" ).toString() );
        }
    }
}

void OptionsDialog::setupPolling()
{
    pollIntervalLineEdit->setEnabled( pollingCheckBox->isChecked() );
}

void OptionsDialog::setupSearchResultsCache()
{
    searchCacheSpinBox->setEnabled( searchResultsCacheCheckBox->isChecked() );
}

void OptionsDialog::setupLogging()
{
    verbositySpinBox->setEnabled( loggingCheckBox->isChecked() );
}

void OptionsDialog::setupArchives()
{
    extractArchivesAlwaysCheckBox->setEnabled( extractArchivesCheckBox->isChecked() );
}

void OptionsDialog::setupIndexCache()
{
    indexCacheMaxSizeSpinBox->setEnabled( indexCacheCheckBox->isChecked() );
}

void OptionsDialog::setupTeamFolder()
{
    const auto teamFolderOn = teamFolderCheckBox->isChecked();
    teamFolderUrlEdit->setEnabled( teamFolderOn );
    teamFolderSubfolderEdit->setEnabled( teamFolderOn );
    updateTeamFolderStatus();
}

void OptionsDialog::setupTeamFolderStatus()
{
    auto headingFont = teamFolderStatusHeadingLabel->font();
    headingFont.setBold( true );
    teamFolderStatusHeadingLabel->setFont( headingFont );

    // Git's output as Git wrote it: its own line breaks, none added.
    teamFolderDetailsEdit->setFont( QFontDatabase::systemFont( QFontDatabase::FixedFont ) );
    teamFolderDetailsEdit->setVisible( false );
    connect( teamFolderDetailsButton, &QToolButton::toggled, this, [ this ]( bool expanded ) {
        teamFolderDetailsButton->setArrowType( expanded ? Qt::DownArrow : Qt::RightArrow );
        updateTeamFolderStatus();
    } );
    connect( teamFolderCopyDetailsButton, &QPushButton::clicked, this, [ this ] {
        QGuiApplication::clipboard()->setText( teamFolderDetailsEdit->toPlainText() );
    } );
    connect( teamFolderOpenFolderButton, &QPushButton::clicked, this, [ this ] {
        if ( teamFolder_ && teamFolder_->hasClone() ) {
            QDesktopServices::openUrl( QUrl::fromLocalFile( teamFolder_->cloneDirectory() ) );
        }
    } );

    // The note explains; it stays out of the way of the settings and the
    // status.
    auto noteFont = teamFolderNoteLabel->font();
    noteFont.setPointSizeF( noteFont.pointSizeF() * 0.9 );
    teamFolderNoteLabel->setFont( noteFont );
    teamFolderNoteLabel->setForegroundRole( QPalette::PlaceholderText );

    teamFolderStatusGroup->setVisible( false );
}

void OptionsDialog::showTeamFolder( TeamFolder& teamFolder )
{
    teamFolder_ = &teamFolder;
    connect( &teamFolder, &TeamFolder::stateChanged, this, &OptionsDialog::updateTeamFolderStatus );
    connect( teamFolderSyncButton, &QPushButton::clicked, this, &OptionsDialog::syncTeamFolderNow );
    updateTeamFolderStatus();
}

TeamFolderPolicy OptionsDialog::teamFolderPolicyOfFields() const
{
    return TeamFolderPolicy{ .enabled = teamFolderCheckBox->isChecked(),
                             .repositoryUrl = teamFolderUrlEdit->text().trimmed(),
                             .subfolder = teamFolderSubfolderEdit->text().trimmed() };
}

void OptionsDialog::syncTeamFolderNow()
{
    if ( !teamFolder_ ) {
        return;
    }
    // Sync Now is the connection test: it syncs the repository the fields
    // show. Fields that differ from the applied ones are applied first, the
    // Team Folder settings only and for good: Cancel does not undo them, and
    // the other tabs keep waiting for OK or Apply.
    const auto policy = teamFolderPolicyOfFields();
    auto& config = Configuration::get();
    const TeamFolderPolicy applied{ .enabled = config.teamFolderEnabled(),
                                    .repositoryUrl = config.teamFolderUrl(),
                                    .subfolder = config.teamFolderSubfolder() };
    if ( policy != applied ) {
        config.setTeamFolderEnabled( policy.enabled );
        config.setTeamFolderUrl( policy.repositoryUrl );
        config.setTeamFolderSubfolder( policy.subfolder );
        config.save();
        // A changed repository syncs as it is set up.
        teamFolder_->setUp( policy );
        Q_EMIT optionsChanged();
        if ( !teamFolder_ || teamFolder_->isSyncing() ) {
            return;
        }
    }
    teamFolder_->sync();
}

namespace {

// The standard icon of the Team Folder's status: one for each state, and a
// warning for Team groups that are current but read-only.
QStyle::StandardPixmap statusIconOf( const TeamFolder& teamFolder )
{
    if ( teamFolder.isSyncing() ) {
        return QStyle::SP_BrowserReload;
    }
    switch ( teamFolder.state() ) {
    case TeamFolder::State::Off:
        return QStyle::SP_MediaStop;
    case TeamFolder::State::NotSynced:
        return QStyle::SP_MessageBoxWarning;
    case TeamFolder::State::Synced:
        return teamFolder.failedStep() == logsquirl::teamfolder::SyncStep::None
                   ? QStyle::SP_DialogApplyButton
                   : QStyle::SP_MessageBoxWarning;
    case TeamFolder::State::Error:
        return QStyle::SP_MessageBoxCritical;
    }
    return QStyle::SP_MessageBoxInformation;
}

} // namespace

void OptionsDialog::updateTeamFolderStatus()
{
    // What the Team Folder does now, as it was last applied; Sync Now applies
    // what the fields show first.
    teamFolderStatusGroup->setVisible( teamFolder_ != nullptr );
    if ( !teamFolder_ ) {
        return;
    }

    const auto iconSize = style()->pixelMetric( QStyle::PM_SmallIconSize, nullptr, this );
    teamFolderStatusIconLabel->setPixmap(
        style()
            ->standardIcon( statusIconOf( *teamFolder_ ), nullptr, this )
            .pixmap( QSize( iconSize, iconSize ), devicePixelRatioF() ) );
    teamFolderStatusHeadingLabel->setText( teamFolder_->heading() );

    // What to do about a common failure, under the heading; Git's output
    // stays as it is in the details.
    const auto hint
        = teamFolder_->isSyncing() ? QString{} : TeamFolder::hintOf( teamFolder_->failureHint() );
    teamFolderHintLabel->setText( hint );
    teamFolderHintLabel->setVisible( !hint.isEmpty() );

    const auto remarks = teamFolder_->remarks();
    teamFolderRemarksLabel->setText( remarks.join( QLatin1Char( '\n' ) ) );
    teamFolderRemarksLabel->setVisible( !remarks.isEmpty() );

    // Whether the Team groups were ever current, in the user's locale.
    const auto lastSynced = teamFolder_->lastSynced();
    teamFolderLastSyncedLabel->setText(
        tr( "Last synced: %1" )
            .arg( lastSynced.isValid()
                      ? QLocale{}.toString( lastSynced.toLocalTime(), QLocale::ShortFormat )
                      : tr( "never" ) ) );
    teamFolderLastSyncedLabel->setVisible( teamFolder_->state() != TeamFolder::State::Off );
    teamFolderOpenFolderButton->setEnabled( teamFolder_->hasClone() );

    // Git's output, untranslated (ADR-0008), of the last sync that failed.
    const auto gitOutput = teamFolder_->isSyncing() ? QString{} : teamFolder_->gitOutput();
    if ( teamFolderDetailsEdit->toPlainText() != gitOutput ) {
        teamFolderDetailsEdit->setPlainText( gitOutput );
    }
    const bool hasDetails = !gitOutput.isEmpty();
    teamFolderDetailsHeader->setVisible( hasDetails );
    teamFolderDetailsEdit->setVisible( hasDetails && teamFolderDetailsButton->isChecked() );

    teamFolderSyncButton->setEnabled( teamFolderPolicyOfFields().isActive()
                                      && !teamFolder_->isSyncing() );
}

// Populate the Log Formats tab from the application's Log Format Catalog
// and wire the "Open Formats Folder" button.
void OptionsDialog::setupLogFormats( const LogFormatCatalog& logFormatCatalog )
{
    formatsTreeWidget->clear();
    auto names = logFormatCatalog.formatNames();
    names.sort( Qt::CaseInsensitive );

    for ( const auto& name : names ) {
        const auto fmt = logFormatCatalog.formatByName( name );
        if ( !fmt ) {
            continue;
        }
        auto* item = new QTreeWidgetItem( formatsTreeWidget );
        const auto title = fmt->title().isEmpty() ? name : fmt->title();
        item->setText( 0, title );
        item->setText( 1, fmt->description() );
        item->setToolTip( 0, name );
    }

    formatsTreeWidget->resizeColumnToContents( 0 );

    connect( openFormatsFolderButton, &QPushButton::clicked, this, []() {
        const auto formatsDir = LogFormatCatalog::defaultUserFormatsDirectory();
        QDir().mkpath( formatsDir );
        QDesktopServices::openUrl( QUrl::fromLocalFile( formatsDir ) );
    } );
}

// The File Associations page lists the file types the user chooses from,
// grouped as the list groups them (#720). It stays hidden until it is given
// the file associations to show.
void OptionsDialog::setupFileAssociations()
{
    tabWidget->setTabVisible( tabWidget->indexOf( fileAssociationsTab ), false );
    fileAssociationsUnavailable->setVisible( false );
    fileAssociationsNoteLabel->setVisible( false );
    fileAssociationsContextMenuCheckBox->setVisible( false );

    FileTypeChoices::fill( *fileAssociationsTree );
}

void OptionsDialog::showFileAssociations( FileAssociations& fileAssociations )
{
    fileAssociations_ = &fileAssociations;
    tabWidget->setTabVisible( tabWidget->indexOf( fileAssociationsTab ), true );

    const auto available = fileAssociations.isAvailable();
    fileAssociationsTree->setEnabled( available );
    fileAssociationsUnavailable->setVisible( !available );
    if ( !available ) {
        const auto iconSize = style()->pixelMetric( QStyle::PM_SmallIconSize, nullptr, this );
        fileAssociationsUnavailableIconLabel->setPixmap(
            style()
                ->standardIcon( QStyle::SP_MessageBoxInformation, nullptr, this )
                .pixmap( QSize( iconSize, iconSize ), devicePixelRatioF() ) );
        fileAssociationsUnavailableLabel->setText( fileAssociations.unavailableReason() );
    }

    // Explorer's entry for every file, on Windows (#724).
    fileAssociationsContextMenuCheckBox->setVisible( available
                                                     && fileAssociations.offersContextMenuEntry() );

    const auto note = available ? fileAssociations.applyNote() : QString{};
    fileAssociationsNoteLabel->setText( note );
    fileAssociationsNoteLabel->setVisible( !note.isEmpty() );

    // A platform where the user confirms outside LogSquirl tells the outcome
    // later; the page then shows it, and the checks follow.
    connect( &fileAssociations, &FileAssociations::statesChanged, this,
             &OptionsDialog::updateFileAssociations );
    updateFileAssociations();
}

// Reads the states again and shows them; every check becomes what its state
// is.
void OptionsDialog::updateFileAssociations()
{
    if ( !fileAssociations_ || !fileAssociations_->isAvailable() ) {
        fileAssociationStates_.clear();
        return;
    }
    fileAssociationStates_ = fileAssociations_->states();
    fileAssociationsContextMenuCheckBox->setChecked( fileAssociations_->hasContextMenuEntry() );

    for ( const auto& [ id, state ] : fileAssociationStates_ ) {
        auto* row = FileTypeChoices::row( *fileAssociationsTree, id );
        if ( row == nullptr ) {
            continue;
        }
        row->setCheckState( 0, state == FileAssociationState::Default ? Qt::Checked
                                                                      : Qt::Unchecked );
        FileTypeChoices::showState( *row, 2, state, *this );
    }
    fileAssociationsTree->resizeColumnToContents( 2 );
}

// Makes LogSquirl the default for each checked type it is not the default for
// yet, and gives back each unchecked one it is the default for.
void OptionsDialog::applyFileAssociations()
{
    if ( !fileAssociations_ || !fileAssociations_->isAvailable() ) {
        return;
    }

    const auto checkedIds = FileTypeChoices::checkedIds( *fileAssociationsTree );

    QStringList errors;
    const auto plan = FileAssociationPlan::of( fileAssociationStates_, checkedIds );
    const auto entryChanged = fileAssociations_->offersContextMenuEntry()
                              && fileAssociationsContextMenuCheckBox->isChecked()
                                     != fileAssociations_->hasContextMenuEntry();
    if ( plan.isEmpty() && !entryChanged ) {
        return;
    }
    if ( entryChanged ) {
        const auto result = fileAssociations_->setContextMenuEntry(
            fileAssociationsContextMenuCheckBox->isChecked() );
        if ( !result.succeeded() ) {
            errors << result.error;
        }
    }
    if ( const auto result = plan.applyWith( *fileAssociations_ ) ) {
        // The choice is kept, so the first start asks no more (#723).
        auto& config = Configuration::get();
        auto choice = FileAssociationChoice::of( config );
        choice.apply( checkedIds );
        choice.keepIn( config );

        if ( !result->succeeded() ) {
            errors << result->error;
        }
    }
    // What the system says now, not what was asked for.
    updateFileAssociations();
    if ( !errors.isEmpty() ) {
        QMessageBox::warning( this, tr( "File Associations" ), errors.join( QLatin1Char( '\n' ) ) );
    }
}

// Convert a regexp type to its index in the list
int OptionsDialog::getRegexpTypeIndex( SearchRegexpType syntax ) const
{
    int index;

    switch ( syntax ) {
    case SearchRegexpType::FixedString:
        index = 1;
        break;
    default:
        index = 0;
        break;
    }

    return index;
}

// Convert the index of a regexp type to its type
SearchRegexpType OptionsDialog::getRegexpTypeFromIndex( int index ) const
{
    SearchRegexpType type;

    switch ( index ) {
    case 1:
        type = SearchRegexpType::FixedString;
        break;
    default:
        type = SearchRegexpType::ExtendedRegexp;
        break;
    }

    return type;
}

int OptionsDialog::getRegexpEngineIndex( RegexpEngine engine ) const
{
    int index;

    switch ( engine ) {
    case RegexpEngine::QRegularExpression:
        index = 1;
        break;
    default:
        index = 0;
        break;
    }

    return index;
}

RegexpEngine OptionsDialog::getRegexpEngineFromIndex( int index ) const
{
    RegexpEngine type;

    switch ( index ) {
    case 1:
        type = RegexpEngine::QRegularExpression;
        break;
    default:
        type = RegexpEngine::Vectorscan;
        break;
    }

    return type;
}

// Updates the dialog box using values in global Config()
void OptionsDialog::updateDialogFromConfig()
{
    const auto& config = Configuration::get();

    // Main font
    QFontInfo fontInfo = QFontInfo( config.mainFont() );

    int familyIndex = fontFamilyBox->findText( fontInfo.family() );
    if ( familyIndex != -1 )
        fontFamilyBox->setCurrentIndex( familyIndex );

    updateFontSize( fontInfo.family() );

    int sizeIndex = fontSizeBox->findText( QString::number( fontInfo.pointSize() ) );
    if ( sizeIndex != -1 )
        fontSizeBox->setCurrentIndex( sizeIndex );

    fontSmoothCheckBox->setChecked( config.forceFontAntialiasing() );
    boldFontCheckBox->setChecked( config.useBoldFont() );
    wrapTextCheckBox->setChecked( config.useTextWrap() );
    showValueNamesCheckBox->setChecked( config.showValueNames() );
    enableQtHiDpiCheckBox->setChecked( config.enableQtHighDpi() );
    scaleRoundingComboBox->setCurrentIndex( config.scaleFactorRounding() - 1 );

    // Language
    auto langIdx = languageComboBox->findData( { config.language() } );
    if ( langIdx == -1 ) {
        langIdx = 0;
    }
    languageComboBox->setCurrentIndex( langIdx );

    const auto style = config.style();
    if ( !styleComboBox->findText( style, Qt::MatchExactly ) ) {
        styleComboBox->setCurrentIndex( 0 );
    }
    else {
        styleComboBox->setCurrentText( style );
    }

    // The items are in the order of the values.
    ansiColorSequencesComboBox->setCurrentIndex( static_cast<int>( config.ansiColorSequences() ) );

    contextLinesSpinBox->setValue( config.contextLinesCount() );

    // Regexp types
    mainSearchBox->setCurrentIndex( getRegexpTypeIndex( config.mainRegexpType() ) );
    mainSearchColor_ = config.mainSearchBackColor();
    HighlighterEdit::updateIcon( mainSearchColorButton, mainSearchColor_ );
    quickFindSearchBox->setCurrentIndex( getRegexpTypeIndex( config.quickfindRegexpType() ) );
    qfSearchColor_ = config.qfBackColor();
    HighlighterEdit::updateIcon( quickFindColorButton, qfSearchColor_ );
    regexpEngineComboBox->setCurrentIndex( getRegexpEngineIndex( config.regexpEngine() ) );
    autoRunSearchOnAddCheckBox->setChecked( config.autoRunSearchOnPatternChange() );

    highlightMainSearchCheckBox->setChecked( config.mainSearchHighlight() );
    variateHighlightCheckBox->setChecked( config.variateMainSearchHighlight() );
    incrementalCheckBox->setChecked( config.isQuickfindIncremental() );
    caseSensitiveCheckBox->setChecked( !config.isSearchIgnoreCaseDefault() );
    logicalCombiningCheckBox->setChecked( config.isSearchLogicalCombiningDefault() );
    autoRefreshCheckBox->setChecked( config.isSearchAutoRefreshDefault() );

    // Polling
    nativeFileWatchCheckBox->setChecked( config.nativeFileWatchEnabled() );
    fastModificationDetectionCheckBox->setChecked( config.fastModificationDetection() );
    pollingCheckBox->setChecked( config.pollingEnabled() );
    pollIntervalLineEdit->setText( QString::number( config.pollIntervalMs() ) );
    allowFollowOnScrollCheckBox->setChecked( config.allowFollowOnScroll() );

    fastScrollEnabledCheckBox->setChecked( config.fastScrollEnabled() );
    fastScrollMultiplierSpinBox->setValue( config.fastScrollMultiplier() );

    // Last session
    loadLastSessionCheckBox->setChecked( config.loadLastSession() );
    followFileOnLoadCheckBox->setChecked( config.followFileOnLoad() );
    minimizeToTrayCheckBox->setChecked( config.minimizeToTray() );
    multipleWindowsCheckBox->setChecked( config.allowMultipleWindows() );
    confirmTabCloseCheckBox->setChecked( config.confirmTabClose() );
    showSplashScreenCheckBox->setChecked( config.showSplashScreen() );
    showDashboardCheckBox->setChecked( config.showDashboard() );
    autoDetectLogFormatsCheckBox->setChecked( config.autoDetectLogFormats() );
    autoShowTableViewCheckBox->setChecked( config.autoShowTableView() );

    loggingCheckBox->setChecked( config.enableLogging() );
    verbositySpinBox->setValue( config.loggingLevel() );

    extractArchivesCheckBox->setChecked( config.extractArchives() );
    extractArchivesAlwaysCheckBox->setChecked( config.extractArchivesAlways() );

    // Perf
    parallelSearchCheckBox->setChecked( config.useParallelSearch() );
    searchResultsCacheCheckBox->setChecked( config.useSearchResultsCache() );
    searchCacheSpinBox->setValue( static_cast<int>( config.searchResultsCacheLines() ) );
    indexReadBufferSpinBox->setValue( config.indexReadBufferSizeMb() );
    searchReadBufferSpinBox->setValue( config.searchReadBufferSizeLines() );
    keepFileClosedCheckBox->setChecked( config.keepFileClosed() );
    compressedIndexCheckBox->setChecked( config.useCompressedIndex() );
    indexCacheCheckBox->setChecked( config.useIndexCache() );
    indexCacheMaxSizeSpinBox->setValue( config.indexCacheMaxSizeMb() );
    optimizeForNotLatinEncodingsCheckBox->setChecked( config.optimizeForNotLatinEncodings() );

    // version checking
    checkForNewVersionCheckBox->setChecked( config.versionCheckingEnabled() );
    checkForBetaVersionCheckBox->setChecked( config.betaVersionCheckingEnabled() );
    checkForBetaVersionCheckBox->setEnabled( config.versionCheckingEnabled() );
    // Turned off for the whole installation by the installer, which the
    // settings cannot turn back on (#445).
    if ( logsquirl::versioncheck::updateCheckTurnedOffAtInstall() ) {
        checkForNewVersionCheckBox->setChecked( false );
        checkForNewVersionCheckBox->setEnabled( false );
        checkForNewVersionCheckBox->setToolTip(
            tr( "Turned off when LogSquirl was installed. Run the installer again to turn it "
                "back on." ) );
        checkForBetaVersionCheckBox->setEnabled( false );
    }

    // downloads
    verifySslCheckBox->setChecked( config.verifySslPeers() );

    teamFolderCheckBox->setChecked( config.teamFolderEnabled() );
    teamFolderUrlEdit->setText( config.teamFolderUrl() );
    teamFolderSubfolderEdit->setText( config.teamFolderSubfolder() );

    const auto encodingIndex = encodingComboBox->findData( config.defaultEncodingMib() );
    encodingComboBox->setCurrentIndex( encodingIndex < 0 ? 0 : encodingIndex );

    buildShortcutsTable( false );

    const auto& savedSearches = SavedSearches::get();
    searchHistorySpinBox->setValue( savedSearches.historySize() );

    const auto& recentFiles = RecentFiles::get();
    filesHistoryMaxItemsSpinBox->setMinimum( 1 );
    filesHistoryMaxItemsSpinBox->setMaximum( MAX_RECENT_FILES );
    filesHistoryMaxItemsSpinBox->setValue( recentFiles.filesHistoryMaxItems() );
}

//
// Q_SLOTS:
//

void OptionsDialog::updateFontSize( const QString& fontFamily )
{
    QString oldFontSize = fontSizeBox->currentText();
    const auto sizes = FontUtils::availableFontSizes( fontFamily );

    fontSizeBox->clear();
    for ( int size : sizes ) {
        fontSizeBox->addItem( QString::number( size ) );
    }
    // Now restore the size we had before
    int i = fontSizeBox->findText( oldFontSize );
    if ( i != -1 )
        fontSizeBox->setCurrentIndex( i );
}

void OptionsDialog::changeMainColor()
{
    QColor newColor;
    if ( HighlighterEdit::showColorPicker( mainSearchColor_, newColor ) ) {
        mainSearchColor_ = newColor;
        HighlighterEdit::updateIcon( mainSearchColorButton, mainSearchColor_ );
    }
}

void OptionsDialog::changeQfColor()
{
    QColor newColor;
    if ( HighlighterEdit::showColorPicker( qfSearchColor_, newColor ) ) {
        qfSearchColor_ = newColor;
        HighlighterEdit::updateIcon( quickFindColorButton, qfSearchColor_ );
    }
}

void OptionsDialog::checkShortcutsOnDuplicate() const
{
    static constexpr int PRIMARY_COL = 1;
    static constexpr int SECONDARY_COL = 2;

    if ( !shortcutsTable->rowCount() ) {
        return;
    }

    // A conflict is marked in the Theme's error colors; an item without its
    // own brushes shows the table's.
    const auto markConflict = []( QTableWidgetItem* item ) {
        const Theme& theme = Theme::active();
        item->setBackground( theme.color( ColorToken::ErrorBackground ) );
        item->setForeground( theme.color( ColorToken::ErrorText ) );
    };

    for ( auto shortcutRow = 0; shortcutRow < shortcutsTable->rowCount(); ++shortcutRow ) {
        for ( const auto column : { PRIMARY_COL, SECONDARY_COL } ) {
            shortcutsTable->item( shortcutRow, column )->setBackground( QBrush{} );
            shortcutsTable->item( shortcutRow, column )->setForeground( QBrush{} );
        }
    }

    std::unordered_map<std::string, std::pair<int, int>> uniqueShortcuts;
    bool hasDuplicateShortcuts = false;
    for ( auto shortcutRow = 0; shortcutRow < shortcutsTable->rowCount(); ++shortcutRow ) {

        auto hasDuplicates = [ &uniqueShortcuts, &markConflict, shortcutRow, this ]( int ncol ) {
            auto keySequence
                = shortcutsTable->item( shortcutRow, ncol )->data( Qt::UserRole ).toString();

            if ( !keySequence.isEmpty() ) {
                if ( auto it = uniqueShortcuts.find( keySequence.toStdString() );
                     it != uniqueShortcuts.end() ) {

                    markConflict( shortcutsTable->item( it->second.first, it->second.second ) );
                    markConflict( shortcutsTable->item( shortcutRow, ncol ) );

                    return true;
                }

                uniqueShortcuts.try_emplace( keySequence.toStdString(),
                                             std::make_pair( shortcutRow, ncol ) );
            }

            return false;
        };

        if ( hasDuplicates( PRIMARY_COL ) || hasDuplicates( SECONDARY_COL ) ) {
            hasDuplicateShortcuts = true;
        }
    }

    buttonBox->button( QDialogButtonBox::Ok )->setEnabled( !hasDuplicateShortcuts );
    buttonBox->button( QDialogButtonBox::Apply )->setEnabled( !hasDuplicateShortcuts );
}

int OptionsDialog::updateTranslate()
{
    // Without a main window there is no user interface to translate.
    auto mw = dynamic_cast<MainWindow*>( parent() );
    return mw ? mw->installLanguage( languageComboBox->currentData().toString() ) : 0;
}

void OptionsDialog::updateConfigFromDialog()
{
    bool restartAppMessage = false;
    auto& config = Configuration::get();

    QFont font = QFont( fontFamilyBox->currentText(), ( fontSizeBox->currentText() ).toInt() );
    config.setMainFont( font );
    config.setForceFontAntialiasing( fontSmoothCheckBox->isChecked() );
    config.setUseBoldFont( boldFontCheckBox->isChecked() );
    config.setUseTextWrap( wrapTextCheckBox->isChecked() );
    config.setShowValueNames( showValueNamesCheckBox->isChecked() );
    config.setEnableQtHighDpi( enableQtHiDpiCheckBox->isChecked() );
    config.setScaleFactorRounding( scaleRoundingComboBox->currentIndex() + 1 );

    config.setMainRegexpType( getRegexpTypeFromIndex( mainSearchBox->currentIndex() ) );
    config.setMainSearchBackColor( mainSearchColor_ );
    config.setEnableMainSearchHighlight( highlightMainSearchCheckBox->isChecked() );
    config.setVariateMainSearchHighlight( variateHighlightCheckBox->isChecked() );
    config.setSearchIgnoreCaseDefault( !caseSensitiveCheckBox->isChecked() );
    config.setSearchAutoRefreshDefault( autoRefreshCheckBox->isChecked() );
    config.setSearchLogicalCombiningDefault( logicalCombiningCheckBox->isChecked() );
    config.setQuickfindRegexpType( getRegexpTypeFromIndex( quickFindSearchBox->currentIndex() ) );
    config.setQfBackColor( qfSearchColor_ );
    config.setQuickfindIncremental( incrementalCheckBox->isChecked() );
    config.setRegexpEngine( getRegexpEngineFromIndex( regexpEngineComboBox->currentIndex() ) );
    config.setAutoRunSearchOnPatternChange( autoRunSearchOnAddCheckBox->isChecked() );

    config.setNativeFileWatchEnabled( nativeFileWatchCheckBox->isChecked() );
    config.setPollingEnabled( pollingCheckBox->isChecked() );
    auto pollInterval = pollIntervalLineEdit->text().toInt();
    if ( pollInterval < PollIntervalMin )
        pollInterval = PollIntervalMin;
    else if ( pollInterval > PollIntervalMax )
        pollInterval = PollIntervalMax;

    config.setPollIntervalMs( pollInterval );
    config.setFastModificationDetection( fastModificationDetectionCheckBox->isChecked() );
    config.setAllowFollowOnScroll( allowFollowOnScrollCheckBox->isChecked() );

    config.setFastScrollEnabled( fastScrollEnabledCheckBox->isChecked() );
    config.setFastScrollMultiplier( fastScrollMultiplierSpinBox->value() );

    config.setLoadLastSession( loadLastSessionCheckBox->isChecked() );
    config.setFollowFileOnLoad( followFileOnLoadCheckBox->isChecked() );
    config.setAllowMultipleWindows( multipleWindowsCheckBox->isChecked() );
    config.setConfirmTabClose( confirmTabCloseCheckBox->isChecked() );
    config.setMinimizeToTray( minimizeToTrayCheckBox->isChecked() );
    config.setShowSplashScreen( showSplashScreenCheckBox->isChecked() );
    config.setShowDashboard( showDashboardCheckBox->isChecked() );
    config.setAutoDetectLogFormats( autoDetectLogFormatsCheckBox->isChecked() );
    config.setAutoShowTableView( autoShowTableViewCheckBox->isChecked() );
    config.setEnableLogging( loggingCheckBox->isChecked() );
    config.setLoggingLevel( verbositySpinBox->value() );

    config.setExtractArchives( extractArchivesCheckBox->isChecked() );
    config.setExtractArchivesAlways( extractArchivesAlwaysCheckBox->isChecked() );

    config.setUseParallelSearch( parallelSearchCheckBox->isChecked() );
    config.setUseSearchResultsCache( searchResultsCacheCheckBox->isChecked() );
    config.setSearchResultsCacheLines( static_cast<unsigned>( searchCacheSpinBox->value() ) );
    config.setIndexReadBufferSizeMb( indexReadBufferSpinBox->value() );
    config.setSearchReadBufferSizeLines( searchReadBufferSpinBox->value() );
    config.setKeepFileClosed( keepFileClosedCheckBox->isChecked() );
    config.setUseCompressedIndex( compressedIndexCheckBox->isChecked() );
    config.setUseIndexCache( indexCacheCheckBox->isChecked() );
    config.setIndexCacheMaxSizeMb( indexCacheMaxSizeSpinBox->value() );
    config.setOptimizeForNotLatinEncodings( optimizeForNotLatinEncodingsCheckBox->isChecked() );

    // version checking; the box shows no setting while the installer turned
    // the check off (#445), so the stored one is kept for a later install
    if ( !logsquirl::versioncheck::updateCheckTurnedOffAtInstall() ) {
        config.setVersionCheckingEnabled( checkForNewVersionCheckBox->isChecked() );
    }
    config.setBetaVersionCheckingEnabled( checkForBetaVersionCheckBox->isChecked() );

    config.setVerifySslPeers( verifySslCheckBox->isChecked() );

    config.setTeamFolderEnabled( teamFolderCheckBox->isChecked() );
    config.setTeamFolderUrl( teamFolderUrlEdit->text().trimmed() );
    config.setTeamFolderSubfolder( teamFolderSubfolderEdit->text().trimmed() );

    const auto themeChanged = config.style() != styleComboBox->currentText();
    config.setStyle( styleComboBox->currentText() );
    if ( themeChanged ) {
        Theme::apply( config.style() );
    }
    config.setAnsiColorSequences(
        static_cast<AnsiColorSequences>( ansiColorSequencesComboBox->currentIndex() ) );

    config.setContextLinesCount( contextLinesSpinBox->value() );

    config.setDefaultEncodingMib( encodingComboBox->currentData().toInt() );

    auto shortcuts = config.shortcuts();
    for ( auto shortcutRow = 0; shortcutRow < shortcutsTable->rowCount(); ++shortcutRow ) {
        QStringList actionKeys;

        auto primaryKeySequence
            = shortcutsTable->item( shortcutRow, 1 )->data( Qt::UserRole ).toString();
        auto secondaryKeySequence
            = shortcutsTable->item( shortcutRow, 2 )->data( Qt::UserRole ).toString();
        actionKeys << primaryKeySequence << secondaryKeySequence;

        auto action
            = shortcutsTable->item( shortcutRow, 0 )->data( Qt::UserRole ).toString().toStdString();
        shortcuts[ action ] = actionKeys;
    }
    config.setShortcuts( shortcuts );

    // update translate when accept or apply clicked
    restartAppMessage |= config.language() != languageComboBox->currentData().toString();
    updateTranslate();
    config.setLanguage( languageComboBox->currentData().toString() );
    retranslateUi( this );

    config.save();

    auto& savedSearches = SavedSearches::get();
    savedSearches.setHistorySize( searchHistorySpinBox->value() );
    savedSearches.save();

    auto& recentFiles = RecentFiles::get();
    recentFiles.setFilesHistoryMaxItems( filesHistoryMaxItemsSpinBox->value() );
    recentFiles.save();

    if ( restartAppMessage ) {
        QMessageBox::warning(
            this, "logsquirl",
            QApplication::translate( "OptionsDialog",
                                     "LogSquirl needs to be restarted to apply some changes. " ) );
    }

    Q_EMIT optionsChanged();
}

void OptionsDialog::onButtonBoxClicked( QAbstractButton* button )
{
    QDialogButtonBox::ButtonRole role = buttonBox->buttonRole( button );
    if ( ( role == QDialogButtonBox::AcceptRole ) || ( role == QDialogButtonBox::ApplyRole ) ) {
        updateConfigFromDialog();
        applyFileAssociations();
    }

    if ( role == QDialogButtonBox::AcceptRole )
        accept();
    else if ( role == QDialogButtonBox::RejectRole )
        reject();
}

ShortcutRecordingDelegate::ShortcutRecordingDelegate( QAbstractItemView* view )
    : QStyledItemDelegate( view )
    , view_( view )
{
    // Recording starts only on a click or Enter; typing on a selected cell
    // must not start it, and Tab leaves the table instead of walking its cells.
    view_->setEditTriggers( QAbstractItemView::NoEditTriggers );
    view_->setTabKeyNavigation( false );
    view_->installEventFilter( this );

    connect( view_, &QAbstractItemView::clicked, this, [ this ]( const QModelIndex& index ) {
        if ( view_->itemDelegateForIndex( index ) == this
             && index.flags().testFlag( Qt::ItemIsEditable ) ) {
            view_->edit( index );
        }
    } );
}

QWidget* ShortcutRecordingDelegate::createEditor( QWidget* parent, const QStyleOptionViewItem&,
                                                  const QModelIndex& ) const
{
    auto* recorder = new QKeySequenceEdit( parent );
    // One key combination ends the recording right away.
    recorder->setMaximumSequenceLength( 1 );
    recorder->setClearButtonEnabled( true );

    auto* self = const_cast<ShortcutRecordingDelegate*>( this );
    connect( recorder, &QKeySequenceEdit::editingFinished, self,
             [ self, recorder ] { self->endRecording( recorder, Recording::Keep ); } );

    // The clear icon shown while recording clears the shortcut.
    if ( auto* clearAction = recorder->findChild<QAction*>( "_q_qlineeditclearaction" ) ) {
        connect( clearAction, &QAction::triggered, self,
                 [ self, recorder ] { self->endRecording( recorder, Recording::Clear ); } );
    }

    return recorder;
}

void ShortcutRecordingDelegate::setEditorData( QWidget* editor, const QModelIndex& index ) const
{
    static_cast<QKeySequenceEdit*>( editor )->setKeySequence(
        QKeySequence( index.data( Qt::UserRole ).toString(), QKeySequence::PortableText ) );
}

void ShortcutRecordingDelegate::setModelData( QWidget* editor, QAbstractItemModel* model,
                                              const QModelIndex& index ) const
{
    setShortcut( model, index, static_cast<QKeySequenceEdit*>( editor )->keySequence() );
    Q_EMIT const_cast<ShortcutRecordingDelegate*>( this )->edited();
}

void ShortcutRecordingDelegate::setShortcut( QAbstractItemModel* model, const QModelIndex& index,
                                             const QKeySequence& keySequence )
{
    model->setData( index, keySequence.toString( QKeySequence::PortableText ), Qt::UserRole );
    model->setData( index, keySequence.toString( QKeySequence::NativeText ), Qt::DisplayRole );
}

bool ShortcutRecordingDelegate::eventFilter( QObject* watched, QEvent* event )
{
    if ( watched == view_ ) {
        return event->type() == QEvent::KeyPress
               && viewKeyPressed( static_cast<QKeyEvent*>( event ) );
    }

    auto* recorder = qobject_cast<QKeySequenceEdit*>( watched );
    if ( recorder && event->type() == QEvent::KeyPress
         && recorderKeyPressed( recorder, static_cast<QKeyEvent*>( event ) ) ) {
        return true;
    }

    if ( recorder && event->type() == QEvent::FocusOut ) {
        // Leaving the cell while recording cancels it: a finished recording
        // has already been stored, an unfinished one is dropped.
        if ( !recorder->isAncestorOf( QApplication::focusWidget() ) ) {
            endRecording( recorder, Recording::Cancel );
        }
        return false;
    }

    return QStyledItemDelegate::eventFilter( watched, event );
}

bool ShortcutRecordingDelegate::viewKeyPressed( const QKeyEvent* keyEvent )
{
    const auto index = view_->currentIndex();
    if ( !index.isValid() || view_->itemDelegateForIndex( index ) != this
         || !index.flags().testFlag( Qt::ItemIsEditable )
         || ( keyEvent->modifiers() & ~Qt::KeypadModifier ) != Qt::NoModifier ) {
        return false;
    }

    switch ( keyEvent->key() ) {
    case Qt::Key_Return:
    case Qt::Key_Enter:
        view_->edit( index );
        return true;
    case Qt::Key_Backspace:
    case Qt::Key_Delete:
        setShortcut( view_->model(), index, {} );
        Q_EMIT edited();
        return true;
    default:
        return false;
    }
}

bool ShortcutRecordingDelegate::recorderKeyPressed( QWidget* recorder, const QKeyEvent* keyEvent )
{
    if ( keyEvent->matches( QKeySequence::Cancel ) || keyEvent->key() == Qt::Key_Tab
         || keyEvent->key() == Qt::Key_Backtab ) {
        endRecording( recorder, Recording::Cancel );
        return true;
    }

    // A bare Backspace or Delete clears the shortcut instead of being recorded.
    if ( ( keyEvent->key() == Qt::Key_Backspace || keyEvent->key() == Qt::Key_Delete )
         && ( keyEvent->modifiers() & ~Qt::KeypadModifier ) == Qt::NoModifier ) {
        endRecording( recorder, Recording::Clear );
        return true;
    }

    // Every other key, Enter included, is recorded by the editor itself.
    return false;
}

void ShortcutRecordingDelegate::endRecording( QWidget* recorder, Recording outcome )
{
    // Closing hides the recorder, and it then finishes and loses focus once
    // more; only the first end counts.
    recorder->disconnect( this );
    recorder->removeEventFilter( this );

    if ( outcome == Recording::Cancel ) {
        Q_EMIT closeEditor( recorder, QAbstractItemDelegate::RevertModelCache );
        return;
    }
    if ( outcome == Recording::Clear ) {
        static_cast<QKeySequenceEdit*>( recorder )->clear();
    }
    Q_EMIT commitData( recorder );
    Q_EMIT closeEditor( recorder, QAbstractItemDelegate::NoHint );
}

void OptionsDialog::buildShortcutsTable( bool useDefaultsOnly )
{
    shortcutsTable->setRowCount( 0 );

    const auto& config = Configuration::get();
    auto shortcutList = ShortcutAction::defaultShortcutList();
    if ( !useDefaultsOnly ) {
        for ( const auto& [ action, keys ] : config.shortcuts() ) {
            shortcutList[ action ].keySequence = keys;
        }
    }

    for ( const auto& [ action, shortCut ] : shortcutList ) {
        auto currentRow = shortcutsTable->rowCount();
        shortcutsTable->insertRow( currentRow );

        auto keyItem = new QTableWidgetItem( shortCut.name );
        keyItem->setFlags( Qt::ItemIsEnabled | Qt::ItemIsSelectable );
        keyItem->setData( Qt::UserRole, QString::fromStdString( action ) );
        shortcutsTable->setItem( currentRow, 0, keyItem );

        const auto shortcutItem = [ this, currentRow ]( int column, const QString& keySequence ) {
            auto item = new QTableWidgetItem;
            item->setToolTip( tr( "Click or press Enter to record a shortcut, Escape cancels.\n"
                                  "Backspace or Delete clears the shortcut." ) );
            shortcutsTable->setItem( currentRow, column, item );
            ShortcutRecordingDelegate::setShortcut(
                shortcutsTable->model(), shortcutsTable->model()->index( currentRow, column ),
                QKeySequence( keySequence ) );
        };
        shortcutItem( 1, shortCut.keySequence.size() > 0 ? shortCut.keySequence[ 0 ] : "" );
        shortcutItem( 2, shortCut.keySequence.size() > 1 ? shortCut.keySequence[ 1 ] : "" );
    }

    shortcutsTable->horizontalHeader()->setSectionResizeMode( QHeaderView::Stretch );
    shortcutsTable->horizontalHeader()->setSectionResizeMode( 0, QHeaderView::Interactive );
    shortcutsTable->horizontalHeader()->setMinimumSectionSize( 150 );
    shortcutsTable->resizeColumnToContents( 0 );
    shortcutsTable->setHorizontalHeaderItem( 0, new QTableWidgetItem( tr( "Action" ) ) );
    shortcutsTable->setHorizontalHeaderItem( 1, new QTableWidgetItem( tr( "Primary shortcut" ) ) );
    shortcutsTable->setHorizontalHeaderItem( 2,
                                             new QTableWidgetItem( tr( "Secondary shortcut" ) ) );

    // in case if user set duplicate keys and after restores defaults
    // it is need to enable back standard buttons
    checkShortcutsOnDuplicate();

    shortcutsTable->sortItems( 0 );
}
