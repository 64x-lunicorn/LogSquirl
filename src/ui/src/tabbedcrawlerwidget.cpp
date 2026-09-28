/*
 * Copyright (C) 2014, 2015 Nicolas Bonnefon and other contributors
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

#include "tabbedcrawlerwidget.h"

#include <algorithm>
#include <iterator>

#include <QApplication>
#include <QClipboard>
#include <QColorDialog>
#include <QDir>
#include <QFileInfo>
#include <QInputDialog>
#include <QKeyEvent>
#include <QMenu>
#include <QPixmap>
#include <qobjectdefs.h>
#include <qpoint.h>

#include "crawlerwidget.h"

#include "clipboard.h"
#include "iconloader.h"
#include "log.h"
#include "openfilehelper.h"
#include "tabbarstyle.h"
#include "tabgroupinfo.h"
#include "tabnamemapping.h"
#include "theme.h"

namespace {
constexpr QLatin1String PathKey = QLatin1String( "path", 4 );
constexpr QLatin1String StatusKey = QLatin1String( "status", 6 );
constexpr QLatin1String TransientKey = QLatin1String( "transient", 9 );

// Creates a small solid-colour icon for use in group context menus.
QIcon createColorIcon( const QColor& color, int size = 12 )
{
    QPixmap pixmap( size, size );
    pixmap.fill( color );
    return QIcon( pixmap );
}
} // namespace

TabbedCrawlerWidget::TabbedCrawlerWidget()
    : QTabWidget()
    , newdata_icon_( ":/images/newdata_icon.png" )
    , newfiltered_icon_( ":/images/newfiltered_icon.png" )
{
    applyTheme();

    setTabBar( &myTabBar_ );
    myTabBar_.hide();

    myTabBar_.setContextMenuPolicy( Qt::CustomContextMenu );
    connect( &myTabBar_, &CrawlerTabBar::showTabContextMenu, this,
             &TabbedCrawlerWidget::showContextMenu );

    loadIcons();
    Theme::whenApplied( this, [ this ] {
        applyTheme();
        loadIcons();
    } );
}

void TabbedCrawlerWidget::applyTheme()
{
    myTabBar_.setStyleSheet( closableTabBarStyleSheet( Theme::active() ) );
}

void TabbedCrawlerWidget::loadIcons()
{
    IconLoader iconLoader;
    olddata_icon_ = iconLoader.load( "olddata_icon" );
    for ( int tab = 0; tab < count(); ++tab ) {
        updateIcon( tab );
    }
}

void TabbedCrawlerWidget::setOpeningTitle( const QString& path, const QString& title,
                                           const QString& toolTip )
{
    if ( title.isEmpty() ) {
        openingTitles_.remove( path );
    }
    else {
        openingTitles_.insert( path, { title, toolTip } );
    }
}

void TabbedCrawlerWidget::addTabBarItem( int index, const QString& fileName,
                                         LogFileLifetime lifetime )
{
    QVariantMap tabData;
    tabData[ PathKey ] = fileName;
    tabData[ StatusKey ] = static_cast<int>( DataStatus::OLD_DATA );
    tabData[ TransientKey ] = lifetime == LogFileLifetime::Transient;

    myTabBar_.setTabData( index, tabData );

    myTabBar_.setTabIcon( index, olddata_icon_ );
    const auto toolTip = openingTitles_.value( fileName ).toolTip;
    myTabBar_.setTabToolTip( index,
                             toolTip.isEmpty() ? QDir::toNativeSeparators( fileName ) : toolTip );

    // Names the tab too.
    updateTabGroupAppearance( index );

    setCurrentIndex( index );

    if ( count() > 1 )
        myTabBar_.show();
}

QString TabbedCrawlerWidget::baseTabName( int index ) const
{
    const auto path = tabPathAt( index );
    const auto customName = holdsTransientLogFile( index ) ? renamedTransientTabs_.value( path )
                                                           : TabNameMapping::get().tabName( path );
    if ( !customName.isEmpty() ) {
        return customName;
    }
    const auto openingTitle = openingTitles_.value( path ).title;
    return openingTitle.isEmpty() ? QFileInfo( path ).fileName() : openingTitle;
}

void TabbedCrawlerWidget::updateTabGroupAppearance( int index )
{
    const auto group = groupOfTab( index );
    const auto name = baseTabName( index );

    if ( group.has_value() ) {
        myTabBar_.setTabText( index, QString::fromUtf8( "\u25CF " ) + name );
        myTabBar_.setTabTextColor( index, group->color );
    }
    else {
        myTabBar_.setTabText( index, name );
        myTabBar_.setTabTextColor( index, QColor{} );
    }
}

void TabbedCrawlerWidget::refreshAllTabGroupAppearances()
{
    for ( int i = 0; i < count(); ++i ) {
        updateTabGroupAppearance( i );
    }
}

bool TabbedCrawlerWidget::holdsTransientLogFile( int index ) const
{
    return myTabBar_.tabData( index ).toMap()[ TransientKey ].toBool();
}

void TabbedCrawlerWidget::renameTab( int index, const QString& name )
{
    const auto path = tabPathAt( index );
    // A Transient Log File's path is gone after a restart: stored, its name
    // would never name a tab again.
    if ( holdsTransientLogFile( index ) ) {
        if ( name.isEmpty() ) {
            renamedTransientTabs_.remove( path );
        }
        else {
            renamedTransientTabs_.insert( path, name );
        }
    }
    else {
        TabNameMapping::getSynced().setTabName( path, name ).save();
    }
    updateTabGroupAppearance( index );
}

void TabbedCrawlerWidget::addTabToGroup( int index, const QString& groupId )
{
    const auto path = tabPathAt( index );
    if ( holdsTransientLogFile( index ) ) {
        transientTabGroups_.insert( path, groupId );
    }
    else {
        TabGroupInfo::getSynced().addTabToGroup( groupId, path ).save();
    }
    refreshAllTabGroupAppearances();
}

void TabbedCrawlerWidget::removeTabFromGroup( int index )
{
    const auto path = tabPathAt( index );
    if ( holdsTransientLogFile( index ) ) {
        transientTabGroups_.remove( path );
    }
    else {
        TabGroupInfo::getSynced().removeTabFromGroup( path ).save();
    }
    refreshAllTabGroupAppearances();
}

std::optional<TabGroupInfo::TabGroup> TabbedCrawlerWidget::groupOfTab( int index ) const
{
    const auto path = tabPathAt( index );
    if ( !holdsTransientLogFile( index ) ) {
        return TabGroupInfo::get().groupForTab( path );
    }

    // The group itself is stored, and may have been deleted since.
    const auto groupId = transientTabGroups_.value( path );
    const auto& groups = TabGroupInfo::get().groups();
    const auto group = std::ranges::find( groups, groupId, &TabGroupInfo::TabGroup::id );
    if ( groupId.isEmpty() || group == groups.end() ) {
        return std::nullopt;
    }
    return *group;
}

void TabbedCrawlerWidget::removeCrawler( int index )
{
    // A Transient Log File goes with its tab, and what the user made of the
    // tab with it. So does the title the tab opened with.
    const auto path = tabPathAt( index );
    if ( holdsTransientLogFile( index ) ) {
        renamedTransientTabs_.remove( path );
        transientTabGroups_.remove( path );
    }
    openingTitles_.remove( path );

    QTabWidget::removeTab( index );

    // Keep the tab bar visible while a tab that holds no Log File remains:
    // the dashboard
    if ( logFileTabs().size() < count() ) {
        myTabBar_.show();
    }
}

bool TabbedCrawlerWidget::holdsLogFile( int index ) const
{
    return qobject_cast<const CrawlerWidget*>( widget( index ) ) != nullptr;
}

QList<int> TabbedCrawlerWidget::logFileTabs() const
{
    QList<int> tabs;
    for ( int i = 0; i < count(); ++i ) {
        if ( holdsLogFile( i ) ) {
            tabs.append( i );
        }
    }
    return tabs;
}

int TabbedCrawlerWidget::tabOfPath( const QString& path ) const
{
    for ( int i = 0; i < count(); ++i ) {
        if ( tabPathAt( i ) == path ) {
            return i;
        }
    }
    return -1;
}

void TabbedCrawlerWidget::mouseReleaseEvent( QMouseEvent* event )
{
    LOG_DEBUG << "TabbedCrawlerWidget::mouseReleaseEvent";

    if ( event->button() == Qt::MiddleButton ) {
        const int tab = myTabBar_.tabAt( myTabBar_.mapFrom( this, event->position().toPoint() ) );
        if ( holdsLogFile( tab ) ) {
            Q_EMIT tabCloseRequested( tab );
            event->accept();
        }
    }

    event->ignore();
}

QString TabbedCrawlerWidget::tabPathAt( int index ) const
{
    return myTabBar_.tabData( index ).toMap()[ PathKey ].toString();
}

void CrawlerTabBar::mouseReleaseEvent( QMouseEvent* mouseEvent )
{
    if ( mouseEvent->button() == Qt::RightButton ) {
        int tab = tabAt( mouseEvent->pos() );
        if ( tab != -1 ) {
            Q_EMIT showTabContextMenu( tab, mapToGlobal( mouseEvent->pos() ) );
            mouseEvent->accept();
        }
    }

    mouseEvent->ignore();
}

void TabbedCrawlerWidget::showContextMenu( int tab, QPoint globalPoint )
{
    // No context menu for a tab that holds no Log File: the dashboard
    if ( !holdsLogFile( tab ) ) {
        return;
    }

    const auto logFiles = logFileTabs();
    const auto logFilesWhere = [ &logFiles ]( auto&& predicate ) {
        QList<int> tabs;
        std::ranges::copy_if( logFiles, std::back_inserter( tabs ), predicate );
        return tabs;
    };
    const auto leftOfTab = logFilesWhere( [ tab ]( int i ) { return i < tab; } );
    const auto rightOfTab = logFilesWhere( [ tab ]( int i ) { return i > tab; } );

    QMenu menu( this );
    auto closeThis = menu.addAction( tr( "Close this" ) );
    auto closeOthers = menu.addAction( tr( "Close others" ) );
    auto closeLeft = menu.addAction( tr( "Close to the left" ) );
    auto closeRight = menu.addAction( tr( "Close to the right" ) );
    auto closeAll = menu.addAction( tr( "Close all" ) );
    menu.addSeparator();
    auto copyFullPath = menu.addAction( tr( "Copy full path" ) );
    auto openContainingFolder = menu.addAction( tr( "Open containing folder" ) );
    menu.addSeparator();
    auto renameTabAction = menu.addAction( tr( "Rename tab" ) );
    auto resetTabName = menu.addAction( tr( "Reset tab name" ) );

    connect( closeThis, &QAction::triggered, [ tab, this ] { Q_EMIT tabCloseRequested( tab ); } );

    connect( closeOthers, &QAction::triggered, [ this, others = leftOfTab + rightOfTab ] {
        Q_EMIT bulkTabCloseRequested( others );
    } );

    connect( closeLeft, &QAction::triggered,
             [ this, leftOfTab ] { Q_EMIT bulkTabCloseRequested( leftOfTab ); } );

    connect( closeRight, &QAction::triggered,
             [ this, rightOfTab ] { Q_EMIT bulkTabCloseRequested( rightOfTab ); } );

    connect( closeAll, &QAction::triggered,
             [ this, logFiles ] { Q_EMIT bulkTabCloseRequested( logFiles ); } );

    closeLeft->setDisabled( leftOfTab.isEmpty() );
    closeRight->setDisabled( rightOfTab.isEmpty() );

    connect( copyFullPath, &QAction::triggered, this, [ this, tab ] {
        sendTextToClipboard( QDir::toNativeSeparators( tabPathAt( tab ) ) );
    } );

    connect( openContainingFolder, &QAction::triggered, this,
             [ this, tab ] { showPathInFileExplorer( tabPathAt( tab ) ); } );

    connect( renameTabAction, &QAction::triggered, this, [ this, tab ] {
        bool isNameEntered = false;
        auto newName = QInputDialog::getText( this, "Rename tab", "Tab name", QLineEdit::Normal,
                                              myTabBar_.tabText( tab ), &isNameEntered );
        if ( isNameEntered ) {
            renameTab( tab, newName );
        }
    } );

    connect( resetTabName, &QAction::triggered, this, [ this, tab ] { renameTab( tab, {} ); } );

    // --- Tab group operations ---
    // Read again, to offer what another instance saved in the meantime.
    TabGroupInfo::getSynced();
    const auto currentGroup = groupOfTab( tab );

    menu.addSeparator();

    // "Add to Group" submenu
    auto* addToGroupMenu = menu.addMenu( tr( "Add to Group" ) );
    const auto& allGroups = TabGroupInfo::getSynced().groups();
    for ( const auto& group : allGroups ) {
        // Skip the group the tab already belongs to
        if ( currentGroup.has_value() && currentGroup->id == group.id ) {
            continue;
        }
        auto* action = addToGroupMenu->addAction( group.name );
        action->setIcon( createColorIcon( group.color ) );
        connect( action, &QAction::triggered, this,
                 [ this, groupId = group.id, tab ] { addTabToGroup( tab, groupId ); } );
    }
    if ( !allGroups.empty() ) {
        addToGroupMenu->addSeparator();
    }
    auto* newGroupAction = addToGroupMenu->addAction( tr( "New Group..." ) );
    connect( newGroupAction, &QAction::triggered, this, [ this, tab ] {
        bool ok = false;
        const auto name = QInputDialog::getText( this, tr( "New Tab Group" ), tr( "Group name:" ),
                                                 QLineEdit::Normal, QString{}, &ok );
        if ( !ok || name.isEmpty() ) {
            return;
        }
        const auto color = QColorDialog::getColor( Qt::blue, this, tr( "Group Color" ) );
        if ( !color.isValid() ) {
            return;
        }
        auto& groupInfo = TabGroupInfo::getSynced();
        const auto groupId = groupInfo.addGroup( name, color );
        groupInfo.save();
        addTabToGroup( tab, groupId );
    } );

    // "Remove from Group" (enabled only if tab is in a group)
    auto* removeFromGroup = menu.addAction( tr( "Remove from Group" ) );
    removeFromGroup->setEnabled( currentGroup.has_value() );
    connect( removeFromGroup, &QAction::triggered, this,
             [ this, tab ] { removeTabFromGroup( tab ); } );

    // Group management submenu (visible only if tab is in a group)
    if ( currentGroup.has_value() ) {
        auto* groupMenu = menu.addMenu( tr( "Group: %1" ).arg( currentGroup->name ) );
        const auto groupId = currentGroup->id;

        auto* renameGroupAction = groupMenu->addAction( tr( "Rename Group..." ) );
        connect( renameGroupAction, &QAction::triggered, this, [ this, groupId, currentGroup ] {
            bool ok = false;
            const auto newName
                = QInputDialog::getText( this, tr( "Rename Group" ), tr( "Group name:" ),
                                         QLineEdit::Normal, currentGroup->name, &ok );
            if ( ok && !newName.isEmpty() ) {
                TabGroupInfo::getSynced().renameGroup( groupId, newName ).save();
                refreshAllTabGroupAppearances();
            }
        } );

        auto* changeColorAction = groupMenu->addAction( tr( "Change Group Color..." ) );
        connect( changeColorAction, &QAction::triggered, this, [ this, groupId, currentGroup ] {
            const auto color
                = QColorDialog::getColor( currentGroup->color, this, tr( "Group Color" ) );
            if ( color.isValid() ) {
                TabGroupInfo::getSynced().setGroupColor( groupId, color ).save();
                refreshAllTabGroupAppearances();
            }
        } );

        groupMenu->addSeparator();

        auto* closeAllInGroup = groupMenu->addAction( tr( "Close All in Group" ) );
        connect( closeAllInGroup, &QAction::triggered, this, [ this, groupId ] {
            TabGroupInfo::getSynced();
            // Collect tab indices first, then close in reverse order. The
            // tabs of Transient Log Files in the group are among them.
            const auto logFileTabsNow = logFileTabs();
            for ( auto i = logFileTabsNow.crbegin(); i != logFileTabsNow.crend(); ++i ) {
                const auto group = groupOfTab( *i );
                if ( group.has_value() && group->id == groupId ) {
                    Q_EMIT tabCloseRequested( *i );
                }
            }
        } );

        auto* ungroupAll = groupMenu->addAction( tr( "Ungroup All" ) );
        connect( ungroupAll, &QAction::triggered, this, [ this, groupId ] {
            TabGroupInfo::getSynced().removeGroup( groupId ).save();
            refreshAllTabGroupAppearances();
        } );
    }

    // --- Merge operations ---
    if ( logFiles.size() > 1 ) {
        menu.addSeparator();

        const auto pathsOf = [ this ]( const QList<int>& tabs ) {
            QStringList paths;
            for ( const auto i : tabs ) {
                paths.append( tabPathAt( i ) );
            }
            return paths;
        };

        if ( !leftOfTab.isEmpty() ) {
            auto* mergeLeft = menu.addAction( tr( "Merge All Left" ) );
            connect( mergeLeft, &QAction::triggered, this, [ this, paths = pathsOf( leftOfTab ) ] {
                Q_EMIT mergeRequested( paths, false );
            } );

            auto* mergeLeftDedup = menu.addAction( tr( "Merge All Left (dedup)" ) );
            connect(
                mergeLeftDedup, &QAction::triggered, this,
                [ this, paths = pathsOf( leftOfTab ) ] { Q_EMIT mergeRequested( paths, true ); } );
        }

        if ( !rightOfTab.isEmpty() ) {
            auto* mergeRight = menu.addAction( tr( "Merge All Right" ) );
            connect( mergeRight, &QAction::triggered, this,
                     [ this, paths = pathsOf( rightOfTab ) ] {
                         Q_EMIT mergeRequested( paths, false );
                     } );

            auto* mergeRightDedup = menu.addAction( tr( "Merge All Right (dedup)" ) );
            connect(
                mergeRightDedup, &QAction::triggered, this,
                [ this, paths = pathsOf( rightOfTab ) ] { Q_EMIT mergeRequested( paths, true ); } );
        }
    }

    menu.exec( globalPoint );
}

void TabbedCrawlerWidget::keyPressEvent( QKeyEvent* event )
{
    const auto mod = event->modifiers();
    const auto key = event->key();

    LOG_DEBUG << "TabbedCrawlerWidget::keyPressEvent";

    // Ctrl + tab
    if ( ( mod == Qt::ControlModifier && key == Qt::Key_Tab )
         || ( mod == Qt::ControlModifier && key == Qt::Key_PageDown )
         || ( mod == ( Qt::ControlModifier | Qt::AltModifier | Qt::KeypadModifier )
              && key == Qt::Key_Right ) ) {
        setCurrentIndex( ( currentIndex() + 1 ) % count() );
    }
    // Ctrl + shift + tab
    else if ( ( mod == ( Qt::ControlModifier | Qt::ShiftModifier ) && key == Qt::Key_Tab )
              || ( mod == Qt::ControlModifier && key == Qt::Key_PageUp )
              || ( mod == ( Qt::ControlModifier | Qt::AltModifier | Qt::KeypadModifier )
                   && key == Qt::Key_Left ) ) {
        setCurrentIndex( ( currentIndex() - 1 >= 0 ) ? currentIndex() - 1 : count() - 1 );
    }
    // Ctrl + numbers
    else if ( mod == Qt::ControlModifier && ( key >= Qt::Key_1 && key <= Qt::Key_8 ) ) {
        int newIndex = key - Qt::Key_0;
        if ( newIndex <= count() )
            setCurrentIndex( newIndex - 1 );
    }
    // Ctrl + 9
    else if ( mod == Qt::ControlModifier && key == Qt::Key_9 ) {
        setCurrentIndex( count() - 1 );
    }
    else if ( mod == Qt::ControlModifier && ( key == Qt::Key_Q || key == Qt::Key_W ) ) {
        Q_EMIT tabCloseRequested( currentIndex() );
    }
    else {
        QTabWidget::keyPressEvent( event );
    }
}

void TabbedCrawlerWidget::updateIcon( int index )
{
    auto tabData = myTabBar_.tabData( index ).toMap();

    const QIcon* icon;
    switch ( static_cast<DataStatus>( tabData[ StatusKey ].toInt() ) ) {
    case DataStatus::OLD_DATA:
        icon = &olddata_icon_;
        break;
    case DataStatus::NEW_DATA:
        icon = &newdata_icon_;
        break;
    case DataStatus::NEW_FILTERED_DATA:
        icon = &newfiltered_icon_;
        break;
    default:
        return;
    }

    myTabBar_.setTabIcon( index, *icon );
}

void TabbedCrawlerWidget::setTabDataStatus( int index, DataStatus status )
{
    LOG_DEBUG << "TabbedCrawlerWidget::setTabDataStatus " << index;

    auto tabData = myTabBar_.tabData( index ).toMap();
    tabData[ StatusKey ] = static_cast<int>( status );
    myTabBar_.setTabData( index, tabData );

    updateIcon( index );
}
