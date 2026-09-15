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

#include "configuration.h"
#include "crawlerwidget.h"
#include "filteredview.h"
#include "filterspanel.h"
#include "highlightersdialog.h"
#include "logformatcatalog.h"
#include "logtableview.h"
#include "mainwindow.h"
#include "optionsdialog.h"
#include "predefinedfiltersdialog.h"
#include "session.h"
#include "tabbarstyle.h"
#include "tabbedcrawlerwidget.h"
#include "test_policies.h"
#include "test_utils.h"
#include "theme.h"

#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QGuiApplication>
#include <QImage>
#include <QMessageBox>
#include <QSignalSpy>
#include <QStyleHints>
#include <QTabBar>
#include <QTabWidget>
#include <QTemporaryFile>
#include <QTest>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>

#include <catch2/catch.hpp>

// Changing the Theme while the application runs (#173).
//
// The archived switching approach crashed here: QApplication::setStyleSheet()
// repolishes every widget, the Filters Panel reacted to the resulting
// PaletteChange by giving its tree a widget style of its own, and the style
// sheet style then read that tree's style while repolishing it.

namespace {

bool writeLogFile( QTemporaryFile& file )
{
    if ( !file.open() ) {
        return false;
    }
    for ( int i = 0; i < 2000; ++i ) {
        // spdlog lines, so that a built-in Log Format recognizes the Log File
        // and its Table View can be shown.
        file.write( QStringLiteral( "[2026-01-01 12:00:00.000] [theme] [info] theme switch test "
                                    "line %1\n" )
                        .arg( i )
                        .toUtf8() );
    }
    file.flush();
    return true;
}

QToolButton* buttonWithText( QWidget* parent, const QString& text )
{
    for ( auto* button : parent->findChildren<QToolButton*>() ) {
        if ( button->text() == text ) {
            return button;
        }
    }
    return nullptr;
}

QTabWidget* filteredViewTabs( CrawlerWidget* crawler )
{
    for ( auto* tabs : crawler->findChildren<QTabWidget*>() ) {
        if ( tabs->count() > 0 && qobject_cast<FilteredView*>( tabs->widget( 0 ) ) ) {
            return tabs;
        }
    }
    return nullptr;
}

// Keeps the current results and starts a new Search, which opens another
// Filtered View tab.
void addFilteredViewTab( CrawlerWidget* crawler )
{
    auto* keep = buttonWithText( crawler, QStringLiteral( "Keep Results" ) );
    auto* search = buttonWithText( crawler, QStringLiteral( "Search" ) );
    REQUIRE( keep != nullptr );
    REQUIRE( search != nullptr );
    keep->setChecked( true );
    search->click();
}

// Waits for the Log File to be recognized, then shows its Table View.
void showTableView( CrawlerWidget* crawler )
{
    QToolButton* toggle = nullptr;
    for ( auto* button : crawler->findChildren<QToolButton*>() ) {
        if ( button->accessibleName() == QStringLiteral( "Toggle table view" ) ) {
            toggle = button;
        }
    }
    REQUIRE( toggle != nullptr );
    REQUIRE( waitUiState( [ toggle, crawler ] { return toggle->isVisibleTo( crawler ); } ) );
    toggle->setChecked( true );

    auto* tableView = crawler->findChild<LogTableView*>();
    REQUIRE( tableView != nullptr );
    REQUIRE( waitUiState( [ tableView, crawler ] { return tableView->isVisibleTo( crawler ); } ) );
}

void closeLogFiles( TabbedCrawlerWidget* tabArea, int baseTabCount )
{
    while ( tabArea->count() > baseTabCount ) {
        const auto countBefore = tabArea->count();
        Q_EMIT tabArea->tabCloseRequested( tabArea->count() - 1 );
        REQUIRE( tabArea->count() < countBefore );
    }
}

std::vector<QImage> toolBarIconImages( MainWindow& mainWindow )
{
    std::vector<QImage> images;
    for ( auto* toolBar : mainWindow.findChildren<QToolBar*>() ) {
        for ( auto* action : toolBar->actions() ) {
            if ( !action->icon().isNull() ) {
                images.push_back( action->icon().pixmap( 16, 16 ).toImage() );
            }
        }
    }
    return images;
}

// Answers any message box that opens, and remembers that one did.
class MessageBoxWatcher {
public:
    MessageBoxWatcher()
    {
        timer_.setInterval( 20 );
        QObject::connect( &timer_, &QTimer::timeout, [ this ] {
            if ( auto* box = qobject_cast<QMessageBox*>( QApplication::activeModalWidget() ) ) {
                seen_ = true;
                box->done( QMessageBox::Ok );
            }
        } );
        timer_.start();
    }

