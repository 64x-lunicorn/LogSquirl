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

#include "crawlerwidget.h"
#include "highlighterset.h"
#include "logformatcatalog.h"
#include "mainwindow.h"
#include "pathline.h"
#include "recentfiles.h"
#include "session.h"
#include "test_policies.h"
#include "test_utils.h"
#include "theme.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QGridLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMenuBar>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSlider>
#include <QSpinBox>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTest>
#include <QTimer>
#include <QToolButton>
#include <QTreeWidget>

#include <catch2/catch.hpp>

#include <algorithm>
#include <functional>
#include <vector>

// Renders every Theme to PNG files, so a Theme change can be compared before
// and after, side by side (#253). Hidden: it runs only when asked for by its
// tag, never in the normal test run or in CI. See BUILD.md for how to run it.
//
// Each file is named <view>_<theme>.png, so the Themes of one view sort
// together.

namespace {

const std::vector<QString> RenderedThemes{ Theme::LightKey, Theme::DarkKey, Theme::HighContrastKey,
                                           Theme::SmyckKey };

// The Log File shown is a copy of the demo log in a neutral directory, the
// same on every machine: the tool bar shows the full path of the Log File, and
// the path of the checkout or the home directory must not appear in the
// images. The same path and modification time also keep the images of two
// runs comparable.
#ifdef Q_OS_WIN
const QString NeutralLogDirectory = QDir::tempPath() + QStringLiteral( "/logsquirl-screenshots" );
#else
const QString NeutralLogDirectory = QStringLiteral( "/tmp/logsquirl-screenshots" );
#endif
const QString NeutralLogFileName = QStringLiteral( "acorn-store.log" );

QString slug( QString text )
{
    text = text.remove( '&' ).toLower();
    text.replace( QRegularExpression( QStringLiteral( "[^a-z0-9]+" ) ), QStringLiteral( "-" ) );
    return text.remove( QRegularExpression( QStringLiteral( "^-+|-+$" ) ) );
}

// Paths of this machine that must not appear in an image.
QStringList localPaths()
{
    QStringList paths{ QDir::homePath(), QStringLiteral( LOGSQUIRL_SOURCE_DIR ),
                       QCoreApplication::applicationDirPath() };
    paths.removeAll( QDir::rootPath() );
    return paths;
}

// Every text widget shows below root, as far as a widget tells it. The Log
// File's lines are painted, not held in widgets; the demo log names no path.
QStringList shownTexts( QWidget* root )
{
    QStringList texts;
    auto widgets = root->findChildren<QWidget*>();
    widgets.prepend( root );
    for ( auto* widget : widgets ) {
        if ( widget != root && !widget->isVisibleTo( root ) ) {
            continue;
        }
        if ( auto* label = qobject_cast<QLabel*>( widget ) ) {
            texts << label->text();
        }
        else if ( auto* lineEdit = qobject_cast<QLineEdit*>( widget ) ) {
            texts << lineEdit->text();
        }
        else if ( auto* button = qobject_cast<QAbstractButton*>( widget ) ) {
            texts << button->text();
        }
        else if ( auto* textEdit = qobject_cast<QPlainTextEdit*>( widget ) ) {
            texts << textEdit->toPlainText();
        }
        else if ( auto* tabBar = qobject_cast<QTabBar*>( widget ) ) {
            for ( int i = 0; i < tabBar->count(); ++i ) {
                texts << tabBar->tabText( i );
            }
        }
        else if ( auto* menu = qobject_cast<QMenu*>( widget ) ) {
            for ( auto* action : menu->actions() ) {
                texts << action->text();
            }
        }
        else if ( auto* view = qobject_cast<QAbstractItemView*>( widget ) ) {
            const auto* model = view->model();
            std::function<void( const QModelIndex& )> collect = [ & ]( const QModelIndex& parent ) {
                for ( int row = 0; model && row < model->rowCount( parent ); ++row ) {
                    for ( int column = 0; column < model->columnCount( parent ); ++column ) {
                        texts << model->index( row, column, parent ).data().toString();
                    }
                    collect( model->index( row, 0, parent ) );
                }
            };
            collect( {} );
        }
    }
    return texts;
}

class Screenshots {
public:
    explicit Screenshots( QString directory )
        : directory_( std::move( directory ) )
    {
    }

