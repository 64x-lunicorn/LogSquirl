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

#include "commandpalette.h"

#include <algorithm>

#include <QApplication>
#include <QFont>
#include <QHBoxLayout>
#include <QPainter>
#include <QScreen>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

namespace {

/// Custom delegate that renders the category badge, command name,
/// and keyboard shortcut in a single row.
class CommandDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint( QPainter* painter, const QStyleOptionViewItem& option,
                const QModelIndex& index ) const override
    {
        QStyledItemDelegate::paint( painter, option, index );

        painter->save();

        const auto category = index.data( Qt::UserRole + 1 ).toString();
        const auto shortcut = index.data( Qt::UserRole + 2 ).toString();

        const auto rect = option.rect;

        // Category badge
        if ( !category.isEmpty() ) {
            auto badgeFont = option.font;
            badgeFont.setPointSizeF( badgeFont.pointSizeF() * 0.85 );
            painter->setFont( badgeFont );

            const QFontMetrics fm( badgeFont );
            const auto textRect = fm.boundingRect( category );
            const int pad = 4;
            const int badgeW = textRect.width() + 2 * pad;
            const int badgeH = fm.height() + 2;
            const int badgeY = rect.top() + ( rect.height() - badgeH ) / 2;

            QRect badge( rect.right() - badgeW - 8, badgeY, badgeW, badgeH );

            if ( !shortcut.isEmpty() ) {
                const QFontMetrics sfm( option.font );
                badge.moveLeft( rect.right() - badgeW - sfm.horizontalAdvance( shortcut ) - 24 );
            }

            const auto palette = option.palette;
            const auto badgeBg
                = palette.color( QPalette::Active, QPalette::Highlight ).lighter( 160 );
            const auto badgeFg = palette.color( QPalette::Active, QPalette::HighlightedText );

            painter->setRenderHint( QPainter::Antialiasing );
            painter->setBrush( badgeBg );
            painter->setPen( Qt::NoPen );
            painter->drawRoundedRect( badge, 3, 3 );

            painter->setPen( badgeFg );
            painter->drawText( badge, Qt::AlignCenter, category );
        }

        // Shortcut text (right-aligned)
        if ( !shortcut.isEmpty() ) {
            auto shortcutFont = option.font;
            shortcutFont.setPointSizeF( shortcutFont.pointSizeF() * 0.9 );
            painter->setFont( shortcutFont );

            const auto palette = option.palette;
            painter->setPen(
                palette.color( QPalette::Active, QPalette::Text ).lighter( 140 ) );

            QRect shortcutRect = rect;
            shortcutRect.setRight( rect.right() - 8 );
            painter->drawText( shortcutRect, Qt::AlignVCenter | Qt::AlignRight, shortcut );
        }

        painter->restore();
    }

    QSize sizeHint( const QStyleOptionViewItem& option, const QModelIndex& index ) const override
    {
        auto hint = QStyledItemDelegate::sizeHint( option, index );
        hint.setHeight( std::max( hint.height(), 32 ) );
        return hint;
    }
};

} // namespace

CommandPalette::CommandPalette( QWidget* parent )
    : QDialog( parent, Qt::Popup | Qt::FramelessWindowHint )
{
    setMinimumWidth( 560 );
    setMaximumWidth( 800 );

    auto* layout = new QVBoxLayout( this );
    layout->setContentsMargins( 0, 0, 0, 0 );
    layout->setSpacing( 0 );

    input_ = new QLineEdit( this );
    input_->setPlaceholderText( tr( "Type a command..." ) );
    input_->setClearButtonEnabled( true );
    input_->setMinimumHeight( 36 );

    auto inputFont = input_->font();
    inputFont.setPointSizeF( inputFont.pointSizeF() * 1.15 );
    input_->setFont( inputFont );

    list_ = new QListWidget( this );
    list_->setItemDelegate( new CommandDelegate( list_ ) );
    list_->setHorizontalScrollBarPolicy( Qt::ScrollBarAlwaysOff );
    list_->setMinimumHeight( 200 );
    list_->setMaximumHeight( 400 );

    layout->addWidget( input_ );
    layout->addWidget( list_ );

    // Styling
    setStyleSheet( QStringLiteral(
        "CommandPalette { border: 1px solid palette(mid); border-radius: 6px; }"
        "QLineEdit { border: none; border-bottom: 1px solid palette(mid);"
        "  padding: 8px 12px; background: palette(base); }"
        "QListWidget { border: none; background: palette(base); }"
        "QListWidget::item { padding: 4px 12px; }"
        "QListWidget::item:selected { background: palette(highlight);"
        "  color: palette(highlighted-text); }" ) );

    // Connect filter updates
    connect( input_, &QLineEdit::textChanged, this, &CommandPalette::updateFilter );

    // Double-click or Enter on list
    connect( list_, &QListWidget::itemActivated, this, &CommandPalette::acceptCurrent );

    // Install event filter to capture Up/Down/Enter/Escape on the input
    input_->installEventFilter( this );
}

