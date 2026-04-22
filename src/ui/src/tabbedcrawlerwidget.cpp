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
#include "configuration.h"
#include "dispatch_to.h"
#include "iconloader.h"
#include "log.h"
#include "openfilehelper.h"
#include "styles.h"
#include "tabgroupinfo.h"
#include "tabnamemapping.h"

namespace {
constexpr QLatin1String PathKey = QLatin1String( "path", 4 );
constexpr QLatin1String StatusKey = QLatin1String( "status", 6 );

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

    const auto& config = Configuration::get();
    const bool isDark = ( config.style() == StyleManager::DarkStyleKey );

    QString tabStyle = QStringLiteral( "QTabBar::tab { height: 28px; }" );

    QString tabCloseButtonStyle = " QTabBar::close-button {\
              height: 14px; width: 14px;\
              subcontrol-origin: padding;\
              subcontrol-position: right;\
              %1}";

    QString backgroundImage;
    QString backgroundHoverImage;

    if ( isDark ) {
        backgroundImage = ":/images/icons8-close-window-16_inverse.png";
        backgroundHoverImage = ":/images/icons8-close-window-hover-16_inverse.png";
    }

#if defined( Q_OS_MAC )
    // work around Qt macOS bug missing tab close icons
    // see: https://bugreports.qt.io/browse/QTBUG-61092
    if ( !isDark ) {
        backgroundImage
            = ":/qt-project.org/styles/commonstyle/images/standardbutton-closetab-16.png";
        backgroundHoverImage
            = ":/qt-project.org/styles/commonstyle/images/standardbutton-closetab-hover-16.png";
    }
#elif defined( Q_OS_WIN )
    if ( config.style() == StyleManager::FusionKey ) {
        backgroundImage = ":/images/icons8-close-window-16.png";
        backgroundHoverImage = ":/images/icons8-close-window-hover-16.png";
    }
#endif

    if ( !backgroundImage.isEmpty() ) {
        const QString backgroundImageTemplate = " image: url(%1);";
        QString tabCloseButtonHoverStyle
            = isDark
                  ? " QTabBar::close-button:hover { %1 background-color: #C42B1C;"
                    " border-radius: 3px; }"
                  : " QTabBar::close-button:hover { %1 }";
        backgroundImage = backgroundImageTemplate.arg( backgroundImage );
        backgroundHoverImage = backgroundImageTemplate.arg( backgroundHoverImage );
        tabCloseButtonHoverStyle = tabCloseButtonHoverStyle.arg( backgroundHoverImage );
        tabCloseButtonStyle = tabCloseButtonStyle.arg( backgroundImage );
        tabCloseButtonStyle.append( tabCloseButtonHoverStyle );
    }
    else {
        tabCloseButtonStyle = tabCloseButtonStyle.arg( "" );
    }

    myTabBar_.setStyleSheet( tabStyle + tabCloseButtonStyle );

    setTabBar( &myTabBar_ );
    myTabBar_.hide();

    myTabBar_.setContextMenuPolicy( Qt::CustomContextMenu );
    connect( &myTabBar_, &CrawlerTabBar::showTabContextMenu, this,
             &TabbedCrawlerWidget::showContextMenu );

    dispatchToMainThread( [ this ] { loadIcons(); } );
}

void TabbedCrawlerWidget::loadIcons()
{
    IconLoader iconLoader{ this };
    olddata_icon_ = iconLoader.load( "olddata_icon" );
    for ( int tab = 0; tab < count(); ++tab ) {
        updateIcon( tab );
    }
}

void TabbedCrawlerWidget::changeEvent( QEvent* event )
{
    if ( event->type() == QEvent::StyleChange ) {
        dispatchToMainThread( [ this ] { loadIcons(); } );
    }

    QWidget::changeEvent( event );
}

void TabbedCrawlerWidget::addTabBarItem( int index, const QString& fileName )
{
    const auto tabLabel = QFileInfo( fileName ).fileName();
    const auto tabName = TabNameMapping::getSynced().tabName( fileName );

    myTabBar_.setTabIcon( index, olddata_icon_ );
    myTabBar_.setTabText( index, tabName.isEmpty() ? tabLabel : tabName );
    myTabBar_.setTabToolTip( index, QDir::toNativeSeparators( fileName ) );

    QVariantMap tabData;
    tabData[ PathKey ] = fileName;
    tabData[ StatusKey ] = static_cast<int>( DataStatus::OLD_DATA );

    myTabBar_.setTabData( index, tabData );

    updateTabGroupAppearance( index );

    setCurrentIndex( index );

    if ( count() > 1 )
        myTabBar_.show();
}