    void setTheme( const QString& theme )
    {
        themeSlug_ = slug( theme );
    }

    void save( QWidget* widget, const QString& view ) const
    {
        save( widget, widget->grab(), view );
    }

    // Saves image, rendered from widget, whose texts are checked for local
    // paths.
    void save( QWidget* widget, const QPixmap& image, const QString& view ) const
    {
        const auto fileName = QStringLiteral( "%1/%2_%3.png" ).arg( directory_, view, themeSlug_ );
        INFO( fileName.toStdString() );
        QStringList textsWithLocalPaths;
        const auto paths = localPaths();
        for ( const auto& text : shownTexts( widget ) ) {
            if ( std::any_of( paths.begin(), paths.end(),
                              [ &text ]( const auto& path ) { return text.contains( path ); } ) ) {
                textsWithLocalPaths << text;
            }
        }
        CHECK( textsWithLocalPaths.join( '\n' ).toStdString() == "" );
        CHECK( image.save( fileName ) );
    }

private:
    QString directory_;
    QString themeSlug_;
};

void settle( int ms = 150 )
{
    QTest::qWait( ms );
}

// A window with every standard widget in every state a Token styles, one row
// per widget, one column per state.
//
// Hover is left out: it follows the mouse pointer, which the offscreen
// platform does not have, so no hover state can be rendered. Selected text of
// an input shows only while the input has the focus: the inputs of the Focused
// column have all their text selected, as a focus by Tab selects it.
class WidgetGallery {
public:
    enum Column { Normal, Checked, Indeterminate, Disabled, DisabledChecked, Focused, Selected };

    WidgetGallery()
    {
        window_.setWindowTitle( QStringLiteral( "Widget gallery" ) );
        grid_ = new QGridLayout( &window_ );
        const QStringList headers{
            "Normal",  "Checked", "Indeterminate", "Disabled", "Disabled + checked",
            "Focused", "Selected"
        };
        for ( int column = 0; column < headers.size(); ++column ) {
            grid_->addWidget( new QLabel( "<b>" + headers[ column ] + "</b>" ), 0, column + 1 );
        }

        addButtons<QPushButton>( "Push button" );
        addRow( "Default button", { { Normal, defaultButton() },
                                    { Disabled, disabled( defaultButton() ) },
                                    { Focused, defaultButton() } } );
        addButtons<QToolButton>( "Tool button" );
        addCheckBoxes();
        addRadioButtons();
        addLineEdits();
        addComboBoxes();
        addSpinBoxes();
        addSliders();
        addProgressBars();
        addTabs();
        addItemViews<QTreeWidget>( "Tree" );
        addItemViews<QTableWidget>( "Table" );
        addItemViews<QListWidget>( "List" );
    }

    QWidget* window()
    {
        return &window_;
    }

    // The gallery with the widgets of the Focused column drawn focused, one
    // after another, as only one widget of a window can have the focus.
    QPixmap render()
    {
        QPixmap image = window_.grab();
        QPainter painter( &image );
        for ( auto* widget : focused_ ) {
            widget->setFocus( Qt::TabFocusReason );
            settle( 50 );
            CHECK( widget->hasFocus() );
            painter.drawPixmap( widget->mapTo( &window_, QPoint{} ), widget->grab() );
        }
        return image;
    }

    QComboBox* comboBox() const
    {
        return comboBox_;
    }

private:
    void addRow( const QString& name, std::initializer_list<std::pair<Column, QWidget*>> cells )
    {
        ++row_;
        grid_->addWidget( new QLabel( name ), row_, 0 );
        for ( const auto& [ column, widget ] : cells ) {
            grid_->addWidget( widget, row_, column + 1 );
            if ( column == Focused ) {
                widget->setFocusPolicy( Qt::StrongFocus );
                focused_.push_back( widget );
            }
            else if ( widget->focusPolicy() != Qt::NoFocus ) {
                widget->setFocusPolicy( Qt::ClickFocus );
            }
        }
    }