    bool seen() const
    {
        return seen_;
    }

private:
    QTimer timer_;
    bool seen_ = false;
};

// Settings Policies under which a Log File is recognized against the Log
// Format Catalog.
SettingsPolicies recognitionEnabled()
{
    auto policies = testSettingsPolicies();
    policies.recognition.enabled = true;
    return policies;
}

// A Log Format Catalog holding the built-in Log Formats.
std::shared_ptr<LogFormatCatalog> builtInLogFormats()
{
    auto catalog = std::make_shared<LogFormatCatalog>();
    catalog->rebuild();
    return catalog;
}

struct MainWindowFixture {
    explicit MainWindowFixture( const SettingsPolicies& policies = testSettingsPolicies(),
                                std::shared_ptr<LogFormatCatalog> catalog
                                = std::make_shared<LogFormatCatalog>() )
        : session( std::make_shared<Session>( policies, std::move( catalog ) ) )
        , mainWindow( std::make_unique<MainWindow>( WindowSession{ session, "Main", 0 } ) )
    {
        mainWindow->show();
        QTest::qWait( 100 );
        tabArea = mainWindow->findChild<TabbedCrawlerWidget*>();
        REQUIRE( tabArea != nullptr );
        baseTabCount = tabArea->count();
    }

    ~MainWindowFixture()
    {
        mainWindow.reset();
        QTest::qWait( 50 );
        Theme::apply( Theme::defaultTheme() );
    }

    std::shared_ptr<Session> session;
    std::unique_ptr<MainWindow> mainWindow;
    TabbedCrawlerWidget* tabArea = nullptr;
    int baseTabCount = 0;
};

} // namespace

SCENARIO( "Switching the Theme with Log Files, Filtered Views, the Table View, a floating sidebar "
          "and dialogs open",
          "[ui][theme]" )
{
    QTemporaryFile firstFile{ QDir::tempPath() + "/theme_switch_test_XXXXXX" };
    QTemporaryFile secondFile{ QDir::tempPath() + "/theme_switch_test_XXXXXX" };
    REQUIRE( writeLogFile( firstFile ) );
    REQUIRE( writeLogFile( secondFile ) );

    MainWindowFixture fixture( recognitionEnabled(), builtInLogFormats() );
    auto& mainWindow = *fixture.mainWindow;
    const auto themes = Theme::availableThemes();

    for ( int round = 0; round < 3; ++round ) {
        mainWindow.loadInitialFile( firstFile.fileName(), false );
        mainWindow.loadInitialFile( secondFile.fileName(), false );
        REQUIRE(
            waitUiState( [ & ] { return fixture.tabArea->count() == fixture.baseTabCount + 2; } ) );

        for ( auto* crawler : mainWindow.findChildren<CrawlerWidget*>() ) {
            addFilteredViewTab( crawler );
            addFilteredViewTab( crawler );
            showTableView( crawler );
        }

        auto* sidebar = mainWindow.findChild<QDockWidget*>( "sidebarDock" );
        REQUIRE( sidebar != nullptr );
        sidebar->show();
        sidebar->setFloating( true );

        auto highlighters = std::make_unique<HighlightersDialog>( &mainWindow );
        highlighters->show();
        auto filters = std::make_unique<PredefinedFiltersDialog>( &mainWindow );
        filters->show();

        for ( const auto& theme : themes ) {
            Theme::apply( theme );
            // Closes a Filtered View before the events the switch posted
            // have been handled.
            for ( auto* crawler : mainWindow.findChildren<CrawlerWidget*>() ) {
                if ( auto* tabs = filteredViewTabs( crawler ); tabs && tabs->count() > 1 ) {
                    Q_EMIT tabs->tabCloseRequested( tabs->count() - 1 );
                }
            }
            QCoreApplication::processEvents();
        }

        highlighters.reset();
        filters.reset();
        sidebar->setFloating( round % 2 == 1 );

        // Closes the Log Files right after a switch, too.
        Theme::apply( themes.at( round % themes.size() ) );
        closeLogFiles( fixture.tabArea, fixture.baseTabCount );
        QTest::qWait( 50 );
    }

    REQUIRE( fixture.tabArea->count() == fixture.baseTabCount );
}

