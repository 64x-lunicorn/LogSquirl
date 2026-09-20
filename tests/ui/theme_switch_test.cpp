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

#include "commandpalette.h"
#include "configuration.h"
#include "crawlerwidget.h"
#include "filteredview.h"
#include "filterspanel.h"
#include "highlightersdialog.h"
#include "iconloader.h"
#include "infoline.h"
#include "logformatcatalog.h"
#include "logtableview.h"
#include "mainwindow.h"
#include "optionsdialog.h"
#include "predefinedfilterscombobox.h"
#include "predefinedfiltersdialog.h"
#include "predefinedfiltersetedit.h"
#include "recentfiles.h"
#include "savedsearches.h"
#include "session.h"
#include "tabbarstyle.h"
#include "tabbedcrawlerwidget.h"
#include "test_policies.h"
#include "test_utils.h"
#include "theme.h"
#include "theme_lists.h"
#include "welcomedashboard.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
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

#include <algorithm>
#include <cmath>

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
        , mainWindow( std::make_unique<MainWindow>(
              WindowSession{ session, "Main", 0 },
              std::make_shared<logsquirl::plugins::ApplicationPlugins>() ) )
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

// The WCAG 2 contrast ratio of two opaque colors, from 1:1 to 21:1.
double contrastRatio( const QColor& a, const QColor& b )
{
    const auto luminance = []( const QColor& color ) {
        const auto linear = []( float value ) {
            const double channel = static_cast<double>( value );
            return channel <= 0.04045 ? channel / 12.92
                                      : std::pow( ( channel + 0.055 ) / 1.055, 2.4 );
        };
        return 0.2126 * linear( color.redF() ) + 0.7152 * linear( color.greenF() )
               + 0.0722 * linear( color.blueF() );
    };
    const auto first = luminance( a );
    const auto second = luminance( b );
    return ( std::max( first, second ) + 0.05 ) / ( std::min( first, second ) + 0.05 );
}

// The highest contrast any pixel of area reaches against background: that of
// the core of the text drawn there, whose edges are blended into background.
double textContrast( const QImage& image, const QRect& area, const QColor& background )
{
    double best = 1.0;
    for ( int y = area.top(); y <= area.bottom(); ++y ) {
        for ( int x = area.left(); x <= area.right(); ++x ) {
            best = std::max( best, contrastRatio( image.pixelColor( x, y ), background ) );
        }
    }
    return best;
}

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
    // Filters Panel (#173): a reproduction, not an enumeration, so it stays a
    // list of its own rather than Theme::builtInThemes() (#358).
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
    // Stands in for the operating system, whose color scheme a test cannot
    // change. Shared, so the source stays valid even if a REQUIRE aborts.
    const auto systemScheme = std::make_shared<Qt::ColorScheme>( Qt::ColorScheme::Light );
    Theme::setSystemColorSchemeSource( [ systemScheme ] { return *systemScheme; } );
    Theme::followSystemColorScheme();
    auto* hints = QGuiApplication::styleHints();

    // What a platform does when the operating system changes its scheme.
    const auto systemTurns = [ systemScheme, hints ]( Qt::ColorScheme scheme ) {
        *systemScheme = scheme;
        Q_EMIT hints->colorSchemeChanged( scheme );
    };

    GIVEN( "the System Theme is chosen" )
    {
        Theme::apply( Theme::SystemKey );
        REQUIRE( Theme::active().name() == Theme::LightKey );

        WHEN( "the operating system turns dark" )
        {
            systemTurns( Qt::ColorScheme::Dark );

            THEN( "nothing is applied inside Qt's handling of the change" )
            {
                REQUIRE( Theme::active().name() == Theme::LightKey );
            }

            AND_WHEN( "the event loop runs" )
            {
                QCoreApplication::processEvents();

                THEN( "the application shows the Dark Theme" )
                {
                    REQUIRE( Theme::active().name() == Theme::DarkKey );
                    REQUIRE( qApp->palette().color( QPalette::Window )
                             == Theme::active().color( ColorToken::Window ) );
                }

                AND_WHEN( "it turns light again" )
                {
                    systemTurns( Qt::ColorScheme::Light );
                    QCoreApplication::processEvents();

                    THEN( "the application shows the Light Theme" )
                    {
                        REQUIRE( Theme::active().name() == Theme::LightKey );
                    }
                }
            }
        }

        WHEN( "a late signal reports a scheme the operating system no longer has" )
        {
            Q_EMIT hints->colorSchemeChanged( Qt::ColorScheme::Dark );
            QCoreApplication::processEvents();

            THEN( "the application keeps the Theme of the current scheme" )
            {
                REQUIRE( Theme::active().name() == Theme::LightKey );
            }
        }
    }

    GIVEN( "the operating system is dark" )
    {
        *systemScheme = Qt::ColorScheme::Dark;

        WHEN( "the System Theme is chosen" )
        {
            Theme::apply( Theme::SystemKey );

            THEN( "the application shows the Dark Theme" )
            {
                REQUIRE( Theme::active().name() == Theme::DarkKey );
            }
        }
    }

    GIVEN( "the Light Theme is chosen" )
    {
        Theme::apply( Theme::LightKey );

        WHEN( "the operating system turns dark" )
        {
            systemTurns( Qt::ColorScheme::Dark );
            QCoreApplication::processEvents();

            THEN( "the application keeps the Light Theme" )
            {
                REQUIRE( Theme::active().name() == Theme::LightKey );
            }
        }
    }

    Theme::setSystemColorSchemeSource( {} );
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

