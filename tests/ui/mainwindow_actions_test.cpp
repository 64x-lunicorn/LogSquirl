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
#include "logformatcatalog.h"
#include "mainwindow.h"
#include "releasepages.h"
#include "session.h"
#include "shortcuts.h"
#include "shown_widget.h"
#include "test_policies.h"

#include <QAction>
#include <QApplication>
#include <QTest>
#include <QTranslator>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

// The window's actions are named, translated again and given their shortcuts
// from one table (#950).

namespace {

constexpr auto ActionContext = "logsquirl::mainwindow::action";

/// A language in which every text of the actions' context is its source
/// behind a marker, and every other text is left as it is.
class ActionMarkingTranslator : public QTranslator {
public:
    static QString marked( const char* source )
    {
        return QStringLiteral( "[xx] " ) + QString::fromUtf8( source );
    }

    QString translate( const char* context, const char* sourceText,
                       const char* /*disambiguation*/ = nullptr, int /*n*/ = -1 ) const override
    {
        if ( context == nullptr || std::strcmp( context, ActionContext ) != 0
             || sourceText == nullptr || *sourceText == '\0' ) {
            return {};
        }
        return marked( sourceText );
    }

    bool isEmpty() const override
    {
        return false;
    }
};

struct ShownWindow {
    ShownWindow()
        : session( std::make_shared<Session>( testSettingsPolicies(),
                                              std::make_shared<LogFormatCatalog>() ) )
        , window( std::make_unique<MainWindow>(
              WindowSession{ session, QStringLiteral( "ActionTable" ), 0 },
              std::make_shared<logsquirl::plugins::ApplicationPlugins>() ) )
    {
        showUntilExposed( *window );
    }

    std::shared_ptr<Session> session;
    std::unique_ptr<MainWindow> window;
};

/// The text an action of the table shows for `translated`, its translation:
/// What's New names the running version in it.
QString shownText( const QString& translated )
{
    return translated.contains( QStringLiteral( "%1" ) )
               ? translated.arg( logsquirl::versioncheck::runningVersionMonth() )
               : translated;
}

} // namespace

TEST_CASE( "Every action of the table has a text and a registered shortcut id",
           "[ui][mainwindow][actiontable]" )
{
    const auto table = MainWindow::actionTable();
    REQUIRE( table.size() >= 60 );

    const auto& registered = ShortcutAction::defaultShortcutList();
    // Member pointers are equal or not, without an order.
    std::vector<QAction * MainWindow::*> members;
    for ( const auto& entry : table ) {
        INFO( ( entry.text != nullptr ? entry.text : "(no text)" ) );
        REQUIRE( entry.action != nullptr );
        CHECK( std::find( members.cbegin(), members.cend(), entry.action ) == members.cend() );
        members.push_back( entry.action );
        REQUIRE( entry.text != nullptr );
        CHECK( *entry.text != '\0' );
        if ( entry.statusTip != nullptr ) {
            CHECK( *entry.statusTip != '\0' );
        }
        if ( entry.shortcutId != nullptr ) {
            CHECK( registered.contains( entry.shortcutId ) );
        }
    }
}

SCENARIO( "The window's actions are named and given their shortcuts from the table",
          "[ui][mainwindow][actiontable]" )
{
    GIVEN( "a shown window" )
    {
        const ShownWindow shown;
        auto& window = *shown.window;
        const auto shortcuts = Configuration::get().shortcuts();

        THEN( "every action shows the text, status tip and keys of its entry" )
        {
            for ( const auto& entry : MainWindow::actionTable() ) {
                INFO( entry.text );
                const auto* action = window.*entry.action;
                REQUIRE( action != nullptr );
                CHECK( action->text() == shownText( QString::fromUtf8( entry.text ) ) );
                if ( entry.statusTip != nullptr ) {
                    CHECK( action->statusTip() == QString::fromUtf8( entry.statusTip ) );
                }
                if ( entry.shortcutId != nullptr ) {
                    CHECK( action->shortcuts()
                           == ShortcutAction::shortcutKeys( entry.shortcutId, shortcuts ) );
                }
            }
        }

        WHEN( "the language changes" )
        {
            ActionMarkingTranslator translator;
            REQUIRE( QCoreApplication::installTranslator( &translator ) );
            QCoreApplication::sendPostedEvents( nullptr, QEvent::LanguageChange );
            QCoreApplication::processEvents();

            THEN( "every action shows its translation, from the actions' context" )
            {
                for ( const auto& entry : MainWindow::actionTable() ) {
                    INFO( entry.text );
                    const auto* action = window.*entry.action;
                    CHECK( action->text()
                           == shownText( ActionMarkingTranslator::marked( entry.text ) ) );
                    if ( entry.statusTip != nullptr ) {
                        CHECK( action->statusTip()
                               == ActionMarkingTranslator::marked( entry.statusTip ) );
                    }
                }
            }

            QCoreApplication::removeTranslator( &translator );
            QCoreApplication::sendPostedEvents( nullptr, QEvent::LanguageChange );
            QCoreApplication::processEvents();

            THEN( "going back shows every source again" )
            {
                for ( const auto& entry : MainWindow::actionTable() ) {
                    INFO( entry.text );
                    CHECK( ( window.*entry.action )->text()
                           == shownText( QString::fromUtf8( entry.text ) ) );
                }
            }
        }
    }
}
