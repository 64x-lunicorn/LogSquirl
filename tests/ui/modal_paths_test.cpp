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

// The paths of the main window that only a person clicked through until now
// (#492): each is driven through the window's actions, buttons and dialogs,
// with the dialogs answered by ModalAnswers. Nothing here calls a slot by name
// except where the path starts outside the window (a colleague's publish, the
// command line's standard input).

#include <catch2/catch_test_macros.hpp>

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QHeaderView>
#include <QListWidget>
#include <QProcess>
#include <QPushButton>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTest>
#include <QUrl>

#include <cstdio>
#include <map>
#include <optional>
#include <string>

#include "applicationplugins.h"
#include "chartpanel.h"
#include "crawlerwidget.h"
#include "fake_file_watch.h"
#include "highlightersdialog.h"
#include "highlighterset.h"
#include "logformatcatalog.h"
#include "mainwindow.h"
#include "mainwindowtext.h"
#include "modal_answers.h"
#include "openlogfile.h"
#include "session.h"
#include "tabbedcrawlerwidget.h"
#include "teamfolder.h"
#include "test_policies.h"
#include "test_utils.h"

using namespace logsquirl::teamfolder;

// What the tests below read from a Crawler Widget beyond its public face: the
// state the paths leave behind.
struct ModalPathsAccess {};

template <>
struct CrawlerWidget::access_by<ModalPathsAccess> {
    CrawlerWidget& crawler;

    LineNumber searchStart() const
    {
        return crawler.openLogFile_->searchStartLine();
    }

    LineNumber searchEnd() const
    {
        return crawler.openLogFile_->searchEndLine();
    }

    LineNumber currentLine() const
    {
        return crawler.currentLineNumber_;
    }

    QString searchText() const
    {
        return crawler.searchLineEdit_->currentText();
    }

    LinesCount nbLines() const
    {
        return crawler.openLogFile_->logData()->getNbLine();
    }
};

using CrawlerState = CrawlerWidget::access_by<ModalPathsAccess>;

namespace {

constexpr int LineCount = 300;

SettingsPolicies recognitionEnabled()
{
    auto policies = testSettingsPolicies();
    policies.recognition.enabled = true;
    return policies;
}

// The Team Folder Policy the Session sets its Team Folder up with: the
// Session follows it, so it names the repository the Team Folder was set up with.
SettingsPolicies policiesWith( const TeamFolderPolicy& teamPolicy )
{
    auto policies = recognitionEnabled();
    policies.teamFolder = teamPolicy;
    return policies;
}

std::shared_ptr<LogFormatCatalog> builtInLogFormats()
{
    auto catalog = std::make_shared<LogFormatCatalog>();
    catalog->rebuild();
    return catalog;
}

// Line i is written at 12:00:00 plus ten seconds a line, in a Log Format the
// built-in catalog recognizes, with a level that repeats every three lines.
QByteArray timestampedLogLines()
{
    static const char* const levels[] = { "info", "warn", "error" };
    QByteArray text;
    for ( int i = 0; i < LineCount; ++i ) {
        const auto seconds = i * 10;
        text += QStringLiteral( "[2026-01-01 12:%1:%2.000] [modal] [%3] modal path line %4\n" )
                    .arg( seconds / 60, 2, 10, QChar( '0' ) )
                    .arg( seconds % 60, 2, 10, QChar( '0' ) )
                    .arg( levels[ i % 3 ] )
                    .arg( i )
                    .toUtf8();
    }
    return text;
}

bool writeFile( const QString& path, const QByteArray& content )
{
    QFile file( path );
    if ( !file.open( QIODevice::WriteOnly | QIODevice::Truncate ) ) {
        return false;
    }
    file.write( content );
    return true;
}

QAction* actionNamed( const MainWindow& window, const char* text )
{
    const auto translated = QApplication::translate( "logsquirl::mainwindow::action", text );
    for ( auto* action : window.findChildren<QAction*>() ) {
        if ( action->text() == translated ) {
            return action;
        }
    }
    return nullptr;
}

QPushButton* buttonNamed( QWidget& parent, const QString& text )
{
    for ( auto* button : parent.findChildren<QPushButton*>() ) {
        if ( button->text() == text ) {
            return button;
        }
    }
    return nullptr;
}

// A main window over a Session, shown and active.
struct WindowFixture {
    explicit WindowFixture( std::shared_ptr<TeamFolder> teamFolder = {},
                            const TeamFolderPolicy& teamPolicy = {},
                            std::shared_ptr<FakeFileWatch> watch
                            = std::make_shared<FakeFileWatch>(),
                            std::shared_ptr<LogFormatCatalog> catalog = builtInLogFormats() )
        : fileWatch( watch )
        , session( std::make_shared<Session>( policiesWith( teamPolicy ), std::move( catalog ),
                                              std::move( watch ), std::move( teamFolder ) ) )
        , plugins( std::make_shared<logsquirl::plugins::ApplicationPlugins>() )
        , mainWindow( std::make_unique<MainWindow>( WindowSession{ session, "Main", 0 }, plugins ) )
    {
        mainWindow->show();
        mainWindow->activateWindow();
        REQUIRE( QTest::qWaitForWindowActive( mainWindow.get(), 5000 ) );
        tabArea = mainWindow->findChild<TabbedCrawlerWidget*>();
        REQUIRE( tabArea != nullptr );
    }

