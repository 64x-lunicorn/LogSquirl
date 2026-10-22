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

// A Log Line, a Named Value, a file name or a plugin catalog string shown in
// a label, a tool tip or a message box reads literally: markup in it never
// changes the layout, hides content or inserts an image (#830).

#include <catch2/catch_test_macros.hpp>

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTest>
#include <QTextDocument>
#include <QTimer>

#include <memory>

#include "abstractlogview.h"
#include "applicationplugins.h"
#include "configuration.h"
#include "infoline.h"
#include "logformatcatalog.h"
#include "mainwindow.h"
#include "mainwindowtext.h"
#include "plaintext.h"
#include "plugindialog.h"
#include "session.h"
#include "tabbedcrawlerwidget.h"
#include "test_policies.h"
#include "test_utils.h"
#include "valuenamer.h"

namespace {

constexpr int UiTimeoutMs = 10'000;

// Bold and image markup in a name; no closing tag, as a slash ends a name.
const auto MarkupName = QStringLiteral( "log <b>bold<img src=x>.log" );

// What a tool tip with this text shows: QToolTip renders a text that might be
// rich text as such, and any other as it is.
QString asShownInToolTip( const QString& toolTip )
{
    if ( !Qt::mightBeRichText( toolTip ) ) {
        return toolTip;
    }
    QTextDocument document;
    document.setHtml( toolTip );
    return document.toPlainText();
}

QAction* fileMenuAction( const MainWindow& window, const char* text )
{
    const auto translated = QApplication::translate( "logsquirl::mainwindow::action", text );
    for ( auto* action : window.findChildren<QAction*>() ) {
        if ( action->text() == translated ) {
            return action;
        }
    }
    return nullptr;
}

// A main window over a Session of its own, shown and active.
struct Window {
    Window()
        : session( std::make_shared<Session>( testSettingsPolicies(),
                                              std::make_shared<LogFormatCatalog>() ) )
        , plugins( std::make_shared<logsquirl::plugins::ApplicationPlugins>() )
    {
        mainWindow = std::make_unique<MainWindow>( WindowSession{ session, "Main", 0 }, plugins );
        mainWindow->show();
        mainWindow->activateWindow();
        REQUIRE( QTest::qWaitForWindowActive( mainWindow.get(), 5000 ) );
        tabArea = mainWindow->findChild<TabbedCrawlerWidget*>();
        REQUIRE( tabArea != nullptr );
    }

    ~Window()
    {
        mainWindow.reset();
        QTest::qWait( 50 );
    }

    Window( const Window& ) = delete;
    Window& operator=( const Window& ) = delete;

    // The tab that holds this Log File, by its data rather than its tool tip.
    int tabOf( const QString& path ) const
    {
        return tabArea->tabOfPath( path );
    }

    std::shared_ptr<Session> session;
    std::shared_ptr<logsquirl::plugins::ApplicationPlugins> plugins;
    std::unique_ptr<MainWindow> mainWindow;
    TabbedCrawlerWidget* tabArea = nullptr;
};

// Turns the confirmation of a tab close on for its lifetime.
struct ConfirmTabClose {
    ConfirmTabClose()
        : previous( Configuration::get().confirmTabClose() )
    {
        Configuration::get().setConfirmTabClose( true );
    }
    ~ConfirmTabClose()
    {
        Configuration::get().setConfirmTabClose( previous );
    }
    ConfirmTabClose( const ConfirmTabClose& ) = delete;
    ConfirmTabClose& operator=( const ConfirmTabClose& ) = delete;

    bool previous;
};

// Keeps the text and the text format of the next question the window asks,
// and answers No.
class QuestionKeeper {
public:
    QuestionKeeper()
    {
        QObject::connect( &driver_, &QTimer::timeout, [ this ] {
            auto* box = qobject_cast<QMessageBox*>( QApplication::activeModalWidget() );
            if ( box == nullptr || !box->isVisible() || box->property( "kept" ).toBool() ) {
                return;
            }
            box->setProperty( "kept", true );
            text_ = box->text();
            format_ = box->textFormat();
            box->button( QMessageBox::No )->click();
        } );
        driver_.start( 10 );
    }