void CommandPalette::setCommands( std::vector<CommandEntry> commands )
{
    commands_ = std::move( commands );
    input_->clear();
    updateFilter();

    // Position at the top-centre of the parent window
    if ( parentWidget() ) {
        const auto parentRect = parentWidget()->geometry();
        const int x = parentRect.x() + ( parentRect.width() - width() ) / 2;
        const int y = parentRect.y() + 60;
        move( x, y );
    }
}

bool CommandPalette::eventFilter( QObject* obj, QEvent* event )
{
    if ( obj == input_ && event->type() == QEvent::KeyPress ) {
        auto* keyEvent = static_cast<QKeyEvent*>( event );
        const auto key = keyEvent->key();

        if ( key == Qt::Key_Down ) {
            const auto row = list_->currentRow();
            if ( row < list_->count() - 1 ) {
                list_->setCurrentRow( row + 1 );
            }
            return true;
        }
        if ( key == Qt::Key_Up ) {
            const auto row = list_->currentRow();
            if ( row > 0 ) {
                list_->setCurrentRow( row - 1 );
            }
            return true;
        }
        if ( key == Qt::Key_Return || key == Qt::Key_Enter ) {
            acceptCurrent();
            return true;
        }
        if ( key == Qt::Key_Escape ) {
            close();
            return true;
        }
    }
    return QDialog::eventFilter( obj, event );
}

void CommandPalette::updateFilter()
{
    const auto pattern = input_->text().trimmed();

    list_->clear();

    // Score and collect matching entries
    struct ScoredEntry {
        int index;
        int score;
    };

    std::vector<ScoredEntry> matches;
    matches.reserve( commands_.size() );

    for ( int i = 0; i < static_cast<int>( commands_.size() ); ++i ) {
        auto& cmd = commands_[ static_cast<size_t>( i ) ];

        if ( pattern.isEmpty() ) {
            matches.push_back( { i, 0 } );
            continue;
        }

        // Match against name and category
        const auto nameScore = fuzzyScore( pattern, cmd.name );
        const auto catScore = fuzzyScore( pattern, cmd.category + " " + cmd.name );

        const auto best = ( nameScore >= 0 && catScore >= 0 ) ? std::min( nameScore, catScore )
                          : ( nameScore >= 0 )                ? nameScore
                          : ( catScore >= 0 )                 ? catScore
                                                              : -1;

        if ( best >= 0 ) {
            matches.push_back( { i, best } );
        }
    }

    // Sort by score (lower = better)
    std::sort( matches.begin(), matches.end(),
               []( const ScoredEntry& a, const ScoredEntry& b ) { return a.score < b.score; } );

    // Populate list widget
    for ( const auto& m : matches ) {
        const auto& cmd = commands_[ static_cast<size_t>( m.index ) ];
        auto* item = new QListWidgetItem( cmd.name, list_ );
        item->setData( Qt::UserRole, m.index );
        item->setData( Qt::UserRole + 1, cmd.category );
        item->setData( Qt::UserRole + 2, cmd.shortcut );
    }

    if ( list_->count() > 0 ) {
        list_->setCurrentRow( 0 );
    }
}

void CommandPalette::acceptCurrent()
{
    const auto* item = list_->currentItem();
    if ( !item ) {
        return;
    }

    const auto idx = item->data( Qt::UserRole ).toInt();
    if ( idx >= 0 && idx < static_cast<int>( commands_.size() ) ) {
        const auto& cmd = commands_[ static_cast<size_t>( idx ) ];
        close();
        if ( cmd.action ) {
            cmd.action();
        }
    }
}

int CommandPalette::fuzzyScore( const QString& pattern, const QString& text )
{
    if ( pattern.isEmpty() ) {
        return 0;
    }

    const auto patternLen = pattern.length();
    const auto textLen = text.length();

    int patternIdx = 0;
    int score = 0;
    int lastMatchPos = -1;
    bool prevMatched = false;

    for ( int textIdx = 0; textIdx < textLen && patternIdx < patternLen; ++textIdx ) {
        if ( pattern[ patternIdx ].toLower() == text[ textIdx ].toLower() ) {
            // Bonus for consecutive matches
            if ( prevMatched ) {
                score -= 5;
            }

            // Bonus for match at word boundary (start, after space/separator)
            if ( textIdx == 0 || text[ textIdx - 1 ] == ' ' || text[ textIdx - 1 ] == '/'
                 || text[ textIdx - 1 ] == ':' || text[ textIdx - 1 ] == '.'
                 || text[ textIdx - 1 ] == '_' || text[ textIdx - 1 ] == '-' ) {
                score -= 10;
            }

            // Bonus for exact case match
            if ( pattern[ patternIdx ] == text[ textIdx ] ) {
                score -= 1;
            }

            // Penalty for gap between matches
            if ( lastMatchPos >= 0 ) {
                const auto gap = textIdx - lastMatchPos - 1;
                score += gap;
            }

            lastMatchPos = textIdx;
            patternIdx++;
            prevMatched = true;
        }
        else {
            prevMatched = false;
        }
    }

    // All pattern characters must be found
    if ( patternIdx < patternLen ) {
        return -1;
    }

    // Penalty for remaining unmatched text length
    score += static_cast<int>( ( textLen - patternLen ) / 3 );

    return score;
}