    ~WindowFixture()
    {
        mainWindow.reset();
        QTest::qWait( 50 );
    }

    // Opens the Log File and waits for its tab, loaded.
    CrawlerWidget* open( const QString& path )
    {
        mainWindow->loadInitialFile( path, false );
        CrawlerWidget* found = nullptr;
        REQUIRE( waitUiState( [ & ] {
            for ( auto* crawler : mainWindow->findChildren<CrawlerWidget*>() ) {
                if ( CrawlerState{ *crawler }.nbLines().get() > 0 ) {
                    found = crawler;
                }
            }
            return found != nullptr;
        } ) );
        return found;
    }

    std::shared_ptr<FakeFileWatch> fileWatch;
    std::shared_ptr<Session> session;
    std::shared_ptr<logsquirl::plugins::ApplicationPlugins> plugins;
    std::unique_ptr<MainWindow> mainWindow;
    TabbedCrawlerWidget* tabArea = nullptr;
};

// --- The Team Folder against a real Git server on disk ---

// Git runs with an identity of the test's own and without the user's global
// and system configuration, so the real ~/.gitconfig is neither read nor changed.
class IsolatedGitEnvironment {
public:
    IsolatedGitEnvironment()
    {
#ifdef Q_OS_WIN
        const QByteArray nullDevice = "NUL";
#else
        const QByteArray nullDevice = "/dev/null";
#endif
        set( "GIT_CONFIG_GLOBAL", nullDevice );
        set( "GIT_CONFIG_NOSYSTEM", "1" );
        set( "GIT_TERMINAL_PROMPT", "0" );
        set( "GIT_AUTHOR_NAME", "Modal Path Test" );
        set( "GIT_AUTHOR_EMAIL", "modal-path-test@example.invalid" );
        set( "GIT_COMMITTER_NAME", "Modal Path Test" );
        set( "GIT_COMMITTER_EMAIL", "modal-path-test@example.invalid" );
    }

    ~IsolatedGitEnvironment()
    {
        for ( const auto& [ name, value ] : previous_ ) {
            if ( value.has_value() ) {
                qputenv( name.c_str(), *value );
            }
            else {
                qunsetenv( name.c_str() );
            }
        }
    }

    IsolatedGitEnvironment( const IsolatedGitEnvironment& ) = delete;
    IsolatedGitEnvironment& operator=( const IsolatedGitEnvironment& ) = delete;

private:
    void set( const char* name, const QByteArray& value )
    {
        previous_.emplace( name, qEnvironmentVariableIsSet( name )
                                     ? std::optional<QByteArray>( qgetenv( name ) )
                                     : std::nullopt );
        qputenv( name, value );
    }

