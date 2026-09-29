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

    // Emitted when the user confirms Merge… in a tab's context menu: the
    // paths of the Log Files checked in its dialog, in the order chosen there
    // (#571). `dedup` is true when duplicate lines are to be dropped.
    Q_SIGNAL void mergeRequested( QStringList filePaths, bool dedup );

    // Adds the tab of a Log File, last or at `position`. The tab of a
    // Transient Log File can be renamed and grouped like any other, but
    // neither is stored (#597). The name and group of any other tab are stored
    // by `storedKey`, or by `fileName` when it is empty: a Log File
    // decompressed from an archive is stored by its archive and member, which
    // name it again after a restart where its temporary path does not (#609).
    template <typename T>
    int addCrawler( T* crawler, const QString& fileName,
                    LogFileLifetime lifetime = LogFileLifetime::Ordinary,
                    const QString& storedKey = {}, int position = -1 )
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

        addTabBarItem( index, fileName, lifetime, storedKey );

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

    // The opening title of the tab of `path`: the title and tooltip it gets,
    // instead of its file's name and path, when it opens -- standard input, a
    // data source or a merge. It lasts until the tab closes and is never
    // stored. A rename by the user wins over it; grouping and a reset of the
    // rename give it back, never the file's name (#606). An empty title
    // forgets it; an empty tooltip leaves the path.
    void setOpeningTitle( const QString& path, const QString& title, const QString& toolTip = {} );

    // Whether the tab at `index` holds a Transient Log File.
    bool holdsTransientLogFile( int index ) const;

    // Renames the tab at `index`; an empty name gives it back its own. The
    // name of an Ordinary Log File's tab is stored with its path, or with its
    // archive and member for one decompressed from an archive (#609); that of
    // a Transient Log File's tab lasts until the tab closes (#597).
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
    void addTabBarItem( int index, const QString& fileName, LogFileLifetime lifetime,
                        const QString& storedKey );
    QString tabPathAt( int index ) const;
    // What the name and tab group of the tab at `index` are stored by: the
    // key it was added with, else its path (#609).
    QString storedKeyAt( int index ) const;

    // Applies group styling (bullet prefix + text colour) to a single tab.
    void updateTabGroupAppearance( int index );

    // The name a tab shows, before its group's bullet: the user's rename,
    // else its opening title, else its file's name (#606).
    QString baseTabName( int index ) const;

    // Asks with the Merge dialog which Log Files to merge, in which order,
    // and requests that merge when the user confirms (#571).
    void chooseFilesToMerge();

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
    struct OpeningTitle {
        QString title;
        QString toolTip;
    };
    // The opening titles, by path (#606).
    QHash<QString, OpeningTitle> openingTitles_;
    // What the user made of the tabs of Transient Log Files, by path: their
    // names and the ids of their tab groups. Never stored (#597).
    QHash<QString, QString> renamedTransientTabs_;
    QHash<QString, QString> transientTabGroups_;
};

#endif