    static QWidget* disabled( QWidget* widget )
    {
        widget->setEnabled( false );
        return widget;
    }

    static QWidget* defaultButton()
    {
        auto* button = new QPushButton( "Default" );
        button->setDefault( true );
        return button;
    }

    static QWidget* button( const char* className, bool checked )
    {
        QAbstractButton* button = QString( className ) == "QToolButton"
                                      ? static_cast<QAbstractButton*>( new QToolButton )
                                      : new QPushButton;
        button->setText( checked ? "Checked" : "Button" );
        button->setCheckable( checked );
        button->setChecked( checked );
        return button;
    }

    template <typename Button>
    void addButtons( const QString& name )
    {
        const auto* className = Button::staticMetaObject.className();
        addRow( name, { { Normal, button( className, false ) },
                        { Checked, button( className, true ) },
                        { Disabled, disabled( button( className, false ) ) },
                        { DisabledChecked, disabled( button( className, true ) ) },
                        { Focused, button( className, false ) } } );
    }

    static QWidget* checkBox( Qt::CheckState state )
    {
        auto* box = new QCheckBox( "Check box" );
        box->setTristate( state == Qt::PartiallyChecked );
        box->setCheckState( state );
        return box;
    }

    void addCheckBoxes()
    {
        addRow( "Check box", { { Normal, checkBox( Qt::Unchecked ) },
                               { Checked, checkBox( Qt::Checked ) },
                               { Indeterminate, checkBox( Qt::PartiallyChecked ) },
                               { Disabled, disabled( checkBox( Qt::Unchecked ) ) },
                               { DisabledChecked, disabled( checkBox( Qt::Checked ) ) },
                               { Focused, checkBox( Qt::Unchecked ) } } );
    }

    // Each radio button in a widget of its own, so that checking one does not
    // uncheck the others.
    static QWidget* radioButton( bool checked )
    {
        auto* button = new QRadioButton( "Radio button" );
        button->setAutoExclusive( false );
        button->setChecked( checked );
        return button;
    }

    void addRadioButtons()
    {
        addRow( "Radio button", { { Normal, radioButton( false ) },
                                  { Checked, radioButton( true ) },
                                  { Disabled, disabled( radioButton( false ) ) },
                                  { DisabledChecked, disabled( radioButton( true ) ) },
                                  { Focused, radioButton( false ) } } );
    }

    static QWidget* lineEdit( bool placeholder )
    {
        auto* edit = new QLineEdit;
        if ( placeholder ) {
            edit->setPlaceholderText( "Placeholder" );
        }
        else {
            edit->setText( "Line edit" );
        }
        return edit;
    }

    void addLineEdits()
    {
        addRow( "Line edit", { { Normal, lineEdit( false ) },
                               { Disabled, disabled( lineEdit( false ) ) },
                               { Focused, lineEdit( false ) } } );
        addRow( "Placeholder", { { Normal, lineEdit( true ) },
                                 { Disabled, disabled( lineEdit( true ) ) },
                                 { Focused, lineEdit( true ) } } );
    }

    static QComboBox* comboBox( bool editable )
    {
        auto* box = new QComboBox;
        box->setEditable( editable );
        box->addItems( { editable ? "Editable combo" : "Combo box", "Second item", "Third item" } );
        return box;
    }

    void addComboBoxes()
    {
        comboBox_ = comboBox( false );
        addRow( "Combo box", { { Normal, comboBox_ },
                               { Disabled, disabled( comboBox( false ) ) },
                               { Focused, comboBox( false ) } } );
        addRow( "Editable combo", { { Normal, comboBox( true ) },
                                    { Disabled, disabled( comboBox( true ) ) },
                                    { Focused, comboBox( true ) } } );
    }