SCENARIO( "The Filters Panel survives the palette and stylesheet changing while it is shown",
          "[ui][theme]" )
{
    FiltersPanel panel;
    panel.resize( 300, 400 );
    panel.show();
    QTest::qWait( 20 );

    // The order the archived theme switching used, which crashed in the
    // Filters Panel (#173).
    for ( const QString name :
          { Theme::LightKey, Theme::DarkKey, Theme::HighContrastKey, Theme::LightKey } ) {
        const auto theme = Theme::fromName( name, Qt::ColorScheme::Light );
        qApp->setPalette( theme.palette() );
        qApp->setStyleSheet( theme.styleSheet() );
        QGuiApplication::styleHints()->setColorScheme( theme.isDark() ? Qt::ColorScheme::Dark
                                                                      : Qt::ColorScheme::Light );
        QCoreApplication::processEvents();
    }

    REQUIRE( panel.isVisible() );
    Theme::apply( Theme::defaultTheme() );
}

SCENARIO( "Icons and widget styles follow a Theme switch", "[ui][theme]" )
{
    QTemporaryFile file{ QDir::tempPath() + "/theme_switch_test_XXXXXX" };
    REQUIRE( writeLogFile( file ) );

    Theme::apply( Theme::LightKey );
    MainWindowFixture fixture;
    auto& mainWindow = *fixture.mainWindow;
    mainWindow.loadInitialFile( file.fileName(), false );
    REQUIRE(
        waitUiState( [ & ] { return fixture.tabArea->count() == fixture.baseTabCount + 1; } ) );
    QTest::qWait( 50 );

    const auto lightIcons = toolBarIconImages( mainWindow );
    REQUIRE_FALSE( lightIcons.empty() );
    REQUIRE( fixture.tabArea->tabBar()->styleSheet()
             == closableTabBarStyleSheet( Theme::active() ) );

    WHEN( "the Dark Theme is applied" )
    {
        Theme::apply( Theme::DarkKey );
        QTest::qWait( 50 );

        THEN( "the tool bar shows the inverse icons" )
        {
            REQUIRE( toolBarIconImages( mainWindow ) != lightIcons );
        }

        THEN( "the tab bar is styled for the Dark Theme" )
        {
            REQUIRE( fixture.tabArea->tabBar()->styleSheet()
                     == closableTabBarStyleSheet( Theme::active() ) );
            REQUIRE( fixture.tabArea->tabBar()->styleSheet()
                     != closableTabBarStyleSheet(
                         Theme::fromName( Theme::LightKey, Qt::ColorScheme::Light ) ) );
        }

        THEN( "the application palette is the Dark Theme's" )
        {
            REQUIRE( qApp->palette().color( QPalette::Window )
                     == Theme::active().color( ColorToken::Window ) );
        }
    }

    closeLogFiles( fixture.tabArea, fixture.baseTabCount );
}