    std::map<std::string, std::optional<QByteArray>> previous_;
};

bool gitInstalled()
{
    return !QStandardPaths::findExecutable( QStringLiteral( "git" ) ).isEmpty();
}

bool settled( const TeamFolder& folder )
{
    return QTest::qWaitFor( [ &folder ] { return !folder.isSyncing(); }, 60'000 );
}

PublishOutcome publishAndWait( TeamFolder& folder, const QList<PublishRequest>& requests )
{
    QSignalSpy finished( &folder, &TeamFolder::publishFinished );
    folder.publish( requests );
    REQUIRE( finished.wait( 60'000 ) );
    return finished.at( 0 ).at( 0 ).value<PublishOutcome>();
}

PredefinedFilterSet filterGroup( const QString& name, const QString& pattern )
{
    auto group = PredefinedFilterSet::createNewSet( name );
    group.addFilter( { "Errors", pattern, true } );
    return group;
}

HighlighterSet highlighterSet( const QString& name, const QString& pattern = "ERROR" )
{
    auto set = HighlighterSet::createNewSet( name );
    set.addHighlighter( Highlighter( pattern, false, true, Qt::red, Qt::white ) );
    return set;
}

QStringList namesOf( const QList<PredefinedFilterSet>& groups )
{
    QStringList names;
    for ( const auto& group : groups ) {
        names.append( group.name() );
    }
    return names;
}

// The team's server, and Team Folders for two members of the team.
struct Team {
    Team()
    {
        REQUIRE( root.isValid() );
        server = root.filePath( "server.git" );
        QProcess git;
        git.start( "git", { "init", "--quiet", "--bare", server } );
        REQUIRE( git.waitForFinished( 60'000 ) );
        REQUIRE( git.exitCode() == 0 );
    }

    TeamFolderPolicy policy() const
    {
        return TeamFolderPolicy{ .enabled = true,
                                 .repositoryUrl = QUrl::fromLocalFile( server ).toString(),
                                 .subfolder = {} };
    }

    std::shared_ptr<TeamFolder> member( const QString& name )
    {
        auto folder = std::make_shared<TeamFolder>( root.filePath( name ) );
        folder->setUp( policy() );
        REQUIRE( settled( *folder ) );
        return folder;
    }

    // What the server's default branch holds.
    QStringList serverFiles() const
    {
        QProcess git;
        git.start( "git", { "--git-dir", server, "ls-tree", "-r", "--name-only", "HEAD" } );
        git.waitForFinished( 60'000 );
        return QString::fromUtf8( git.readAllStandardOutput() )
            .trimmed()
            .split( '\n', Qt::SkipEmptyParts );
    }

    QTemporaryDir root;
    QString server;
};

// The Highlighter Sets of the collection, put back when this goes.
class KeepCollection {
public:
    KeepCollection()
        : sets_( HighlighterSetCollection::get().highlighterSets() )
        , active_( HighlighterSetCollection::get().activeSetIds() )
    {
    }
    ~KeepCollection()
    {
        auto& collection = HighlighterSetCollection::get();
        collection.setHighlighterSets( sets_ );
        collection.deactivateAll();
        for ( const auto& id : active_ ) {
            collection.activateSet( id );
        }
        collection.save();
    }

private:
    QList<HighlighterSet> sets_;
    QStringList active_;
};

} // namespace

// --- The Team Folder conflict question ---

TEST_CASE( "A publish that meets a colleague's change asks the user and carries out the answer",
           "[ui][modal][teamfolder]" )
{
    const IsolatedGitEnvironment environment;
    if ( !gitInstalled() ) {
        SKIP( "Git is not installed" );
    }

    Team team;
    const auto alice = team.member( "alice" );
    const auto network = filterGroup( "Network", "original" );
    const auto storage = filterGroup( "Storage", "storage" );
    const auto seeded
        = publishAndWait( *alice, { PublishRequest::forGroup( network, GroupAction::Add ),
                                    PublishRequest::forGroup( storage, GroupAction::Add ) } );
    REQUIRE( seeded.results.size() == 2 );
    for ( const auto& result : seeded.results ) {
        INFO( result.message.toStdString() );
        REQUIRE( result.status == PublishStatus::Published );
    }
    REQUIRE( settled( *alice ) );

    const auto bob = team.member( "bob" );
    WindowFixture window( bob, team.policy() );
    ModalAnswers modals;

    // Bob's edit, made against the group as he loaded it.
    auto mine = network;
    mine.setFilters( { { "Bobs", "bobs pattern", false } } );
    auto request = PublishRequest::forGroup( mine, GroupAction::Change );
    request.baseRevision = bob->filterGroupRevision( network.id() );
    REQUIRE_FALSE( request.baseRevision.value_or( QString{} ).isEmpty() );

    // Alice changed it meanwhile.
    auto theirs = network;
    theirs.setFilters( { { "Alices", "alices pattern", false } } );
    auto theirRequest = PublishRequest::forGroup( theirs, GroupAction::Change );
    theirRequest.baseRevision = alice->filterGroupRevision( network.id() );
    REQUIRE( publishAndWait( *alice, { theirRequest } ).results[ 0 ].status
             == PublishStatus::Published );

    const auto answered
        = [ & ] { return waitUiState( [ & ] { return modals.unanswered() == 0; }, 60'000 ); };

    SECTION( "Keep mine publishes bob's version over theirs" )
    {
        modals.clickButton( "Keep mine" );
        bob->publish( { request } );
        REQUIRE( answered() );
        REQUIRE( waitUiState( [ & ] { return team.serverFiles().size() == 2; }, 5000 ) );
        REQUIRE( settled( *bob ) );
        REQUIRE( waitUiState(
            [ & ] {
                alice->sync();
                return settled( *alice ) && alice->filterGroups().size() == 2
                       && alice->filterGroups()[ 0 ].filters()[ 0 ].pattern == "bobs pattern";
            },
            30'000 ) );
    }

    SECTION( "Take theirs drops bob's change" )
    {
        modals.clickButton( "Take theirs" );
        bob->publish( { request } );
        REQUIRE( answered() );
        REQUIRE( settled( *bob ) );
        const auto groups = bob->filterGroups();
        REQUIRE( groups.size() == 2 );
        CHECK( groups[ 0 ].filters()[ 0 ].pattern == "alices pattern" );
    }

    SECTION( "Save mine as a copy adds a group and leaves theirs" )
    {
        modals.clickButton( "Save mine as a copy" );
        bob->publish( { request } );
        REQUIRE( answered() );
        REQUIRE( waitUiState( [ & ] { return team.serverFiles().size() == 3; }, 30'000 ) );
        REQUIRE( settled( *bob ) );
        alice->sync();
        REQUIRE( settled( *alice ) );
        const auto groups = alice->filterGroups();
        REQUIRE( groups.size() == 3 );
        CHECK( namesOf( groups ) == QStringList{ "Network", "Network (2)", "Storage" } );
        CHECK( groups[ 0 ].filters()[ 0 ].pattern == "alices pattern" );
        CHECK( groups[ 1 ].filters()[ 0 ].pattern == "bobs pattern" );
    }

    SECTION( "Two conflicts of one publish are asked one after the other" )
    {
        // Alice changes the second group as well, and bob's edit of it is stale, too.
        auto storageTheirs = storage;
        storageTheirs.setFilters( { { "Alices", "alices storage", false } } );
        auto storageTheirRequest = PublishRequest::forGroup( storageTheirs, GroupAction::Change );
        storageTheirRequest.baseRevision = alice->filterGroupRevision( storage.id() );
        REQUIRE( publishAndWait( *alice, { storageTheirRequest } ).results[ 0 ].status
                 == PublishStatus::Published );
        auto storageMine = storage;
        storageMine.setFilters( { { "Bobs", "bobs storage", false } } );
        auto storageRequest = PublishRequest::forGroup( storageMine, GroupAction::Change );
        storageRequest.baseRevision = bob->filterGroupRevision( storage.id() );

        QStringList openedWhenAnswering;
        modals
            .clickButton( "Take theirs",
                          [ & ] { openedWhenAnswering << QString::number( modals.unanswered() ); } )
            .clickButton( "Take theirs", [ & ] {
                openedWhenAnswering << QString::number( modals.unanswered() );
            } );
        bob->publish( { request, storageRequest } );
        REQUIRE( answered() );
        // The second question was still waiting while the first was answered.
        CHECK( openedWhenAnswering == QStringList{ "1", "0" } );
        CHECK( modals.unexpectedMessageBoxes() == 0 );
        REQUIRE( settled( *bob ) );
        const auto groups = bob->filterGroups();
        REQUIRE( groups.size() == 2 );
        CHECK( groups[ 0 ].filters()[ 0 ].pattern == "alices pattern" );
        CHECK( groups[ 1 ].filters()[ 0 ].pattern == "alices storage" );
    }

    SECTION( "Nothing is asked while a dialog is open; the question follows the dialog" )
    {
        auto* openUrl
            = actionNamed( *window.mainWindow, logsquirl::mainwindow::action::openUrlText );
        REQUIRE( openUrl != nullptr );

        bool askedWhileOpen = true;
        modals
            .answerWith( [ & ]( QDialog& dialog ) {
                // A colleague's change meets this window's edit while the
                // "Open from URL" dialog is open.
                bob->publish( { request } );
                REQUIRE( waitUiState(
                    [ & ] { return bob->hasPendingChanges() || !bob->isSyncing(); }, 30'000 ) );
                QTest::qWait( 1500 );
                askedWhileOpen
                    = qobject_cast<QMessageBox*>( QApplication::activeModalWidget() ) != nullptr;
                dialog.reject();
            } )
            .clickButton( "Take theirs" );

        openUrl->trigger();
        CHECK_FALSE( askedWhileOpen );
        // The dialog is gone and the window system gives the window the focus
        // back (the offscreen platform does not): the window asks now.
        window.mainWindow->activateWindow();
        REQUIRE( answered() );
        CHECK( modals.unexpectedMessageBoxes() == 0 );
        REQUIRE( modals.titles().size() == 2 );
        REQUIRE( modals.messages().size() == 1 );
        CHECK(
            modals.messages()[ 0 ].contains( "Somebody else changed the Team group \"Network\"" ) );
    }
}

// --- The tab of standard input ---

TEST_CASE( "The tab of standard input is named stdin and takes no other tab's name",
           "[ui][modal][stdin]" )
{
    // The window reads from a stream that ends at once, not from the tests' own.
#ifdef Q_OS_WIN
    const char* const nullDevice = "NUL";
#else
    const char* const nullDevice = "/dev/null";
#endif
    REQUIRE( std::freopen( nullDevice, "r", stdin ) != nullptr );

    // A window whose plugins have not loaded: the Log File opens when they do,
    // later than the call that asked for it.
    WindowFixture window;
    const auto dashboardTabs = window.tabArea->count();

    window.mainWindow->openStandardInput();

    REQUIRE( waitUiState( [ & ] { return window.tabArea->count() == dashboardTabs + 1; } ) );
    QStringList titles;
    for ( int i = 0; i < window.tabArea->count(); ++i ) {
        titles << window.tabArea->tabText( i );
    }
    INFO( titles.join( ", " ).toStdString() );
    REQUIRE( titles.count( "stdin" ) == 1 );
    // The tab that was in front keeps its own name: nothing was renamed, one tab came.
    CHECK( titles.size() == dashboardTabs + 1 );
    auto* stdinTab = qobject_cast<CrawlerWidget*>( window.tabArea->currentWidget() );
    REQUIRE( stdinTab != nullptr );
    CHECK( window.tabArea->tabText( window.tabArea->currentIndex() ) == "stdin" );
    CHECK( window.tabArea->tabToolTip( window.tabArea->currentIndex() )
               .startsWith( "Standard input" ) );
}

// --- The time dialogs ---

namespace {

struct TimedLogFile {
    TimedLogFile()
    {
        REQUIRE( directory.isValid() );
        path = directory.filePath( "timed.log" );
        REQUIRE( writeFile( path, timestampedLogLines() ) );
    }

    QTemporaryDir directory;
    QString path;
};

// Waits until the actions that need a Timestamp are there for the Log File.
void waitForTimeActions( const MainWindow& window )
{
    auto* goTo = actionNamed( window, logsquirl::mainwindow::action::goToTimestampText );
    REQUIRE( goTo != nullptr );
    REQUIRE( waitUiState( [ & ] { return goTo->isEnabled(); }, 30'000 ) );
}

} // namespace

TEST_CASE( "The time dialogs move to a time and limit the search to it", "[ui][modal][time]" )
{
    TimedLogFile file;
    WindowFixture window;
    auto* crawler = window.open( file.path );
    waitForTimeActions( *window.mainWindow );
    CrawlerState state{ *crawler };
    ModalAnswers modals;
    const auto trigger = [ & ]( const char* text ) {
        auto* action = actionNamed( *window.mainWindow, text );
        REQUIRE( action != nullptr );
        REQUIRE( action->isEnabled() );
        action->trigger();
    };
    const auto wholeFile = std::pair{ state.searchStart(), state.searchEnd() };

    SECTION( "Go to timestamp selects the first line at or after the time" )
    {
        modals.inputText( "12:10:00" );
        trigger( logsquirl::mainwindow::action::goToTimestampText );
        CHECK( modals.unanswered() == 0 );
        CHECK( modals.unexpectedMessageBoxes() == 0 );
        // 12:10:00 is line 60 (ten seconds a line, from 12:00:00).
        REQUIRE( waitUiState( [ & ] { return state.currentLine().get() == 60; }, 10'000 ) );
    }

    SECTION( "Go to timestamp before the first line says so and goes to the first line" )
    {
        modals.inputText( "11:00:00" ).click( QMessageBox::Ok );
        trigger( logsquirl::mainwindow::action::goToTimestampText );
        REQUIRE( modals.messages().size() == 1 );
        CHECK( modals.messages()[ 0 ].contains( "before the first timestamp" ) );
    }

    SECTION( "Go to timestamp with text that is no time says so" )
    {
        modals.inputText( "not a time" ).click( QMessageBox::Ok );
        trigger( logsquirl::mainwindow::action::goToTimestampText );
        REQUIRE( modals.messages().size() == 1 );
        CHECK( modals.messages()[ 0 ].contains( "is not a time" ) );
    }

    SECTION( "Cancelling the dialog changes nothing" )
    {
        modals.cancelInput();
        trigger( logsquirl::mainwindow::action::goToTimestampText );
        CHECK( modals.unanswered() == 0 );
        CHECK( state.currentLine().get() == 0 );
    }

    SECTION( "Set search limits to a time range takes two times" )
    {
        modals.inputText( "12:10:00" ).inputText( "12:20:00" );
        trigger( logsquirl::mainwindow::action::searchLimitsTimeRangeText );
        CHECK( modals.unanswered() == 0 );
        CHECK( modals.unexpectedMessageBoxes() == 0 );
        CHECK( state.searchStart().get() == 60 );
        CHECK( state.searchEnd().get() == 120 );
    }

    SECTION( "Set search limits to a time range after the end of the file says so" )
    {
        modals.inputText( "13:00:00" ).inputText( "13:10:00" ).click( QMessageBox::Ok );
        trigger( logsquirl::mainwindow::action::searchLimitsTimeRangeText );
        REQUIRE( modals.messages().size() == 1 );
        CHECK( modals.messages()[ 0 ].contains( "after the last timestamp" ) );
        CHECK( state.searchStart() == wholeFile.first );
        CHECK( state.searchEnd() == wholeFile.second );
    }

    SECTION( "Set search limits around the current line takes minutes before and after" )
    {
        // Get to line 150 (12:25:00) first, the way a person would.
        modals.inputText( "12:25:00" );
        trigger( logsquirl::mainwindow::action::goToTimestampText );
        REQUIRE( waitUiState( [ & ] { return state.currentLine().get() == 150; }, 10'000 ) );

        modals.inputInt( 2 );
        trigger( logsquirl::mainwindow::action::searchLimitsAroundLineText );
        CHECK( modals.unanswered() == 0 );
        // Two minutes are twelve lines, each way.
        CHECK( state.searchStart().get() == 138 );
        CHECK( state.searchEnd().get() == 162 );
    }
}

TEST_CASE( "A Log File truncated while a time dialog is open is not read through what it held",
           "[ui][modal][time]" )
{
    TimedLogFile file;
    WindowFixture window;
    auto* crawler = window.open( file.path );
    waitForTimeActions( *window.mainWindow );
    CrawlerState state{ *crawler };
    const auto wholeEnd = state.searchEnd();
    ModalAnswers modals;

    // While the first dialog is open the Log File is cut down to something
    // that has no Log Format: the Timestamp reader and the Log Data it read
    // from are gone when the dialog is answered.
    const auto truncate = [ & ] {
        REQUIRE( writeFile( file.path, "nothing to recognize here\n" ) );
        REQUIRE( window.fileWatch->reportChange( file.path ) );
        REQUIRE( waitUiState( [ & ] { return state.nbLines().get() == 1; }, 60'000 ) );
        REQUIRE( waitUiState(
            [ & ] { return !crawler->searchLimitsByTimeUnavailableReason().isEmpty(); }, 60'000 ) );
    };
    modals.inputText( "12:10:00", truncate ).inputText( "12:20:00" );

    auto* action = actionNamed( *window.mainWindow,
                                logsquirl::mainwindow::action::searchLimitsTimeRangeText );
    REQUIRE( action != nullptr );
    action->trigger();

    // The window is still there, and no limits came out of a reader that was
    // gone: the first dialog's answer ends the path.
    QTest::qWait( 200 );
    CHECK( window.mainWindow->isVisible() );
    CHECK( state.searchStart().get() == 0 );
    CHECK( state.searchEnd().get() <= wholeEnd.get() );
    CHECK_FALSE( action->isEnabled() );
}

// --- The Team Folder buttons of the Highlighters dialog ---

TEST_CASE( "The Highlighters dialog shares and copies and deletes Team sets",
           "[ui][modal][teamfolder]" )
{
    const IsolatedGitEnvironment environment;
    if ( !gitInstalled() ) {
        SKIP( "Git is not installed" );
    }
    const KeepCollection keep;

    Team team;
    const auto alice = team.member( "alice" );
    REQUIRE( publishAndWait( *alice, { PublishRequest::forGroup( highlighterSet( "Theirs" ),
                                                                 GroupAction::Add ) } )
                 .results[ 0 ]
                 .status
             == PublishStatus::Published );
    REQUIRE( settled( *alice ) );

    const auto bob = team.member( "bob" );
    REQUIRE( bob->highlighterGroups().size() == 1 );
    WindowFixture window( bob, team.policy() );
    ModalAnswers modals;
    auto* action
        = actionNamed( *window.mainWindow, logsquirl::mainwindow::action::editHighlightersText );
    REQUIRE( action != nullptr );

    SECTION( "Share with team publishes a set of the user's own; Copy takes a Team set" )
    {
        const auto ownBefore = HighlighterSetCollection::get().highlighterSets().size();
        modals.answerWith( [ & ]( QDialog& dialog ) {
            auto& highlighters = static_cast<HighlightersDialog&>( dialog );
            auto* share = buttonNamed( dialog, "Share with team" );
            auto* copy = buttonNamed( dialog, "Copy to my groups" );
            REQUIRE( share != nullptr );
            REQUIRE( copy != nullptr );

            // A set of the user's own, selected.
            highlighters.addHighlighterButton->click();
            QCoreApplication::processEvents();
            CHECK( share->isEnabled() );
            QListWidget* teamList = nullptr;
            for ( auto* list : dialog.findChildren<QListWidget*>() ) {
                if ( list != highlighters.highlighterListWidget ) {
                    teamList = list;
                }
            }
            REQUIRE( teamList != nullptr );
            REQUIRE( teamList->count() == 1 );
            share->click();
            CHECK( teamList->count() == 2 );

            teamList->setCurrentRow( 0 );
            CHECK( copy->isEnabled() );
            copy->click();
            highlighters.buttonBox->button( QDialogButtonBox::Ok )->click();
        } );

        QSignalSpy finished( bob.get(), &TeamFolder::publishFinished );
        action->trigger();
        CHECK( modals.unanswered() == 0 );
        REQUIRE( finished.count() + ( finished.wait( 60'000 ) ? 1 : 0 ) >= 1 );
        REQUIRE( settled( *bob ) );
        // The server has the shared set beside theirs.
        REQUIRE( waitUiState( [ & ] { return team.serverFiles().size() == 2; }, 30'000 ) );
        // The user's own sets: the new one, and the copy of the Team set.
        CHECK( HighlighterSetCollection::get().highlighterSets().size() >= ownBefore + 2 );
    }

    SECTION( "Answering No to the question keeps the Team set" )
    {
        modals.answerWith( [ & ]( QDialog& dialog ) {
            auto& highlighters = static_cast<HighlightersDialog&>( dialog );
            auto* remove = buttonNamed( dialog, "Delete for the team" );
            REQUIRE( remove != nullptr );
            QListWidget* teamList = nullptr;
            for ( auto* list : dialog.findChildren<QListWidget*>() ) {
                if ( list != highlighters.highlighterListWidget ) {
                    teamList = list;
                }
            }
            REQUIRE( teamList != nullptr );
            teamList->setCurrentRow( 0 );
            remove->click();
            CHECK( teamList->count() == 1 );
            highlighters.buttonBox->button( QDialogButtonBox::Cancel )->click();
        } );
        modals.click( QMessageBox::No );

        action->trigger();
        CHECK( modals.unanswered() == 0 );
        CHECK( team.serverFiles().size() == 1 );
        CHECK( bob->highlighterGroups().size() == 1 );
    }

    SECTION( "Delete for the team removes the set for everybody" )
    {
        modals.answerWith( [ & ]( QDialog& dialog ) {
            auto& highlighters = static_cast<HighlightersDialog&>( dialog );
            auto* remove = buttonNamed( dialog, "Delete for the team" );
            REQUIRE( remove != nullptr );
            QListWidget* teamList = nullptr;
            for ( auto* list : dialog.findChildren<QListWidget*>() ) {
                if ( list != highlighters.highlighterListWidget ) {
                    teamList = list;
                }
            }
            REQUIRE( teamList != nullptr );
            teamList->setCurrentRow( 0 );
            REQUIRE( remove->isEnabled() );
            remove->click();
            CHECK( teamList->count() == 0 );
            highlighters.buttonBox->button( QDialogButtonBox::Ok )->click();
        } );
        // What the dialog asks before it deletes for the whole team.
        modals.click( QMessageBox::Yes );

        QSignalSpy finished( bob.get(), &TeamFolder::publishFinished );
        action->trigger();
        CHECK( modals.unanswered() == 0 );
        REQUIRE( finished.count() + ( finished.wait( 60'000 ) ? 1 : 0 ) >= 1 );
        REQUIRE( settled( *bob ) );
        REQUIRE( waitUiState( [ & ] { return team.serverFiles().isEmpty(); }, 30'000 ) );
        alice->sync();
        REQUIRE( settled( *alice ) );
        CHECK( alice->highlighterGroups().isEmpty() );
    }
}

// --- Value Count ---

TEST_CASE( "A click on a value of a Value Count searches for it", "[ui][modal][valuecount]" )
{
    // A Log Format of the test's own, with a level field to count.
    QTemporaryDir formats;
    REQUIRE( formats.isValid() );
    REQUIRE( writeFile( formats.filePath( "modal_path.json" ), R"({
        "modal_path_log": {
            "title": "Modal path",
            "regex": { "basic": { "pattern": "^MODAL (?<timestamp>\\d{6}) (?<level>[A-Z]+) (?<body>.*)$" } },
            "timestamp-field": "timestamp",
            "level-field": "level",
            "body-field": "body",
            "value": { "level": { "kind": "string" } },
            "sample": [{ "line": "MODAL 000001 INFO hello" }]
        }
    })" ) );
    TimedLogFile file;
    QByteArray text;
    for ( int i = 0; i < LineCount; ++i ) {
        text += QStringLiteral( "MODAL %1 %2 line %3\n" )
                    .arg( i, 6, 10, QChar( '0' ) )
                    .arg( i % 3 == 0   ? "INFO"
                          : i % 3 == 1 ? "WARN"
                                       : "ERROR" )
                    .arg( i )
                    .toUtf8();
    }
    REQUIRE( writeFile( file.path, text ) );
    auto catalog = std::make_shared<LogFormatCatalog>( formats.path() );
    catalog->rebuild();

    WindowFixture window( {}, {}, std::make_shared<FakeFileWatch>(), catalog );
    auto* crawler = window.open( file.path );
    REQUIRE( waitUiState( [ & ] { return crawler->goToTimestampUnavailableReason().isEmpty(); },
                          30'000 ) );
    CrawlerState state{ *crawler };

    auto* chart = crawler->findChild<ChartPanel*>();
    REQUIRE( chart != nullptr );
    // What the Table View's "count values" does: the panel opens, and counts.
    chart->show();
    chart->countFieldValues( "level" );
    // The panel is given room to lay its rows out.
    window.mainWindow->resize( 1200, 900 );
    auto sizes = crawler->sizes();
    for ( int i = 0; i < sizes.size(); ++i ) {
        sizes[ i ] = i == crawler->indexOf( chart ) ? 500 : 50;
    }
    crawler->setSizes( sizes );
    QTest::qWait( 100 );

    // The tab lists the values, most frequent first: "info", "warn" and
    // "error" 100 times each.
    QAbstractItemView* view = nullptr;
    REQUIRE( waitUiState(
        [ & ] {
            for ( auto* candidate : chart->findChildren<QAbstractItemView*>() ) {
                // The table's own headers are item views of the same model.
                if ( !qobject_cast<QHeaderView*>( candidate ) && candidate->model()
                     && candidate->model()->rowCount() == 3 ) {
                    view = candidate;
                }
            }
            return view != nullptr;
        },
        30'000 ) );

    const auto index = view->model()->index( 1, 0 );
    const auto value = index.data().toString();
    REQUIRE_FALSE( value.isEmpty() );
    REQUIRE( view->viewport()->isVisible() );
    view->scrollTo( index );
    QTest::mouseClick( view->viewport(), Qt::LeftButton, Qt::NoModifier,
                       view->visualRect( index ).center() );

    waitUiState( [ & ] { return state.searchText() == value; }, 3000 );
    INFO( "value " << value.toStdString() << " search " << state.searchText().toStdString() );
    REQUIRE( state.searchText() == value );
}