    static QWidget* spinBox()
    {
        auto* box = new QSpinBox;
        box->setValue( 42 );
        return box;
    }

    void addSpinBoxes()
    {
        addRow( "Spin box", { { Normal, spinBox() },
                              { Disabled, disabled( spinBox() ) },
                              { Focused, spinBox() } } );
    }

    static QWidget* slider()
    {
        auto* slider = new QSlider( Qt::Horizontal );
        slider->setValue( 40 );
        return slider;
    }

    void addSliders()
    {
        addRow(
            "Slider",
            { { Normal, slider() }, { Disabled, disabled( slider() ) }, { Focused, slider() } } );
    }

    static QWidget* progressBar( bool busy )
    {
        auto* bar = new QProgressBar;
        if ( busy ) {
            // A busy indicator: animated, so it differs between runs.
            bar->setRange( 0, 0 );
        }
        else {
            bar->setValue( 60 );
        }
        return bar;
    }

    void addProgressBars()
    {
        addRow( "Progress bar", { { Normal, progressBar( false ) },
                                  { Indeterminate, progressBar( true ) },
                                  { Disabled, disabled( progressBar( false ) ) } } );
    }

    // The first tab is the selected one; the third is disabled.
    static QWidget* tabs()
    {
        auto* tabs = new QTabWidget;
        tabs->addTab( new QLabel( "Page" ), "Tab" );
        tabs->addTab( new QWidget, "Other" );
        tabs->addTab( new QWidget, "Off" );
        tabs->setTabEnabled( 2, false );
        tabs->tabBar()->setFocusPolicy( Qt::StrongFocus );
        tabs->setFixedHeight( 64 );
        return tabs;
    }

    void addTabs()
    {
        addRow( "Tabs",
                { { Normal, tabs() }, { Disabled, disabled( tabs() ) }, { Focused, tabs() } } );
    }

    enum class ItemState { None, Checks, Current };

    template <typename View>
    static QWidget* itemView( ItemState state )
    {
        auto* view = new View;
        const QStringList texts{ "First item", "Second item", "Third item" };
        const QList<Qt::CheckState> checks{ Qt::Checked, Qt::PartiallyChecked, Qt::Unchecked };
        if ( auto* tree = qobject_cast<QTreeWidget*>( view ) ) {
            tree->setHeaderLabels( { "Name", "Value" } );
            for ( int i = 0; i < texts.size(); ++i ) {
                auto* item = new QTreeWidgetItem( tree, { texts[ i ], QString::number( i ) } );
                if ( state == ItemState::Checks ) {
                    item->setCheckState( 0, checks[ i ] );
                }
            }
        }
        else if ( auto* table = qobject_cast<QTableWidget*>( view ) ) {
            table->setRowCount( static_cast<int>( texts.size() ) );
            table->setColumnCount( 2 );
            table->setAlternatingRowColors( true );
            for ( int i = 0; i < texts.size(); ++i ) {
                auto* item = new QTableWidgetItem( texts[ i ] );
                if ( state == ItemState::Checks ) {
                    item->setCheckState( checks[ i ] );
                }
                table->setItem( i, 0, item );
                table->setItem( i, 1, new QTableWidgetItem( QString::number( i ) ) );
            }
        }
        else if ( auto* list = qobject_cast<QListWidget*>( view ) ) {
            for ( int i = 0; i < texts.size(); ++i ) {
                auto* item = new QListWidgetItem( texts[ i ], list );
                if ( state == ItemState::Checks ) {
                    item->setCheckState( checks[ i ] );
                }
            }
        }
        if ( state == ItemState::Current ) {
            view->setCurrentIndex( view->model()->index( 1, 0 ) );
        }
        else {
            view->selectionModel()->clear();
        }
        view->setFixedSize( 190, 110 );
        return view;
    }