SCENARIO( "System follows the operating system's color scheme while running", "[ui][theme]" )
{
    Theme::followSystemColorScheme();
    auto* hints = QGuiApplication::styleHints();

    GIVEN( "the System Theme is chosen" )
    {
        Theme::apply( Theme::SystemKey );

        WHEN( "the operating system turns dark" )
        {
            Q_EMIT hints->colorSchemeChanged( Qt::ColorScheme::Dark );

            THEN( "the application shows the Dark Theme" )
            {
                REQUIRE( Theme::active().name() == Theme::DarkKey );
                REQUIRE( qApp->palette().color( QPalette::Window )
                         == Theme::active().color( ColorToken::Window ) );
            }

            AND_WHEN( "it turns light again" )
            {
                Q_EMIT hints->colorSchemeChanged( Qt::ColorScheme::Light );

                THEN( "the application shows the Light Theme" )
                {
                    REQUIRE( Theme::active().name() == Theme::LightKey );
                }
            }
        }
    }

    GIVEN( "the Light Theme is chosen" )
    {
        Theme::apply( Theme::LightKey );

        WHEN( "the operating system turns dark" )
        {
            Q_EMIT hints->colorSchemeChanged( Qt::ColorScheme::Dark );

            THEN( "the application keeps the Light Theme" )
            {
                REQUIRE( Theme::active().name() == Theme::LightKey );
            }
        }
    }

    Theme::apply( Theme::defaultTheme() );
}

SCENARIO( "A widget refreshes after a switch until it is destroyed", "[ui][theme]" )
{
    int refreshes = 0;
    auto context = std::make_unique<QObject>();
    Theme::whenApplied( context.get(), [ &refreshes ] { ++refreshes; } );

    Theme::apply( Theme::DarkKey );
    REQUIRE( refreshes == 1 );

    context.reset();
    Theme::apply( Theme::LightKey );
    REQUIRE( refreshes == 1 );
}

SCENARIO( "Choosing a Theme in the Options Dialog applies it without a restart", "[ui][theme]" )
{
    auto& config = Configuration::get();
    const auto storedStyle = config.style();
    const auto storedLanguage = config.language();

    LogFormatCatalog catalog;
    OptionsDialog dialog( catalog );
    // Only the setting under test differs from what is stored.
    config.setLanguage( dialog.languageComboBox->currentData().toString() );
    MessageBoxWatcher messageBoxes;

    WHEN( "a different Theme is chosen and applied" )
    {
        const auto current = config.style();
        const auto chosen = current == Theme::DarkKey ? QString( Theme::HighContrastKey )
                                                      : QString( Theme::DarkKey );
        dialog.styleComboBox->setCurrentText( chosen );
        QMetaObject::invokeMethod( &dialog, "updateConfigFromDialog", Qt::DirectConnection );
        QTest::qWait( 50 );

        THEN( "the Theme is in use and no restart is asked for" )
        {
            REQUIRE( Theme::active().name() == chosen );
            REQUIRE_FALSE( messageBoxes.seen() );
        }
    }

    WHEN( "a different language is chosen and applied" )
    {
        if ( dialog.languageComboBox->count() > 1 ) {
            dialog.languageComboBox->setCurrentIndex(
                dialog.languageComboBox->currentIndex() == 0 ? 1 : 0 );
            QMetaObject::invokeMethod( &dialog, "updateConfigFromDialog", Qt::DirectConnection );
            QTest::qWait( 100 );

            THEN( "a restart is still asked for" )
            {
                REQUIRE( messageBoxes.seen() );
            }
        }
    }

    config.setStyle( storedStyle );
    config.setLanguage( storedLanguage );
    config.save();
    Theme::apply( Theme::defaultTheme() );
}
