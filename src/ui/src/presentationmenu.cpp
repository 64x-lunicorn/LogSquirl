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

#include "presentationmenu.h"

#include <algorithm>
#include <map>
#include <optional>

#include <QAction>
#include <QActionGroup>
#include <QCoreApplication>
#include <QIcon>
#include <QKeySequence>
#include <QMenu>
#include <QPixmap>

#include "highlighterset.h"
#include "highlightersmenu.h"

namespace {

// An entry calling action when chosen, if there is an action.
QAction* addEntry( QMenu& menu, const QString& text, std::function<void()> action )
{
    auto* entry = menu.addAction( text );
    QObject::connect( entry, &QAction::triggered, entry, [ action = std::move( action ) ]() {
        if ( action ) {
            action();
        }
    } );
    return entry;
}

void addColorLabels( QMenu& colorLabelsMenu, const PresentationMenu::Report& report,
                     const PresentationMenu::Entries& entries )
{
    std::optional<size_t> currentLabel;
    for ( size_t i = 0; i < report.colorLabels.size(); ++i ) {
        if ( report.colorLabels[ i ].contains( report.selectedText ) ) {
            currentLabel = i;
            break;
        }
    }

    auto* colorLabelsActionGroup = new QActionGroup( &colorLabelsMenu );
    QObject::connect( colorLabelsActionGroup, &QActionGroup::triggered, &colorLabelsMenu,
                      [ addColorLabel = entries.addColorLabel,
                        clearColorLabels = entries.clearColorLabels ]( QAction* action ) {
                          if ( action->data().isValid() ) {
                              if ( addColorLabel ) {
                                  addColorLabel( static_cast<size_t>( action->data().toUInt() ) );
                              }
                          }
                          else if ( clearColorLabels ) {
                              clearColorLabels();
                          }
                      } );

    auto* noneAction = colorLabelsMenu.addAction( PresentationMenu::tr( "None" ) );
    noneAction->setActionGroup( colorLabelsActionGroup );
    noneAction->setCheckable( true );
    noneAction->setChecked( !currentLabel.has_value() );
    if ( currentLabel ) {
        noneAction->setData( static_cast<unsigned>( *currentLabel ) );
    }

    const auto& quickHighlightersConfiguration
        = HighlighterSetCollection::get().quickHighlighters();

    colorLabelsMenu.addSeparator();
    const auto maxLabel = std::min( report.colorLabels.size(),
                                    static_cast<size_t>( quickHighlightersConfiguration.size() ) );
    for ( size_t i = 0; i < maxLabel; ++i ) {
        const auto& labelConfiguration = quickHighlightersConfiguration.at( static_cast<int>( i ) );
        auto* colorLabelAction = colorLabelsMenu.addAction( labelConfiguration.name );
        colorLabelAction->setActionGroup( colorLabelsActionGroup );
        colorLabelAction->setCheckable( true );
        colorLabelAction->setChecked( currentLabel == i );
        colorLabelAction->setData( static_cast<unsigned>( i ) );

        QPixmap pixmap( 20, 10 );
        auto fillColor = labelConfiguration.color.backColor;
        fillColor.setAlphaF( 1.0 );
        pixmap.fill( fillColor );
        colorLabelAction->setIcon( QIcon( pixmap ) );
        colorLabelAction->setIconVisibleInMenu( true );
    }
    colorLabelsMenu.addSeparator();
    addEntry( colorLabelsMenu, PresentationMenu::tr( "Clear all" ), entries.clearColorLabels );
}

// The plugins' entries at the end of menu, each in the submenus its path
// names, which entries naming the same path share (#859).
void addPluginEntries( QMenu& menu, const std::vector<PresentationMenu::PluginEntry>& plugins )
{
    if ( plugins.empty() ) {
        return;
    }
    menu.addSeparator();
    std::map<QString, QMenu*> submenus;
    for ( const auto& plugin : plugins ) {
        auto* parent = &menu;
        QString path;
        for ( const auto& segment : plugin.menuPath.split( u'/' ) ) {
            const auto name = segment.trimmed();
            if ( name.isEmpty() ) {
                continue;
            }
            path += u'/' + name;
            auto& submenu = submenus[ path ];
            if ( submenu == nullptr ) {
                submenu = parent->addMenu( name );
            }
            parent = submenu;
        }
        addEntry( *parent, plugin.label, plugin.choose );
    }
}

// The menu being built: what the Presentation reported, what each entry does,
// and what the selection is.
struct Opened {
    Opened( const PresentationMenu::Report& openedReport,
            const PresentationMenu::Entries& openedEntries )
        : report( openedReport )
        , entries( openedEntries )
        , hasSelection( !openedReport.selectedLogLines.empty() )
        , textWithinLogLine( openedReport.textWithinLogLine )
        , oneWholeLogLine( !openedReport.textWithinLogLine
                           && openedReport.selectedLogLines.size() == 1 )
    {
    }