    const QString& text() const
    {
        return text_;
    }
    Qt::TextFormat format() const
    {
        return format_;
    }

private:
    QTimer driver_;
    QString text_;
    Qt::TextFormat format_ = Qt::AutoText;
};

// Writes a plugin.json with these catalog strings into its own subdirectory
// of root, for a library that exists.
void installPlugin( const QString& root, const QString& name, const QString& author,
                    const QString& description )
{
    const auto pluginDir = QDir( root ).filePath( QStringLiteral( "com.example.markup" ) );
    REQUIRE( QDir().mkpath( pluginDir ) );
    QFile manifest( QDir( pluginDir ).filePath( "plugin.json" ) );
    REQUIRE( manifest.open( QIODevice::WriteOnly ) );
    manifest.write( QStringLiteral( R"({
        "id": "com.example.markup",
        "name": "%1",
        "author": "%2",
        "description": "%3",
        "version": "1.0.0",
        "type": "ui",
        "library": "%4",
        "api_version": 1
    })" )
                        .arg( name, author, description,
                              QStringLiteral( LOGSQUIRL_TEST_LOG_LINES_PLUGIN_PATH ) )
                        .toUtf8() );
}

QLabel* labelWithText( const QWidget& widget, const QString& text )
{
    for ( auto* label : widget.findChildren<QLabel*>() ) {
        if ( label->text() == text ) {
            return label;
        }
    }
    return nullptr;
}

} // namespace

SCENARIO( "A Log File named with markup shows that name literally", "[ui][plaintext]" )
{
#ifdef Q_OS_WIN
    SKIP( "Windows allows no '<' in a file name" );
#else
    QTemporaryDir dir;
    REQUIRE( dir.isValid() );
    const auto path = dir.filePath( MarkupName );
    const auto nativePath = QDir::toNativeSeparators( path );
    {
        QFile file( path );
        REQUIRE( file.open( QIODevice::WriteOnly ) );
        file.write( "one Log Line\n" );
    }
    const ConfirmTabClose confirmation;
    Window window;

    GIVEN( "the Log File open in its tab" )
    {
        window.mainWindow->loadFileNonInteractive( path );
        REQUIRE( waitUiState( [ & ] { return window.tabOf( path ) >= 0; }, UiTimeoutMs ) );
        const auto tab = window.tabOf( path );

        THEN( "the info line shows the name as plain text" )
        {
            auto* infoLine = window.mainWindow->findChild<InfoLine*>();
            REQUIRE( infoLine != nullptr );
            REQUIRE( waitUiState( [ & ] { return infoLine->text() == nativePath; }, UiTimeoutMs ) );
            REQUIRE( infoLine->textFormat() == Qt::PlainText );
        }

        THEN( "the tab's tool tip shows the path literally" )
        {
            REQUIRE( asShownInToolTip( window.tabArea->tabToolTip( tab ) ) == nativePath );
        }

        THEN( "the recent files menu names it literally, tool tip included" )
        {
            QMenu* recentMenu = nullptr;
            for ( auto* menu : window.mainWindow->findChildren<QMenu*>() ) {
                if ( menu->title() == QApplication::translate( "MainWindow", "Open Recent" ) ) {
                    recentMenu = menu;
                }
            }
            REQUIRE( recentMenu != nullptr );
            QAction* entry = nullptr;
            for ( auto* action : recentMenu->actions() ) {
                if ( action->data().toString() == path ) {
                    entry = action;
                }
            }
            REQUIRE( entry != nullptr );
            REQUIRE( entry->text().endsWith( MarkupName ) );
            REQUIRE( asShownInToolTip( entry->toolTip() ) == path );
        }

        WHEN( "the tab is closed" )
        {
            QuestionKeeper question;
            window.tabArea->setCurrentIndex( tab );
            auto* close
                = fileMenuAction( *window.mainWindow, logsquirl::mainwindow::action::closeText );
            REQUIRE( close != nullptr );
            close->trigger();

            THEN( "the question names the Log File as plain text" )
            {
                REQUIRE( question.text()
                         == QStringLiteral( "Close tab \"%1\"?" ).arg( MarkupName ) );
                REQUIRE( question.format() == Qt::PlainText );
            }
        }
    }
#endif
}

