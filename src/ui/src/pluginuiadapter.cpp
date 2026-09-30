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

#include "pluginuiadapter.h"

#include "crawlerwidget.h"
#include "log.h"
#include "regexlabwindow.h"

#include <QCoreApplication>
#include <QHBoxLayout>
#include <QMainWindow>
#include <QMenu>
#include <QMetaObject>
#include <QTabWidget>
#include <QThread>
#include <QToolBar>

#include <algorithm>

using logsquirl::plugins::PluginCallbackFn;
using logsquirl::plugins::PluginLogLineJump;
using logsquirl::plugins::PluginPattern;
using logsquirl::plugins::PluginRegexLabAnswer;
using logsquirl::plugins::PluginWidgetHandle;

namespace {

// The only place a handle a plugin passed through the C ABI becomes a widget.
QWidget* widgetFrom( PluginWidgetHandle handle )
{
    return static_cast<QWidget*>( handle.widget );
}

// The submenus of the Plugins menu a menu path names, outermost first. Empty
// segments do not count, and a first segment "Plugins" names the Plugins menu
// itself: "", "Plugins" and "/" all name the Plugins menu.
QStringList submenuNamesOf( const QString& menuPath )
{
    QStringList names;
    for ( const auto& segment : menuPath.split( u'/' ) ) {
        const auto name = segment.trimmed();
        if ( !name.isEmpty() ) {
            names.append( name );
        }
    }
    if ( !names.isEmpty() && names.front() == QStringLiteral( "Plugins" ) ) {
        names.removeFirst();
    }
    return names;
}

} // namespace

PluginUiAdapter::PluginUiAdapter( QMainWindow& window, QMenu& pluginsMenu, QAction* menuSeparator,
                                  QTabWidget& sidebarTabs )
    : window_( window )
    , pluginsMenu_( pluginsMenu )
    , menuSeparator_( menuSeparator )
    , sidebarTabs_( sidebarTabs )
{
}

void PluginUiAdapter::onWindowThread( const QString& pluginId, std::function<void()> work )
{
    if ( QThread::currentThread() == window_.thread() ) {
        work();
        return;
    }
    // Plugins may call the host from threads of their own; widgets may only
    // be touched on the thread they live on. The plugin may be unloaded, and
    // its contributions removed, before the queued work runs: then it must
    // not show anything of the unloaded plugin.
    QMetaObject::invokeMethod(
        &window_,
        [ this, pluginId, generation = generationOf( pluginId ), work = std::move( work ) ] {
            if ( generationOf( pluginId ) != generation ) {
                LOG_INFO << "Plugin " << pluginId << " was unloaded; dropping its queued call";
                return;
            }
            work();
        },
        Qt::QueuedConnection );
}

std::uint64_t PluginUiAdapter::generationOf( const QString& pluginId ) const
{
    const std::scoped_lock lock( generationsMutex_ );
    const auto it = generations_.find( pluginId );
    return it == generations_.end() ? 0 : it->second;
}

void PluginUiAdapter::placeInToolBar( PluginToolBar& bar, const QString& pluginId, QWidget* widget )
{
    if ( !bar.toolBar ) {
        bar.toolBar
            = new QToolBar( QCoreApplication::translate( "MainWindow", bar.title ), &window_ );
        bar.toolBar->setMovable( false );
        bar.toolBar->setFloatable( false );
        window_.addToolBar( bar.area, bar.toolBar );
    }
    const auto alreadyPlaced = std::ranges::any_of(
        bar.placed, [ widget ]( const PlacedWidget& entry ) { return entry.widget == widget; } );
    if ( alreadyPlaced ) {
        return;
    }

    // The action QToolBar::addWidget() creates owns the widget it shows and
    // deletes it with the toolbar, even after removeAction(). So the toolbar
    // shows a container of the host's: the plugin's widget stays the plugin's,
    // and the action and container go when the widget is removed.
    auto* container = new QWidget;
    auto* layout = new QHBoxLayout( container );
    layout->setContentsMargins( 0, 0, 0, 0 );
    layout->addWidget( widget );
    bar.placed.push_back( { pluginId, widget, bar.toolBar->addWidget( container ) } );
}

