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

QString tr( const char* text )
{
    return QCoreApplication::translate( "PresentationMenu", text );
}

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

    auto* noneAction = colorLabelsMenu.addAction( tr( "None" ) );
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
    addEntry( colorLabelsMenu, tr( "Clear all" ), entries.clearColorLabels );
}

} // namespace

std::unique_ptr<QMenu> PresentationMenu::create( QWidget* parent, const Report& report,
                                                 const Entries& entries )
{
    auto menu = std::make_unique<QMenu>( parent );

    const bool hasSelection = !report.selectedLogLines.empty();
    const bool textWithinLogLine = report.textWithinLogLine;
    const bool oneWholeLogLine = !textWithinLogLine && report.selectedLogLines.size() == 1;

    auto* highlightersMenu = new HighlightersMenu( tr( "Highlighters" ), menu.get() );
    highlightersMenu->createHighlightersMenu();
    highlightersMenu->populateHighlightersMenu();
    highlightersMenu->setApplyChange( entries.highlightersChange );
    menu->addMenu( highlightersMenu );

    auto* colorLabelsMenu = menu->addMenu( tr( "Color labels" ) );
    colorLabelsMenu->setEnabled( textWithinLogLine || oneWholeLogLine );
    if ( colorLabelsMenu->isEnabled() ) {
        addColorLabels( *colorLabelsMenu, report, entries );
    }

    menu->addSeparator();
    addEntry( *menu, report.hasUnmarkedLogLines ? tr( "&Mark" ) : tr( "Unmark" ), entries.mark )
        ->setEnabled( hasSelection );

    menu->addSeparator();
    auto* copy = addEntry( *menu, oneWholeLogLine ? tr( "&Copy this line" ) : tr( "&Copy" ),
                           entries.copy );
    copy->setEnabled( hasSelection );
    if ( !oneWholeLogLine ) {
        copy->setStatusTip( tr( "Copy the selection" ) );
    }
    addEntry( *menu,
              oneWholeLogLine ? tr( "Copy this line with line number" )
                              : tr( "Copy with line numbers" ),
              entries.copyWithLineNumbers )
        ->setEnabled( hasSelection );
    addEntry( *menu, tr( "Send to scratchpad" ), entries.sendToScratchpad )
        ->setEnabled( hasSelection );
    addEntry( *menu, tr( "Replace scratchpad" ), entries.replaceScratchpad )
        ->setEnabled( hasSelection );

    menu->addSeparator();
    // The shortcuts are the Text View's, shown here as a reminder.
    auto* findNext = addEntry( *menu, tr( "Find &next" ), entries.findNext );
    findNext->setShortcut( Qt::Key_Asterisk );
    findNext->setStatusTip( tr( "Find the next occurrence" ) );
    findNext->setEnabled( textWithinLogLine );
    auto* findPrevious = addEntry( *menu, tr( "Find &previous" ), entries.findPrevious );
    findPrevious->setShortcut( QKeySequence( tr( "/" ) ) );
    findPrevious->setStatusTip( tr( "Find the previous occurrence" ) );
    findPrevious->setEnabled( textWithinLogLine );

    menu->addSeparator();
    auto* replaceSearch = addEntry( *menu, tr( "&Replace search" ), entries.replaceSearch );
    replaceSearch->setStatusTip( tr( "Replace the search expression with the selection" ) );
    replaceSearch->setEnabled( textWithinLogLine );
    auto* addToSearch = addEntry( *menu, tr( "&Add to search" ), entries.addToSearch );
    addToSearch->setStatusTip( tr( "Add the selection to the current search" ) );
    addToSearch->setEnabled( textWithinLogLine );
    addEntry( *menu, tr( "&Exclude from search" ),
              [ textWithinLogLine, excludeFromSearch = entries.excludeFromSearch ]() {
                  if ( textWithinLogLine && excludeFromSearch ) {
                      excludeFromSearch();
                  }
              } )
        ->setStatusTip( tr( "Excludes the selection from search" ) );

    menu->addSeparator();
    // The Log Line under the cursor; where there is none, the selected one.
    const auto searchLimitLine = report.logLineUnderCursor ? report.logLineUnderCursor
                                 : oneWholeLogLine
                                     ? OptionalLineNumber{ report.selectedLogLines.front() }
                                     : OptionalLineNumber{};
    const bool canSetSearchLimit = oneWholeLogLine && searchLimitLine.has_value();
    addEntry( *menu, tr( "Set search start" ),
              [ searchLimitLine, setSearchStart = entries.setSearchStart ]() {
                  if ( searchLimitLine && setSearchStart ) {
                      setSearchStart( *searchLimitLine );
                  }
              } )
        ->setEnabled( canSetSearchLimit );
    addEntry( *menu, tr( "Set search end" ),
              [ searchLimitLine, setSearchEnd = entries.setSearchEnd ]() {
                  if ( searchLimitLine && setSearchEnd ) {
                      setSearchEnd( *searchLimitLine );
                  }
              } )
        ->setEnabled( canSetSearchLimit );
    addEntry( *menu, tr( "Clear search limits" ), entries.clearSearchLimits );

    if ( entries.setSelectionStart || entries.setSelectionEnd ) {
        menu->addSeparator();
        addEntry( *menu, tr( "Set selection start" ), entries.setSelectionStart )
            ->setEnabled( oneWholeLogLine );
        addEntry( *menu, tr( "Set selection end" ), entries.setSelectionEnd )
            ->setEnabled( oneWholeLogLine && report.selectionStartSet );
    }

    menu->addSeparator();
    addEntry( *menu, tr( "Save splitter position" ), entries.saveSplitterPosition );
    addEntry( *menu, tr( "Save to file" ), entries.saveToFile );
    addEntry( *menu, tr( "Save selected to file" ), entries.saveSelectedToFile )
        ->setEnabled( hasSelection );

    return menu;
}