SCENARIO( "Widgets that adjust a palette role follow a Theme switch", "[ui][theme]" )
{
    Theme::apply( Theme::LightKey );
    const auto darkColor = []( ColorToken token ) {
        return Theme::fromName( Theme::DarkKey, Qt::ColorScheme::Light ).color( token );
    };

    GIVEN( "a Predefined Filters combo box built under the Light Theme" )
    {
        PredefinedFiltersComboBox combo( nullptr );
        const auto lightBase = combo.view()->palette().color( QPalette::Base );

        WHEN( "the Dark Theme is applied" )
        {
            Theme::apply( Theme::DarkKey );

            THEN( "its list shows the combo box's new window color as its base" )
            {
                const auto base = combo.view()->palette().color( QPalette::Base );
                REQUIRE( base == combo.palette().color( QPalette::Window ) );
                REQUIRE( base != lightBase );
            }
        }
    }

    GIVEN( "a filter set editor showing a filter under the Light Theme" )
    {
        PredefinedFilterSetEdit edit;
        auto set = PredefinedFilterSet::createNewSet( QStringLiteral( "theme" ) );
        set.addFilter( { QStringLiteral( "name" ), QStringLiteral( "pattern" ), false } );
        edit.setFilterSet( set );
        REQUIRE_FALSE( edit.findChildren<QCheckBox*>().isEmpty() );

        WHEN( "the Dark Theme is applied" )
        {
            Theme::apply( Theme::DarkKey );

            THEN( "its regex check boxes show the Dark window color as their base" )
            {
                for ( auto* checkBox : edit.findChildren<QCheckBox*>() ) {
                    REQUIRE( checkBox->palette().color( QPalette::Base )
                             == darkColor( ColorToken::Window ) );
                }
            }
        }
    }

    GIVEN( "an info line showing its gauge under the Light Theme" )
    {
        InfoLine line;
        line.displayGauge( 10 );

        WHEN( "the Dark Theme is applied and the gauge advances" )
        {
            Theme::apply( Theme::DarkKey );
            line.displayGauge( 20 );

            THEN( "the gauge is drawn in the Dark highlight color" )
            {
                const auto* gradient = line.palette().brush( line.backgroundRole() ).gradient();
                REQUIRE( gradient != nullptr );
                REQUIRE( gradient->stops().front().second == darkColor( ColorToken::Highlight ) );
            }

            AND_WHEN( "the gauge is hidden" )
            {
                line.hideGauge();

                THEN( "the line shows the Dark window color" )
                {
                    REQUIRE( line.palette().color( line.backgroundRole() )
                             == darkColor( ColorToken::Window ) );
                }
            }
        }
    }

    GIVEN( "a label whose own stylesheet names a palette role" )
    {
        QWidget window;
        auto* label = new QLabel( QStringLiteral( "text" ), &window );
        label->setStyleSheet( QStringLiteral( "color: palette(dark);" ) );
        window.show();
        QTest::qWait( 20 );

        WHEN( "the Dark Theme is applied" )
        {
            Theme::apply( Theme::DarkKey );
            QCoreApplication::processEvents();

            THEN( "the role is resolved against the Dark palette" )
            {
                REQUIRE( label->palette().color( QPalette::WindowText )
                         == darkColor( ColorToken::Dark ) );
            }
        }
    }

    GIVEN( "a button whose own stylesheet draws its border in a palette role" )
    {
        QWidget window;
        auto* button = new QPushButton( &window );
        button->setFixedSize( 60, 24 );
        button->setStyleSheet(
            QStringLiteral( "background-color: #ff0000; border: 1px solid palette(mid);" ) );
        window.show();
        QTest::qWait( 20 );

        WHEN( "the Dark Theme is applied" )
        {
            Theme::apply( Theme::DarkKey );
            QCoreApplication::processEvents();

            THEN( "the border is drawn in the Dark mid color" )
            {
                const auto image = button->grab().toImage();
                REQUIRE( image.pixelColor( 0, 12 ).rgb() == darkColor( ColorToken::Mid ).rgb() );
            }
        }
    }

    Theme::apply( Theme::defaultTheme() );
}