QString TabbedCrawlerWidget::baseTabName( int index ) const
{
    const auto path = tabPathAt( index );
    const auto customName = TabNameMapping::getSynced().tabName( path );
    return customName.isEmpty() ? QFileInfo( path ).fileName() : customName;
}

void TabbedCrawlerWidget::updateTabGroupAppearance( int index )
{
    const auto path = tabPathAt( index );
    const auto group = TabGroupInfo::getSynced().groupForTab( path );
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

void TabbedCrawlerWidget::removeCrawler( int index )
{
    QTabWidget::removeTab( index );

    // Keep the tab bar visible when only the pinned dashboard tab remains
    if ( count() <= 1 ) {
        myTabBar_.show();
    }
}

void TabbedCrawlerWidget::mouseReleaseEvent( QMouseEvent* event )
{
    LOG_DEBUG << "TabbedCrawlerWidget::mouseReleaseEvent";

    if ( event->button() == Qt::MiddleButton ) {
        int tab = this->myTabBar_.tabAt( event->pos() );
        if ( tab > 0 ) {
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
    // No context menu for the pinned dashboard tab
    if ( tab == 0 && !qobject_cast<CrawlerWidget*>( widget( 0 ) ) ) {
        return;
    }

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
    auto renameTab = menu.addAction( tr( "Rename tab" ) );
    auto resetTabName = menu.addAction( tr( "Reset tab name" ) );

    connect( closeThis, &QAction::triggered, [ tab, this ] { Q_EMIT tabCloseRequested( tab ); } );

    connect( closeOthers, &QAction::triggered, [ tab, this ] {
        QList<int> indices;
        for ( int i = 1; i < count(); ++i ) {
            if ( i != tab ) {
                indices.append( i );
            }
        }
        Q_EMIT bulkTabCloseRequested( indices );
    } );

    connect( closeLeft, &QAction::triggered, [ tab, this ] {
        QList<int> indices;
        for ( int i = 1; i < tab; ++i ) {
            indices.append( i );
        }
        Q_EMIT bulkTabCloseRequested( indices );
    } );

    connect( closeRight, &QAction::triggered, [ tab, this ] {
        QList<int> indices;
        for ( int i = tab + 1; i < count(); ++i ) {
            indices.append( i );
        }
        Q_EMIT bulkTabCloseRequested( indices );
    } );

    connect( closeAll, &QAction::triggered, [ this ] {
        QList<int> indices;
        for ( int i = 1; i < count(); ++i ) {
            indices.append( i );
        }
        Q_EMIT bulkTabCloseRequested( indices );
    } );

    if ( tab <= 1 ) {
        closeLeft->setDisabled( true );
    }
    else if ( tab == count() - 1 ) {
        closeRight->setDisabled( true );
    }

    connect( copyFullPath, &QAction::triggered, this,
             [ this, tab ] { sendTextToClipboard( tabToolTip( tab ) ); } );

    connect( openContainingFolder, &QAction::triggered, this,
             [ this, tab ] { showPathInFileExplorer( tabToolTip( tab ) ); } );

    connect( renameTab, &QAction::triggered, this, [ this, tab ] {
        bool isNameEntered = false;
        auto newName = QInputDialog::getText( this, "Rename tab", "Tab name", QLineEdit::Normal,
                                              myTabBar_.tabText( tab ), &isNameEntered );
        if ( isNameEntered ) {
            const auto tabPath = tabPathAt( tab );
            TabNameMapping::getSynced().setTabName( tabPath, newName ).save();
            updateTabGroupAppearance( tab );
        }
    } );

    connect( resetTabName, &QAction::triggered, this, [ this, tab ] {
        const auto tabPath = tabPathAt( tab );
        TabNameMapping::getSynced().setTabName( tabPath, "" ).save();
        updateTabGroupAppearance( tab );
    } );

    // --- Tab group operations ---
    const auto tabPath = tabPathAt( tab );
    const auto currentGroup = TabGroupInfo::getSynced().groupForTab( tabPath );

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
        connect( action, &QAction::triggered, this, [ this, groupId = group.id, tabPath ] {
            TabGroupInfo::getSynced().addTabToGroup( groupId, tabPath ).save();
            refreshAllTabGroupAppearances();
        } );
    }
    if ( !allGroups.empty() ) {
        addToGroupMenu->addSeparator();
    }
    auto* newGroupAction = addToGroupMenu->addAction( tr( "New Group..." ) );
    connect( newGroupAction, &QAction::triggered, this, [ this, tabPath ] {
        bool ok = false;
        const auto name
            = QInputDialog::getText( this, tr( "New Tab Group" ), tr( "Group name:" ),
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
        groupInfo.addTabToGroup( groupId, tabPath ).save();
        refreshAllTabGroupAppearances();
    } );

    // "Remove from Group" (enabled only if tab is in a group)
    auto* removeFromGroup = menu.addAction( tr( "Remove from Group" ) );
    removeFromGroup->setEnabled( currentGroup.has_value() );
    connect( removeFromGroup, &QAction::triggered, this, [ this, tabPath ] {
        TabGroupInfo::getSynced().removeTabFromGroup( tabPath ).save();
        refreshAllTabGroupAppearances();
    } );

    // Group management submenu (visible only if tab is in a group)
    if ( currentGroup.has_value() ) {
        auto* groupMenu
            = menu.addMenu( tr( "Group: %1" ).arg( currentGroup->name ) );
        const auto groupId = currentGroup->id;

        auto* renameGroupAction = groupMenu->addAction( tr( "Rename Group..." ) );
        connect( renameGroupAction, &QAction::triggered, this, [ this, groupId, currentGroup ] {
            bool ok = false;
            const auto newName = QInputDialog::getText(
                this, tr( "Rename Group" ), tr( "Group name:" ), QLineEdit::Normal,
                currentGroup->name, &ok );
            if ( ok && !newName.isEmpty() ) {
                TabGroupInfo::getSynced().renameGroup( groupId, newName ).save();
                refreshAllTabGroupAppearances();
            }
        } );

        auto* changeColorAction = groupMenu->addAction( tr( "Change Group Color..." ) );
        connect( changeColorAction, &QAction::triggered, this, [ this, groupId, currentGroup ] {
            const auto color = QColorDialog::getColor( currentGroup->color, this,
                                                       tr( "Group Color" ) );
            if ( color.isValid() ) {
                TabGroupInfo::getSynced().setGroupColor( groupId, color ).save();
                refreshAllTabGroupAppearances();
            }
        } );

        groupMenu->addSeparator();

        auto* closeAllInGroup = groupMenu->addAction( tr( "Close All in Group" ) );
        connect( closeAllInGroup, &QAction::triggered, this, [ this, groupId ] {
            const auto group = TabGroupInfo::getSynced().groupForTab( QString{} );
            const auto& groups = TabGroupInfo::getSynced().groups();
            auto it = std::find_if( groups.begin(), groups.end(),
                                    [ &groupId ]( const auto& g ) { return g.id == groupId; } );
            if ( it == groups.end() ) {
                return;
            }
            // Collect tab indices first, then close in reverse order
            const auto paths = it->tabPaths;
            for ( int i = count() - 1; i >= 1; --i ) {
                if ( paths.contains( tabPathAt( i ) ) ) {
                    Q_EMIT tabCloseRequested( i );
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
    if ( count() > 2 ) {
        menu.addSeparator();

        if ( tab > 1 ) {
            auto* mergeLeft = menu.addAction( tr( "Merge All Left" ) );
            connect( mergeLeft, &QAction::triggered, this, [ this, tab ] {
                QStringList paths;
                for ( int i = 1; i < tab; ++i ) {
                    paths.append( tabPathAt( i ) );
                }
                Q_EMIT mergeRequested( paths, false );
            } );

            auto* mergeLeftDedup = menu.addAction( tr( "Merge All Left (dedup)" ) );
            connect( mergeLeftDedup, &QAction::triggered, this, [ this, tab ] {
                QStringList paths;
                for ( int i = 1; i < tab; ++i ) {
                    paths.append( tabPathAt( i ) );
                }
                Q_EMIT mergeRequested( paths, true );
            } );
        }

        if ( tab < count() - 1 ) {
            auto* mergeRight = menu.addAction( tr( "Merge All Right" ) );
            connect( mergeRight, &QAction::triggered, this, [ this, tab ] {
                QStringList paths;
                for ( int i = tab + 1; i < count(); ++i ) {
                    paths.append( tabPathAt( i ) );
                }
                Q_EMIT mergeRequested( paths, false );
            } );

            auto* mergeRightDedup = menu.addAction( tr( "Merge All Right (dedup)" ) );
            connect( mergeRightDedup, &QAction::triggered, this, [ this, tab ] {
                QStringList paths;
                for ( int i = tab + 1; i < count(); ++i ) {
                    paths.append( tabPathAt( i ) );
                }
                Q_EMIT mergeRequested( paths, true );
            } );
        }
    }

    // --- Compare operation ---
    menu.addSeparator();
    auto* compareWith = menu.addAction( tr( "Compare with…" ) );
    connect( compareWith, &QAction::triggered, this, [ this, tab ] {
        Q_EMIT compareRequested( tabPathAt( tab ) );
    } );

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
