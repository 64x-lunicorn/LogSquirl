/*
 * Copyright (C) 2014 Nicolas Bonnefon and other contributors
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

#ifndef TABBEDCRAWLERWIDGET_H
#define TABBEDCRAWLERWIDGET_H

#include <optional>

#include <QHash>
#include <QTabBar>
#include <QTabWidget>
#include <qobjectdefs.h>
#include <qtabbar.h>
#include <qwidget.h>

#include "loadingstatus.h"
#include "session.h"
#include "tabgroupinfo.h"

// This class represents glogg's main widget, a tabbed
// group of CrawlerWidgets.
// This is a very slightly customised QTabWidget, with
// a particular style.

class CrawlerTabBar : public QTabBar {
    Q_OBJECT

Q_SIGNALS:
    void showTabContextMenu( int tab, QPoint point );

protected:
    void mouseReleaseEvent( QMouseEvent* ) override;
};

class TabbedCrawlerWidget : public QTabWidget {
    Q_OBJECT
public:
    TabbedCrawlerWidget();

    // Emitted when multiple tabs should be closed at once (indices in order).
    Q_SIGNAL void bulkTabCloseRequested( QList<int> indices );

    // Emitted when the user requests a merge of tabs left or right of the given tab.
    // `dedup` is true when duplicate-line removal was requested.
    Q_SIGNAL void mergeRequested( QStringList filePaths, bool dedup );

    // Adds the tab of a Log File, last or at `position`. The tab of a
    // Transient Log File can be renamed and grouped like any other, but
    // neither is stored (#597).
    template <typename T>
    int addCrawler( T* crawler, const QString& fileName,
                    LogFileLifetime lifetime = LogFileLifetime::Ordinary, int position = -1 )
    {
        const auto index = QTabWidget::insertTab( position, crawler, QString{} );

        connect( crawler, &T::dataStatusChanged, this, [ this, fileName ]( DataStatus status ) {
            const auto tabsCount = count();
            for ( int i = 0; i < tabsCount; ++i ) {
                if ( tabPathAt( i ) == fileName ) {
                    setTabDataStatus( i, status );
                    return;
                }
            }
        } );

        addTabBarItem( index, fileName, lifetime );

        return index;
    }

    void removeCrawler( int index );

    // Whether the tab at `index` holds a Log File. The dashboard, when the
    // window shows one, is a tab that does not; nothing is assumed about
    // where it sits.
    bool holdsLogFile( int index ) const;

    // The tabs that hold a Log File, in tab order. Every path that walks the
    // tabs for their Log Files -- to close, merge, save or find them -- asks
    // this.
    QList<int> logFileTabs() const;

    // The tab that holds the file at `path`, found by the path it was added
    // under; -1 when none does.
    int tabOfPath( const QString& path ) const;

    // Names the tab of `path`, when it is opened next, until the window
    // closes. Unlike a renamed tab it is not saved; a rename by the user wins.
    void setTransientTabName( const QString& path, const QString& name );

    // Whether the tab at `index` holds a Transient Log File.
    bool holdsTransientLogFile( int index ) const;

    // Renames the tab at `index`; an empty name gives it back its own. The
    // name of an Ordinary Log File's tab is stored with its path, that of a
    // Transient Log File's tab lasts until the tab closes (#597).
    void renameTab( int index, const QString& name );

    // Puts the tab at `index` in the tab group `groupId`, or takes it out of
    // its group. As with a rename, a Transient Log File's tab is a member only
    // until it closes, and its path is not stored (#597).
    void addTabToGroup( int index, const QString& groupId );
    void removeTabFromGroup( int index );

    // The tab group the tab at `index` is in, if any.
    std::optional<TabGroupInfo::TabGroup> groupOfTab( int index ) const;

protected:
    void keyPressEvent( QKeyEvent* event ) override;
    void mouseReleaseEvent( QMouseEvent* event ) override;

public:
    // Re-applies group styling (bullet prefix + text colour) to all tabs.
    void refreshAllTabGroupAppearances();

private:
    void addTabBarItem( int index, const QString& fileName, LogFileLifetime lifetime );
    QString tabPathAt( int index ) const;

    // Applies group styling (bullet prefix + text colour) to a single tab.
    void updateTabGroupAppearance( int index );

    // Returns the base display name for a tab (custom rename or filename).
    QString baseTabName( int index ) const;

    // Set the data status (icon) for the tab number 'index'
    void setTabDataStatus( int index, DataStatus status );

    // Styles the tab bar for the active Theme.
    void applyTheme();

    void loadIcons();
    void updateIcon( int index );

private Q_SLOTS:
    void showContextMenu( int tab, QPoint globalPoint );

private:
    QIcon olddata_icon_;
    QIcon newdata_icon_;
    QIcon newfiltered_icon_;

    CrawlerTabBar myTabBar_;
    QHash<QString, QString> transientTabNames_;
    // What the user made of the tabs of Transient Log Files, by path: their
    // names and the ids of their tab groups. Never stored (#597).
    QHash<QString, QString> renamedTransientTabs_;
    QHash<QString, QString> transientTabGroups_;
};

#endif