SCENARIO( "Choosing a Theme in the Options Dialog applies it without a restart", "[ui][theme]" )
{
    // The dialog reads and writes these. Synced here, so the scenario does
    // not depend on another test having synced them first.
    SavedSearches::getSynced();
    RecentFiles::getSynced();
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
        dialog.buttonBox->button( QDialogButtonBox::Apply )->click();
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
            dialog.buttonBox->button( QDialogButtonBox::Apply )->click();
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

SCENARIO( "Every icon Token names an icon that exists", "[ui][theme]" )
{
    GIVEN( "each built-in Theme" )
    {
        THEN( "every url() of its style Tokens is an icon resource" )
        {
            static const QRegularExpression url( "^url\\((.+)\\)$" );
            for ( const auto& name : Theme::builtInThemes() ) {
                const auto theme = Theme::fromName( name, Qt::ColorScheme::Light );
                for ( std::size_t i = 0; i < StyleTokenCount; ++i ) {
                    const auto token = static_cast<StyleToken>( i );
                    const auto match = url.match( theme.value( token ) );
                    if ( !match.hasMatch() ) {
                        continue;
                    }
                    INFO( name.toStdString() << " " << Theme::tokenName( token ).toStdString() );
                    REQUIRE( QFile::exists( match.captured( 1 ) ) );
                }
            }
        }
    }
}

SCENARIO( "The Dashboard's hints are drawn in the Theme's secondary text color", "[ui][theme]" )
{
    GIVEN( "a Dashboard shown under the Light Theme" )
    {
        Theme::apply( Theme::LightKey );
        WelcomeDashboard dashboard;
        dashboard.show();
        QTest::qWait( 20 );

        const auto hints = [ & ] {
            QList<QLabel*> result;
            for ( auto* label : dashboard.findChildren<QLabel*>() ) {
                if ( label->property( "secondaryText" ).toBool() ) {
                    result.append( label );
                }
            }
            return result;
        };
        REQUIRE_FALSE( hints().isEmpty() );

        WHEN( "the Dark Theme is applied" )
        {
            Theme::apply( Theme::DarkKey );
            QCoreApplication::processEvents();

            THEN( "every hint shows Dark's secondary text color, not a frame role" )
            {
                for ( auto* label : hints() ) {
                    INFO( label->text().toStdString() );
                    REQUIRE( label->palette().color( QPalette::WindowText )
                             == Theme::active().color( ColorToken::SecondaryText ) );
                }
            }
        }

        Theme::apply( Theme::defaultTheme() );
    }
}

SCENARIO( "The Command Palette's badges and shortcuts are readable in every Theme", "[ui][theme]" )
{
    GIVEN( "an open Command Palette with a selected and an unselected command" )
    {
        Theme::apply( Theme::LightKey );
        CommandPalette palette;
        palette.setCommands( {
            { QStringLiteral( "Open File" ),
              QStringLiteral( "File" ),
              QStringLiteral( "Ctrl+O" ),
              {} },
            { QStringLiteral( "Find" ), QStringLiteral( "Edit" ), QStringLiteral( "Ctrl+F" ), {} },
        } );
        palette.show();
        QTest::qWait( 20 );
        auto* list = palette.findChild<QListWidget*>();
        REQUIRE( list != nullptr );
        REQUIRE( list->count() == 2 );
        REQUIRE( list->currentRow() == 0 );

        WHEN( "each Theme is applied in turn" )
        {
            THEN( "every badge has the Theme's badge color, and badge and shortcut text reach "
                  "4.5:1 in the selected and the unselected row" )
            {
                for ( const auto& name : themeRoundTripFrom( Theme::LightKey ) ) {
                    Theme::apply( name );
                    QCoreApplication::processEvents();
                    const auto image = list->viewport()->grab().toImage();
                    const auto& theme = Theme::active();

                    for ( int row = 0; row < list->count(); ++row ) {
                        const auto* item = list->item( row );
                        const auto rect = list->visualItemRect( item );
                        const auto category = item->data( Qt::UserRole + 1 ).toString();
                        const auto shortcut = item->data( Qt::UserRole + 2 ).toString();
                        const auto rowBackground
                            = image.pixelColor( rect.right() - 1, rect.top() + 1 );
                        INFO( name.toStdString()
                              << ( row == 0 ? " selected" : " unselected" ) << " row" );

                        // Where the delegate puts them: the shortcut right-aligned
                        // 8px from the edge, the badge 24px left of it.
                        const auto shortcutWidth
                            = QFontMetrics( list->font() ).horizontalAdvance( shortcut );
                        const QRect shortcutArea( rect.right() - 8 - shortcutWidth, rect.top(),
                                                  shortcutWidth, rect.height() );
                        auto badgeFont = list->font();
                        badgeFont.setPointSizeF( badgeFont.pointSizeF() * 0.85 );
                        const QFontMetrics badgeMetrics( badgeFont );
                        const int badgeWidth = badgeMetrics.boundingRect( category ).width() + 8;
                        const int badgeHeight = badgeMetrics.height() + 2;
                        const QRect badge( rect.right() - badgeWidth - shortcutWidth - 24,
                                           rect.top() + ( rect.height() - badgeHeight ) / 2,
                                           badgeWidth, badgeHeight );
                        const auto badgeBackground
                            = image.pixelColor( badge.right() - 1, badge.center().y() );

                        CHECK( badgeBackground.rgb()
                               == theme.color( ColorToken::BadgeBackground ).rgb() );
                        CHECK(
                            textContrast( image, badge.adjusted( 2, 2, -2, -2 ), badgeBackground )
                            >= 4.5 );
                        CHECK( textContrast( image, shortcutArea, rowBackground ) >= 4.5 );
                    }
                }
            }
        }

        Theme::apply( Theme::defaultTheme() );
    }
}

SCENARIO( "High Contrast shows checked, hovered, disabled and progress states legibly",
          "[ui][theme]" )
{
    GIVEN( "the High Contrast Theme" )
    {
        Theme::apply( Theme::HighContrastKey );
        const auto& theme = Theme::active();

        WHEN( "a tool button with an icon is checked" )
        {
            QToolButton button;
            button.setCheckable( true );
            button.setIcon( IconLoader{}.loadCheckable( "regex" ) );
            button.setChecked( true );
            button.show();
            QTest::qWait( 20 );
            const auto image = button.grab().toImage();

            THEN( "it is yellow and its icon reaches 3:1 against it" )
            {
                const auto background = theme.color( ColorToken::Checked );
                REQUIRE( image.pixelColor( image.width() / 2, 3 ).rgb() == background.rgb() );
                QRect iconArea( QPoint( 0, 0 ), button.iconSize() );
                iconArea.moveCenter( image.rect().center() );
                REQUIRE( textContrast( image, iconArea, background ) >= 3.0 );
            }
        }

        WHEN( "an icon is loaded for anything but a checkable button" )
        {
            const auto icon = IconLoader{}.load( "regex" );

            THEN( "it has no checked variant of its own: a selected tab or checked menu item "
                  "stays on a black background" )
            {
                REQUIRE( icon.availableSizes( QIcon::Normal, QIcon::On ).isEmpty() );
            }
        }

        WHEN( "a push button is hovered" )
        {
            QPushButton button( QStringLiteral( "Button" ) );
            button.setFocusPolicy( Qt::NoFocus );
            button.show();
            QTest::qWait( 20 );
            const auto normal = button.grab().toImage();
            QTest::mouseMove( &button, button.rect().center() );
            QTest::qWait( 20 );
            const auto hovered = button.grab().toImage();

            THEN( "its background and border change" )
            {
                const QPoint inside( 4, button.height() / 2 );
                REQUIRE( hovered.pixelColor( inside ).rgb()
                         == theme.color( ColorToken::ButtonHover ).rgb() );
                REQUIRE( normal.pixelColor( inside ).rgb() != hovered.pixelColor( inside ).rgb() );
                const QPoint border( button.width() / 2, 0 );
                REQUIRE( hovered.pixelColor( border ).rgb()
                         == theme.color( ColorToken::InputHoverBorder ).rgb() );
            }
        }

        WHEN( "a push button is disabled" )
        {
            QPushButton button( QStringLiteral( "Button" ) );
            button.setEnabled( false );
            button.show();
            QTest::qWait( 20 );
            const auto image = button.grab().toImage();

            THEN( "its border is drawn in the disabled border color, never the enabled one" )
            {
                const auto disabledBorder = theme.color( ColorToken::DisabledBorder ).rgb();
                bool found = false;
                for ( int x = 4; x < image.width() - 4; ++x ) {
                    const auto pixel = image.pixelColor( x, 0 ).rgb();
                    REQUIRE( pixel != theme.color( ColorToken::InputBorder ).rgb() );
                    found = found || pixel == disabledBorder;
                }
                REQUIRE( found );
            }
        }

        WHEN( "a progress bar is half filled" )
        {
            QProgressBar bar;
            bar.setRange( 0, 100 );
            bar.setValue( 50 );
            bar.setTextVisible( true );
            bar.resize( 200, 16 );
            bar.show();
            QTest::qWait( 20 );
            const auto image = bar.grab().toImage();

            THEN( "its text reaches 4.5:1 over the filled and over the empty part" )
            {
                // The text on either side of the chunk's end, kept clear of
                // the bar's border and the chunk's outline, whose own contrast
                // would pass for the text's.
                const int clear = 5;
                const auto textWidth = QFontMetrics( bar.font() ).horizontalAdvance( bar.text() );
                const auto middle = image.width() / 2;
                const QRect filledText( middle - textWidth / 2, clear, textWidth / 2 - clear,
                                        image.height() - 2 * clear );
                const QRect emptyText( middle + clear, clear, textWidth / 2 - clear,
                                       image.height() - 2 * clear );
                const auto filled = image.pixelColor( 6, image.height() / 2 );
                const auto empty = image.pixelColor( image.width() - 6, image.height() / 2 );
                REQUIRE( textContrast( image, filledText, filled ) >= 4.5 );
                REQUIRE( textContrast( image, emptyText, empty ) >= 4.5 );
            }
        }

        Theme::apply( Theme::defaultTheme() );
    }
}