void PluginUiAdapter::removeFromToolBar( PluginToolBar& bar, const QString& pluginId,
                                         const QWidget* widget )
{
    std::erase_if( bar.placed, [ & ]( const PlacedWidget& entry ) {
        if ( entry.pluginId != pluginId || ( widget && entry.widget != widget ) ) {
            return false;
        }
        // Take the plugin's widget out of the container first: deleting the
        // action deletes the container, and must not reach the widget.
        if ( entry.widget ) {
            entry.widget->setParent( nullptr );
        }
        if ( entry.toolBarAction ) {
            if ( bar.toolBar ) {
                bar.toolBar->removeAction( entry.toolBarAction );
            }
            delete entry.toolBarAction.data();
        }
        return true;
    } );
}

void PluginUiAdapter::removeFromSidebar( const QString& pluginId, const QWidget* widget )
{
    std::erase_if( sidebarWidgets_, [ & ]( const PlacedWidget& entry ) {
        if ( entry.pluginId != pluginId || ( widget && entry.widget != widget ) ) {
            return false;
        }
        if ( entry.widget ) {
            const int index = sidebarTabs_.indexOf( entry.widget );
            if ( index >= 0 ) {
                sidebarTabs_.removeTab( index );
            }
            entry.widget->setParent( nullptr );
        }
        return true;
    } );
}

void PluginUiAdapter::addStatusWidget( const QString& pluginId, PluginWidgetHandle handle )
{
    onWindowThread( pluginId, [ this, pluginId, handle ] {
        auto* widget = widgetFrom( handle );
        if ( !widget ) {
            return;
        }
        placeInToolBar( statusToolBar_, pluginId, widget );
        LOG_INFO << "Plugin " << pluginId << " registered status widget";
    } );
}

void PluginUiAdapter::removeStatusWidget( const QString& pluginId, PluginWidgetHandle handle )
{
    onWindowThread( pluginId, [ this, pluginId, handle ] {
        auto* widget = widgetFrom( handle );
        if ( !widget ) {
            return;
        }
        removeFromToolBar( statusToolBar_, pluginId, widget );
        LOG_INFO << "Plugin " << pluginId << " unregistered status widget";
    } );
}

void PluginUiAdapter::addFooterWidget( const QString& pluginId, PluginWidgetHandle handle )
{
    onWindowThread( pluginId, [ this, pluginId, handle ] {
        auto* widget = widgetFrom( handle );
        if ( !widget ) {
            return;
        }
        placeInToolBar( footerToolBar_, pluginId, widget );
        LOG_INFO << "Plugin " << pluginId << " registered footer widget";
    } );
}

void PluginUiAdapter::removeFooterWidget( const QString& pluginId, PluginWidgetHandle handle )
{
    onWindowThread( pluginId, [ this, pluginId, handle ] {
        auto* widget = widgetFrom( handle );
        if ( !widget ) {
            return;
        }
        removeFromToolBar( footerToolBar_, pluginId, widget );
        LOG_INFO << "Plugin " << pluginId << " unregistered footer widget";
    } );
}

void PluginUiAdapter::addSidebarTab( const QString& pluginId, const QString& label,
                                     PluginWidgetHandle handle )
{
    onWindowThread( pluginId, [ this, pluginId, label, handle ] {
        auto* widget = widgetFrom( handle );
        if ( !widget ) {
            return;
        }

        // Avoid adding the same widget twice.
        if ( sidebarTabs_.indexOf( widget ) >= 0 ) {
            return;
        }

        sidebarTabs_.addTab( widget, label );
        sidebarWidgets_.push_back( { pluginId, widget, nullptr } );
        LOG_INFO << "Plugin " << pluginId << " registered sidebar tab: " << label;
    } );
}

void PluginUiAdapter::removeSidebarTab( const QString& pluginId, PluginWidgetHandle handle )
{
    onWindowThread( pluginId, [ this, pluginId, handle ] {
        auto* widget = widgetFrom( handle );
        if ( !widget ) {
            return;
        }
        removeFromSidebar( pluginId, widget );
        LOG_INFO << "Plugin " << pluginId << " removed sidebar tab";
    } );
}