    const PresentationMenu::Report& report;
    const PresentationMenu::Entries& entries;
    bool hasSelection;
    bool textWithinLogLine;
    bool oneWholeLogLine;
};

// The top of the menu: what the user does with the selection, copied,
// searched for, Marked (#894). An entry for the selection goes here.
void addSelectionBlock( QMenu& menu, const Opened& opened )
{
    const auto& report = opened.report;
    const auto& entries = opened.entries;
    const bool textWithinLogLine = opened.textWithinLogLine;

    auto* copy = addEntry( menu,
                           opened.oneWholeLogLine ? PresentationMenu::tr( "&Copy this line" )
                                                  : PresentationMenu::tr( "&Copy" ),
                           entries.copy );
    copy->setEnabled( opened.hasSelection );
    if ( !opened.oneWholeLogLine ) {
        copy->setStatusTip( PresentationMenu::tr( "Copy the selection" ) );
    }
    if ( entries.copyAsShown ) {
        auto* copyAsShown
            = addEntry( menu, PresentationMenu::tr( "Copy as shown" ), entries.copyAsShown );
        copyAsShown->setStatusTip(
            PresentationMenu::tr( "Copy the selection as shown, with its Value Names" ) );
        copyAsShown->setEnabled( opened.hasSelection && report.showsValueNames );
    }

    menu.addSeparator();
    auto* replaceSearch
        = addEntry( menu, PresentationMenu::tr( "&Replace search" ), entries.replaceSearch );
    replaceSearch->setStatusTip(
        PresentationMenu::tr( "Replace the search expression with the selection" ) );
    replaceSearch->setEnabled( textWithinLogLine );
    auto* addToSearch
        = addEntry( menu, PresentationMenu::tr( "&Add to search" ), entries.addToSearch );
    addToSearch->setStatusTip( PresentationMenu::tr( "Add the selection to the current search" ) );
    addToSearch->setEnabled( textWithinLogLine );
    addEntry( menu, PresentationMenu::tr( "&Exclude from search" ),
              [ textWithinLogLine, excludeFromSearch = entries.excludeFromSearch ]() {
                  if ( textWithinLogLine && excludeFromSearch ) {
                      excludeFromSearch();
                  }
              } )
        ->setStatusTip( PresentationMenu::tr( "Excludes the selection from search" ) );
    // The Table View's cell: the Search narrowed to the Log Lines whose
    // field has the cell's value, as a Field Term (#798).
    if ( entries.searchValueInColumn && !report.columnUnderCursor.isEmpty() ) {
        addEntry( menu,
                  PresentationMenu::tr( "Search this value in %1" ).arg( report.columnUnderCursor ),
                  entries.searchValueInColumn )
            ->setStatusTip( PresentationMenu::tr(
                                "Narrows the search to the lines whose %1 field has the value "
                                "of this cell, as a logical combination" )
                                .arg( report.columnUnderCursor ) );
    }
    // A Predefined Filter made from the text selected within a Log Line
    // (#897).
    if ( entries.addAsPredefinedFilter ) {
        auto* addAsPredefinedFilter
            = addEntry( menu, PresentationMenu::tr( "Add as Predefined Filter..." ),
                        entries.addAsPredefinedFilter );
        addAsPredefinedFilter->setStatusTip(
            PresentationMenu::tr( "Save the selection as a Predefined Filter" ) );
        addAsPredefinedFilter->setEnabled( textWithinLogLine && !report.selectedText.isEmpty() );
    }

    menu.addSeparator();
    addEntry( menu,
              report.hasUnmarkedLogLines ? PresentationMenu::tr( "&Mark" )
                                         : PresentationMenu::tr( "Unmark" ),
              entries.mark )
        ->setEnabled( opened.hasSelection );

    // A Value Name made from the value the user points at (#883); one a
    // Naming Rule captures already is named in its Name Table (#885).
    if ( entries.nameValue && report.capturedValue ) {
        const auto& captured = *report.capturedValue;
        // Shown as text: an & is no mnemonic, a long value is cut short.
        const auto shown = []( QString text ) {
            constexpr qsizetype MaxShown = 32;
            if ( text.size() > MaxShown ) {
                text = text.left( MaxShown - 3 ) + QStringLiteral( "..." );
            }
            return text.replace( u'&', QStringLiteral( "&&" ) );
        };
        const auto value = shown( captured.value );
        const auto table = shown( captured.table );
        QString text;
        if ( captured.teamGroup ) {
            text = captured.named ? PresentationMenu::tr( "Rename %1 %2 (Team group, read-only)" )
                                        .arg( table, value )
                                  : PresentationMenu::tr( "Name %1 in %2 (Team group, read-only)" )
                                        .arg( value, table );
        }
        else {
            text = captured.named ? PresentationMenu::tr( "Rename %1 %2..." ).arg( table, value )
                                  : PresentationMenu::tr( "Name %1 in %2..." ).arg( value, table );
        }
        auto* nameValue = addEntry( menu, text, entries.nameValue );
        nameValue->setEnabled( !captured.teamGroup );
        nameValue->setStatusTip(
            captured.teamGroup
                ? PresentationMenu::tr(
                      "The Naming Rule is a Team group's: change its Name Table in the Value Names "
                      "dialog" )
            : captured.named
                ? PresentationMenu::tr( "Change the name the Name Table gives this value" )
                : PresentationMenu::tr( "Add the value to the Name Table its Naming Rule uses" ) );
    }
    else if ( entries.nameValue ) {
        auto* nameValue
            = addEntry( menu, PresentationMenu::tr( "Name this value..." ), entries.nameValue );
        nameValue->setStatusTip(
            PresentationMenu::tr( "Show this value of the Log Line as a name" ) );
        nameValue->setEnabled( report.hasValueToName );
    }
}

// Below the selection's entries: those of the view itself, how it colors and
// shows the Log Lines (#894). An entry about the view goes here.
void addViewBlock( QMenu& menu, const Opened& opened )
{
    // A Highlighter made from the text selected within a Log Line (#896).
    if ( opened.entries.highlightThis ) {
        auto* highlightThis = addEntry( menu, PresentationMenu::tr( "Highlight this..." ),
                                        opened.entries.highlightThis );
        highlightThis->setStatusTip(
            PresentationMenu::tr( "Color the selected text with a new Highlighter" ) );
        highlightThis->setEnabled( opened.textWithinLogLine
                                   && !opened.report.selectedText.isEmpty() );
    }

    auto* highlightersMenu = new HighlightersMenu( PresentationMenu::tr( "Highlighters" ), &menu );
    highlightersMenu->createHighlightersMenu();
    highlightersMenu->populateHighlightersMenu();
    highlightersMenu->setApplyChange( opened.entries.highlightersChange );
    menu.addMenu( highlightersMenu );

    auto* colorLabelsMenu = menu.addMenu( PresentationMenu::tr( "Color labels" ) );
    colorLabelsMenu->setEnabled( opened.textWithinLogLine || opened.oneWholeLogLine );
    if ( colorLabelsMenu->isEnabled() ) {
        addColorLabels( *colorLabelsMenu, opened.report, opened.entries );
    }
}

// The More submenu: the entries used rarely, kept off the top so that it
// stays short (#894). A rarely used entry goes here.
void addMoreSubmenu( QMenu& menu, const Opened& opened )
{
    const auto& report = opened.report;
    const auto& entries = opened.entries;
    const bool oneWholeLogLine = opened.oneWholeLogLine;

    auto& more = *menu.addMenu( PresentationMenu::tr( "More" ) );

    addEntry( more,
              oneWholeLogLine ? PresentationMenu::tr( "Copy this line with line number" )
                              : PresentationMenu::tr( "Copy with line numbers" ),
              entries.copyWithLineNumbers )
        ->setEnabled( opened.hasSelection );
    addEntry( more, PresentationMenu::tr( "Send to scratchpad" ), entries.sendToScratchpad )
        ->setEnabled( opened.hasSelection );
    addEntry( more, PresentationMenu::tr( "Replace scratchpad" ), entries.replaceScratchpad )
        ->setEnabled( opened.hasSelection );

    more.addSeparator();
    // The shortcuts are the Text View's, shown there as a reminder; the Table
    // View does not bind them.
    auto* findNext = addEntry( more, PresentationMenu::tr( "Find &next" ), entries.findNext );
    if ( report.drawnLikeTextView ) {
        findNext->setShortcut( Qt::Key_Asterisk );
    }
    findNext->setStatusTip( PresentationMenu::tr( "Find the next occurrence" ) );
    findNext->setEnabled( opened.textWithinLogLine );
    auto* findPrevious
        = addEntry( more, PresentationMenu::tr( "Find &previous" ), entries.findPrevious );
    if ( report.drawnLikeTextView ) {
        findPrevious->setShortcut( QKeySequence( PresentationMenu::tr( "/" ) ) );
    }
    findPrevious->setStatusTip( PresentationMenu::tr( "Find the previous occurrence" ) );
    findPrevious->setEnabled( opened.textWithinLogLine );

    more.addSeparator();
    // The Log Line under the cursor; where there is none, the selected one.
    const auto searchLimitLine = report.logLineUnderCursor ? report.logLineUnderCursor
                                 : oneWholeLogLine
                                     ? OptionalLineNumber{ report.selectedLogLines.front() }
                                     : OptionalLineNumber{};
    // The Text View sets them only while one whole Log Line is selected.
    const bool canSetSearchLimit
        = searchLimitLine.has_value() && ( !report.drawnLikeTextView || oneWholeLogLine );
    addEntry( more, PresentationMenu::tr( "Set search start" ),
              [ searchLimitLine, setSearchStart = entries.setSearchStart ]() {
                  if ( searchLimitLine && setSearchStart ) {
                      setSearchStart( *searchLimitLine );
                  }
              } )
        ->setEnabled( canSetSearchLimit );
    addEntry( more, PresentationMenu::tr( "Set search end" ),
              [ searchLimitLine, setSearchEnd = entries.setSearchEnd ]() {
                  if ( searchLimitLine && setSearchEnd ) {
                      setSearchEnd( *searchLimitLine );
                  }
              } )
        ->setEnabled( canSetSearchLimit );
    addEntry( more, PresentationMenu::tr( "Clear search limits" ), entries.clearSearchLimits );

    if ( entries.setSelectionStart || entries.setSelectionEnd ) {
        more.addSeparator();
        addEntry( more, PresentationMenu::tr( "Set selection start" ), entries.setSelectionStart )
            ->setEnabled( oneWholeLogLine );
        addEntry( more, PresentationMenu::tr( "Set selection end" ), entries.setSelectionEnd )
            ->setEnabled( oneWholeLogLine && report.selectionStartSet );
    }

    more.addSeparator();
    addEntry( more, PresentationMenu::tr( "Save selected to file" ), entries.saveSelectedToFile )
        ->setEnabled( opened.hasSelection );
    addEntry( more, PresentationMenu::tr( "Save to file" ), entries.saveToFile );
    if ( entries.exportAsCsv ) {
        addEntry( more, PresentationMenu::tr( "Export as CSV..." ), entries.exportAsCsv );
    }

    more.addSeparator();
    addEntry( more, PresentationMenu::tr( "Save splitter position" ),
              entries.saveSplitterPosition );
}

} // namespace

std::unique_ptr<QMenu> PresentationMenu::create( QWidget* parent, const Report& report,
                                                 const Entries& entries )
{
    auto menu = std::make_unique<QMenu>( parent );
    const Opened opened( report, entries );

    addSelectionBlock( *menu, opened );
    menu->addSeparator();
    addViewBlock( *menu, opened );
    menu->addSeparator();
    addMoreSubmenu( *menu, opened );
    addPluginEntries( *menu, entries.plugins );

    return menu;
}