SCENARIO( "A Named Value whose text holds markup shows it literally in its tool tip",
          "[ui][plaintext][valuenames]" )
{
    GIVEN( "a Named Value whose raw value and name hold markup" )
    {
        logsquirl::valuenames::NamedValue value;
        value.value = QStringLiteral( "<b>0x15</b>" );
        value.name = QStringLiteral( "<img src=x>Beispiel" );
        value.table = QStringLiteral( "ECU" );
        value.rule = QStringLiteral( "BAP ECU" );
        value.group = QStringLiteral( "BAP" );

        THEN( "the tool tip reads the markup literally" )
        {
            const auto toolTip = AbstractLogView::valueNameToolTip( value );
            REQUIRE( asShownInToolTip( toolTip )
                     == QStringLiteral( "<b>0x15</b> → <img src=x>Beispiel · table ECU · rule BAP "
                                        "ECU · group BAP" ) );
        }
    }

    GIVEN( "a Named Value without markup" )
    {
        logsquirl::valuenames::NamedValue value;
        value.value = QStringLiteral( "0x15" );
        value.name = QStringLiteral( "Beispiel" );
        value.table = QStringLiteral( "ECU" );
        value.rule = QStringLiteral( "BAP ECU" );
        value.group = QStringLiteral( "BAP" );

        THEN( "the tool tip is the text itself" )
        {
            REQUIRE(
                AbstractLogView::valueNameToolTip( value )
                == QStringLiteral( "0x15 → Beispiel · table ECU · rule BAP ECU · group BAP" ) );
        }
    }
}

SCENARIO( "A tool tip text reads literally whatever it holds", "[ui][plaintext]" )
{
    GIVEN( "texts with and without markup, on one line and on several" )
    {
        for ( const auto& text :
              { QStringLiteral( "C:\\logs\\<b>bold</b>.log" ), QStringLiteral( "plain.log" ),
                QStringLiteral( "first <i>line</i>\nsecond & third\n\n<img src=x>" ),
                QStringLiteral( "&lt;not a tag&gt;" ), QStringLiteral( "two  spaces\ttab" ) } ) {
            CAPTURE( text );
            THEN( "the tool tip shows the text as it is" )
            {
                REQUIRE( asShownInToolTip( logsquirl::plaintext::toolTip( text ) ) == text );
            }
        }
    }
}

SCENARIO( "Plugin strings from the catalog show markup literally in the plugin dialog",
          "[ui][plaintext][plugins]" )
{
    const auto name = QStringLiteral( "<b>Bold</b> plugin" );
    const auto author = QStringLiteral( "<img src=x>Somebody" );
    const auto description = QStringLiteral( "<i>Reads</i> everything" );

    QTemporaryDir pluginRoot;
    REQUIRE( pluginRoot.isValid() );
    installPlugin( pluginRoot.path(), name, author, description );

    GIVEN( "the plugin dialog over a catalog with such a plugin" )
    {
        logsquirl::plugins::ApplicationPlugins plugins;
        plugins.catalog().discoverPluginsIn( pluginRoot.path() );
        PluginDialog dialog( plugins );

        THEN( "its name, author and description are plain text labels" )
        {
            for ( const auto& text : { name, author, description } ) {
                CAPTURE( text );
                auto* label = labelWithText( dialog, text );
                REQUIRE( label != nullptr );
                REQUIRE( label->textFormat() == Qt::PlainText );
            }
        }

        THEN( "every label of the dialog is plain text" )
        {
            const auto labels = dialog.findChildren<QLabel*>();
            REQUIRE( !labels.isEmpty() );
            for ( const auto* label : labels ) {
                CAPTURE( label->objectName(), label->text() );
                REQUIRE( label->textFormat() == Qt::PlainText );
            }
        }
    }
}

SCENARIO( "The widgets that show a file name or a Log Line are plain text", "[ui][plaintext]" )
{
    GIVEN( "a main window" )
    {
        Window window;

        THEN( "every info line of the window is plain text" )
        {
            const auto infoLines = window.mainWindow->findChildren<InfoLine*>();
            REQUIRE( !infoLines.isEmpty() );
            for ( const auto* infoLine : infoLines ) {
                CAPTURE( infoLine->objectName(), infoLine->text() );
                REQUIRE( infoLine->textFormat() == Qt::PlainText );
            }
        }
    }
}