QMenu& PluginUiAdapter::menuAt( const QStringList& submenuNames )
{
    QMenu* menu = &pluginsMenu_;
    QString path;
    for ( const auto& name : submenuNames ) {
        path = path.isEmpty() ? name : path + u'/' + name;
        auto& submenu = submenus_[ path ];
        if ( !submenu ) {
            submenu = new QMenu( name, menu );
            if ( menu == &pluginsMenu_ ) {
                // Like plugin actions, above the separator.
                pluginsMenu_.insertMenu( menuSeparator_, submenu );
            }
            else {
                menu->addMenu( submenu );
            }
        }
        menu = submenu;
    }
    return *menu;
}

void PluginUiAdapter::removeEmptySubmenus()
{
    // Innermost first: a submenu whose only entry is an empty submenu is
    // empty once that one is gone.
    std::vector<std::map<QString, QPointer<QMenu>>::iterator> byDepth;
    for ( auto it = submenus_.begin(); it != submenus_.end(); ++it ) {
        byDepth.push_back( it );
    }
    std::ranges::sort( byDepth, std::ranges::greater{},
                       []( const auto& it ) { return it->first.count( u'/' ); } );
    for ( const auto& it : byDepth ) {
        if ( it->second && !it->second->actions().isEmpty() ) {
            continue;
        }
        // Deleting the menu also deletes the action that shows it in its parent.
        delete it->second.data();
        submenus_.erase( it );
    }
}

void PluginUiAdapter::addMenuAction( const QString& pluginId, const QString& menuPath,
                                     const QString& label, PluginCallbackFn callback,
                                     void* userData )
{
    onWindowThread( pluginId, [ this, pluginId, menuPath, label, callback, userData ] {
        auto& menu = menuAt( submenuNamesOf( menuPath ) );
        auto& actions = menuActions_[ pluginId ];

        // Prevent duplicate entries when a plugin is re-enabled without restart.
        const auto duplicate
            = std::ranges::any_of( actions, [ &label, &menu ]( const PluginMenuAction& existing ) {
                  return existing.action && existing.menu == &menu
                         && existing.action->text() == label;
              } );
        if ( duplicate ) {
            return;
        }

        auto* action = new QAction( label, &window_ );
        action->setStatusTip(
            QCoreApplication::translate( "MainWindow", "Plugin action from %1" ).arg( pluginId ) );
        QObject::connect( action, &QAction::triggered, action, [ callback, userData ]() {
            if ( callback ) {
                callback( userData );
            }
        } );

        if ( &menu == &pluginsMenu_ ) {
            // Insert above the separator so plugin actions appear at the top,
            // with Manage/Browse sitting below the divider line.
            pluginsMenu_.insertAction( menuSeparator_, action );
        }
        else {
            menu.addAction( action );
        }
        actions.push_back( { action, &menu } );
    } );
}

void PluginUiAdapter::removeContributions( const QString& pluginId )
{
    // Right away, not on the window's thread: calls of the plugin still
    // queued there are dropped from now on.
    {
        const std::scoped_lock lock( generationsMutex_ );
        ++generations_[ pluginId ];
    }

    onWindowThread( pluginId, [ this, pluginId ] {
        if ( const auto it = menuActions_.find( pluginId ); it != menuActions_.end() ) {
            for ( const auto& [ action, menu ] : it->second ) {
                if ( action ) {
                    if ( menu ) {
                        menu->removeAction( action );
                    }
                    delete action.data();
                }
            }
            menuActions_.erase( it );
            // Submenus shared with another plugin's actions stay.
            removeEmptySubmenus();
        }

        // Normally a plugin removes its widgets itself when it is shut down;
        // anything it left behind must not outlive its library in the window.
        removeFromToolBar( statusToolBar_, pluginId, nullptr );
        removeFromToolBar( footerToolBar_, pluginId, nullptr );
        removeFromSidebar( pluginId, nullptr );

        // A Lab the plugin opened answers as it closes; once the plugin is
        // unloaded, its answer context is gone and nothing reaches it.
        if ( const auto it = regexLabs_.find( pluginId ); it != regexLabs_.end() ) {
            const auto labs = std::move( it->second );
            regexLabs_.erase( it );
            for ( const auto& lab : labs ) {
                if ( lab ) {
                    lab->close();
                }
            }
        }
    } );
}