    template <typename View>
    void addItemViews( const QString& name )
    {
        addRow( name, { { Normal, itemView<View>( ItemState::None ) },
                        { Checked, itemView<View>( ItemState::Checks ) },
                        { Disabled, disabled( itemView<View>( ItemState::Current ) ) },
                        { Focused, itemView<View>( ItemState::Current ) },
                        { Selected, itemView<View>( ItemState::Current ) } } );
    }

    QWidget window_;
    QGridLayout* grid_ = nullptr;
    int row_ = 0;
    std::vector<QWidget*> focused_;
    QComboBox* comboBox_ = nullptr;
};

void renderGallery( const Screenshots& screenshots )
{
    WidgetGallery gallery;
    auto* window = gallery.window();
    window->show();
    window->activateWindow();
    REQUIRE( QTest::qWaitForWindowActive( window ) );
    settle();
    screenshots.save( window, gallery.render(), "gallery" );

    auto* combo = gallery.comboBox();
    combo->showPopup();
    settle();
    auto* popup = combo->view()->window();
    REQUIRE( popup->isVisible() );
    screenshots.save( popup, "gallery-combo-popup" );
    combo->hidePopup();
}

// Calls slot, which runs a modal dialog, and saves the dialog, one image per
// page of its tab widget, before closing it.
void renderDialog( MainWindow& mainWindow, const char* slot, const QString& view,
                   const Screenshots& screenshots )
{
    bool rendered = false;
    QTimer poll;
    poll.setInterval( 50 );
    QObject::connect( &poll, &QTimer::timeout, [ & ] {
        auto* dialog = qobject_cast<QDialog*>( QApplication::activeModalWidget() );
        if ( !dialog ) {
            return;
        }
        poll.stop();
        settle();
        if ( auto* tabs = dialog->findChild<QTabWidget*>() ) {
            for ( int i = 0; i < tabs->count(); ++i ) {
                tabs->setCurrentIndex( i );
                settle( 50 );
                screenshots.save( dialog, view + "-" + slug( tabs->tabText( i ) ) );
            }
        }
        else {
            screenshots.save( dialog, view );
        }
        rendered = true;
        dialog->reject();
    } );
    poll.start();

    INFO( slot );
    REQUIRE( QMetaObject::invokeMethod( &mainWindow, slot ) );
    CHECK( rendered );
}

void renderMainWindow( const QString& logFile, const Screenshots& screenshots )
{
    // What other tests opened would show on the Dashboard.
    auto& recentFiles = RecentFiles::getSynced();
    recentFiles.removeAll();
    recentFiles.save();

    auto session
        = std::make_shared<Session>( testSettingsPolicies(), std::make_shared<LogFormatCatalog>() );
    MainWindow mainWindow( WindowSession{ session, "Main", 0 },
                           std::make_shared<logsquirl::plugins::ApplicationPlugins>() );
    mainWindow.resize( 1400, 850 );
    mainWindow.show();
    settle( 300 );
    screenshots.save( &mainWindow, "main-window-dashboard" );

    mainWindow.loadInitialFile( logFile, false );
    CrawlerWidget* crawler = nullptr;
    REQUIRE( waitUiState(
        [ & ] { return ( crawler = mainWindow.findChild<CrawlerWidget*>() ) != nullptr; } ) );
    auto* pathLine = mainWindow.findChild<PathLine*>();
    REQUIRE( pathLine != nullptr );
    REQUIRE( waitUiState( [ & ] { return pathLine->text().endsWith( NeutralLogFileName ); } ) );
    settle( 500 );
    screenshots.save( &mainWindow, "main-window-log-file" );

    QComboBox* searchLine = nullptr;
    for ( auto* combo : crawler->findChildren<QComboBox*>() ) {
        if ( combo->isEditable() ) {
            searchLine = combo;
        }
    }
    REQUIRE( searchLine != nullptr );
    searchLine->setEditText( "warn|error" );
    searchLine->setFocus();
    QTest::keyClick( searchLine, Qt::Key_Return );
    CHECK( waitUiState(
        [ & ] {
            for ( auto* line : crawler->findChildren<InfoLine*>() ) {
                if ( line->text().endsWith( "found" ) ) {
                    return true;
                }
            }
            return false;
        },
        30000 ) );
    settle( 300 );
    screenshots.save( &mainWindow, "main-window-search" );

    REQUIRE( QMetaObject::invokeMethod( &mainWindow, "showFiltersPanel" ) );
    settle( 300 );
    screenshots.save( &mainWindow, "sidebar-filters-panel" );
    REQUIRE( QMetaObject::invokeMethod( &mainWindow, "showScratchPad" ) );
    settle( 300 );
    screenshots.save( &mainWindow, "sidebar-scratch-pad" );
    REQUIRE( QMetaObject::invokeMethod( &mainWindow, "toggleSidebar" ) );
    settle();

    for ( auto* action : mainWindow.menuBar()->actions() ) {
        if ( auto* menu = action->menu() ) {
            menu->popup( mainWindow.mapToGlobal( QPoint( 10, 30 ) ) );
            settle();
            screenshots.save( menu, "menu-" + slug( action->text() ) );
            menu->hide();
        }
    }

    REQUIRE( QMetaObject::invokeMethod( &mainWindow, "showCommandPalette" ) );
    settle( 300 );
    QWidget* palette = nullptr;
    for ( auto* widget : mainWindow.findChildren<QDialog*>() ) {
        if ( widget->inherits( "CommandPalette" ) && widget->isVisible() ) {
            palette = widget;
        }
    }
    REQUIRE( palette != nullptr );
    screenshots.save( palette, "command-palette" );
    palette->hide();

    renderDialog( mainWindow, "options", "dialog-options", screenshots );
    renderDialog( mainWindow, "editHighlighters", "dialog-highlighters", screenshots );
    renderDialog( mainWindow, "showPluginDialog", "dialog-plugins", screenshots );
    renderDialog( mainWindow, "manageTabGroups", "dialog-tab-groups", screenshots );

    mainWindow.close();
    settle( 300 );
}

} // namespace