PluginWidgetHandle PluginUiAdapter::configurationParent()
{
    return PluginWidgetHandle{ static_cast<void*>( static_cast<QWidget*>( &window_ ) ) };
}

void PluginUiAdapter::setRegexLabSampleSource( std::function<RegexLabSampleSource()> sampleSource )
{
    regexLabSampleSource_ = std::move( sampleSource );
}

bool PluginUiAdapter::openRegexLab( const QString& pluginId, const PluginPattern& pattern,
                                    QObject* context, PluginRegexLabAnswer answer )
{
    if ( QThread::currentThread() != window_.thread() || context == nullptr || !answer ) {
        return false;
    }

    // Plugins match their patterns themselves, with Qt's QRegularExpression
    // as a rule: the Lab matches as it does, whatever engine the Searches
    // run on. A window of its own over the main window, which it goes with.
    auto* lab = new RegexLabWindow( RegexpEngine::QRegularExpression, &window_ );
    lab->setAttribute( Qt::WA_DeleteOnClose );
    lab->setPattern(
        RegularExpressionPattern( pattern.pattern, pattern.matchesCase, false, false, false ) );
    // Always a regular expression; the plugin keeps only whether it matches case.
    lab->setOptionsKept( RegexLabWindow::Option::MatchCase, RegexLabWindow::Option::UseRegexp );
    lab->offerApply( true );
    if ( regexLabSampleSource_ ) {
        lab->setSampleSource( regexLabSampleSource_() );
    }

    // Connected with the plugin's answer context, never with this adapter:
    // along with the window, the Lab answers after the adapter is gone.
    QObject::connect( lab, &RegexLabWindow::applied, context,
                      [ answer ]( const RegularExpressionPattern& applied ) {
                          answer( PluginPattern{ .pattern = applied.pattern,
                                                 .matchesCase = applied.isCaseSensitive } );
                      } );
    QObject::connect( lab, &RegexLabWindow::cancelled, context,
                      [ answer ]() { answer( std::nullopt ); } );

    auto& labs = regexLabs_[ pluginId ];
    std::erase_if( labs, []( const QPointer<RegexLabWindow>& open ) { return open.isNull(); } );
    labs.emplace_back( lab );

    LOG_INFO << "Plugin " << pluginId << " opened the Regex Lab";
    lab->show();
    lab->raise();
    lab->activateWindow();
    return true;
}

void PluginUiAdapter::setTabInFront( std::function<CrawlerWidget*()> tabInFront )
{
    tabInFront_ = std::move( tabInFront );
}

PluginLogLineJump PluginUiAdapter::goToLogLine( std::uint64_t logLine )
{
    if ( QThread::currentThread() != window_.thread() || !tabInFront_ ) {
        return PluginLogLineJump::NoLogFile;
    }
    auto* tab = tabInFront_();
    if ( tab == nullptr ) {
        return PluginLogLineJump::NoLogFile;
    }
    return tab->goToLogLine( LineNumber( logLine ) ) ? PluginLogLineJump::Shown
                                                     : PluginLogLineJump::OutOfRange;
}

std::optional<QStringList> PluginUiAdapter::selectedLogLines( std::size_t maxLines )
{
    if ( QThread::currentThread() != window_.thread() || !tabInFront_ ) {
        return std::nullopt;
    }
    auto* tab = tabInFront_();
    if ( tab == nullptr ) {
        return std::nullopt;
    }
    // A selection within a Log Line is that Log Line's, as for the Regex Lab.
    const auto logLines = tab->selectedLogLines( LinesCount( maxLines ) );
    const auto texts = tab->logLineTextReader()( logLines );

    QStringList selected;
    selected.reserve( static_cast<qsizetype>( texts.size() ) );
    for ( const auto& text : texts ) {
        selected.append( text );
    }
    return selected;
}