SCENARIO( "Every Theme is rendered to comparison screenshots", "[.screenshots]" )
{
    const auto directory = qEnvironmentVariable( "LOGSQUIRL_SCREENSHOT_DIR" );
    if ( directory.isEmpty() ) {
        FAIL( "Set LOGSQUIRL_SCREENSHOT_DIR to the directory the images are written to" );
    }
    REQUIRE( QDir().mkpath( directory ) );

    REQUIRE( QDir().mkpath( NeutralLogDirectory ) );
    const auto logFile = NeutralLogDirectory + "/" + NeutralLogFileName;
    QFile::remove( logFile );
    REQUIRE( QFile::copy( QStringLiteral( LOGSQUIRL_SOURCE_DIR "/test_data/screenshot_demo.txt" ),
                          logFile ) );
    {
        QFile file( logFile );
        REQUIRE( file.open( QIODevice::ReadWrite ) );
        REQUIRE( file.setFileTime( QDateTime( QDate( 2026, 9, 9 ), QTime( 9, 45 ) ),
                                   QFileDevice::FileModificationTime ) );
    }

    // The Color Labels follow the Theme in the application, so they do here
    // too, and the images show the colors a user sees (ADR-0006). Their own
    // colors are put back afterwards: the test executables share one settings
    // file.
    const auto storedColorLabels = HighlighterSetCollection::get().quickHighlighters();
    QObject colorLabelsContext;
    HighlighterSetCollection::followTheme( &colorLabelsContext );

    Screenshots screenshots( directory );
    for ( const auto& theme : RenderedThemes ) {
        INFO( theme.toStdString() );
        Theme::apply( theme );
        screenshots.setTheme( theme );
        settle();

        renderGallery( screenshots );
        renderMainWindow( logFile, screenshots );
    }

    Theme::apply( Theme::defaultTheme() );

    auto& highlighterSets = HighlighterSetCollection::get();
    highlighterSets.setQuickHighlighters( storedColorLabels );
    highlighterSets.save();

    QDir( NeutralLogDirectory ).removeRecursively();
}
