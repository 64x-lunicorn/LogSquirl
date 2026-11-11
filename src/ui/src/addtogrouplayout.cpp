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

#include "addtogrouplayout.h"

#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFontMetrics>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QPalette>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>
#include <vector>

namespace {

// The most characters of a Log Line measured around the first mark: no
// preview is wider than that many, and a Log Line can be megabytes long.
constexpr qsizetype MeasuredCharacters = 2000;

const QChar Ellipsis( 0x2026 );

} // namespace

LinePreview::LinePreview( QWidget* parent )
    : QLabel( parent )
{
    setTextFormat( Qt::RichText );
    setWordWrap( false );
    setTextInteractionFlags( Qt::NoTextInteraction );
    // As wide as the dialog makes it: the Log Line never widens the dialog.
    setSizePolicy( QSizePolicy::Ignored, QSizePolicy::Fixed );
}

void LinePreview::setLine( const QString& text, const QList<Mark>& marks, MarkStyle style )
{
    line_ = text;
    marks_ = marks;
    style_ = style;
    remark_.clear();
    render();
}

void LinePreview::setRemark( const QString& remark )
{
    line_.clear();
    marks_.clear();
    remark_ = remark;
    render();
}

QSize LinePreview::minimumSizeHint() const
{
    const auto margins = contentsMargins();
    return { 0, fontMetrics().height() + margins.top() + margins.bottom() };
}

QSize LinePreview::sizeHint() const
{
    return { fontMetrics().averageCharWidth() * 40, minimumSizeHint().height() };
}

void LinePreview::resizeEvent( QResizeEvent* event )
{
    QLabel::resizeEvent( event );
    render();
}

void LinePreview::changeEvent( QEvent* event )
{
    QLabel::changeEvent( event );
    if ( event->type() == QEvent::PaletteChange || event->type() == QEvent::FontChange ) {
        render();
    }
}

void LinePreview::render()
{
    markedTexts_.clear();
    if ( !remark_.isEmpty() ) {
        shownText_ = remark_;
        setText( QStringLiteral( "<span style=\"color: %1\">%2</span>" )
                     .arg( palette().color( QPalette::PlaceholderText ).name(),
                           remark_.toHtmlEscaped() ) );
        return;
    }

    auto text = line_;
    for ( auto& character : text ) {
        if ( character == u'\t' || character == u'\n' || character == u'\r' ) {
            character = u' ';
        }
    }
    const auto size = text.size();

    // The part shown, [first, last), grows from the first mark both ways for
    // as long as it fits, an ellipsis standing for each side cut.
    const auto focusStart
        = marks_.isEmpty() ? 0 : std::clamp( marks_.front().start, qsizetype{ 0 }, size );
    const auto focusEnd
        = marks_.isEmpty()
              ? 0
              : std::clamp( marks_.front().start + marks_.front().length, focusStart, size );
    const auto lowest = std::max<qsizetype>( 0, focusStart - MeasuredCharacters / 2 );
    const auto highest = std::min( size, std::max( focusEnd, lowest + MeasuredCharacters ) );

    const QFontMetrics metrics = fontMetrics();
    const auto available = contentsRect().width();
    const auto ellipsisWidth = metrics.horizontalAdvance( Ellipsis );
    std::vector<int> widths( static_cast<size_t>( highest - lowest ) );
    for ( auto index = lowest; index < highest; ++index ) {
        widths[ static_cast<size_t>( index - lowest ) ]
            = metrics.horizontalAdvance( text[ index ] );
    }
    const auto widthOf = [ &widths, lowest ]( qsizetype index ) {
        return widths[ static_cast<size_t>( index - lowest ) ];
    };
    const auto fits = [ & ]( int used, qsizetype first, qsizetype last ) {
        return used + ( first > 0 ? ellipsisWidth : 0 ) + ( last < size ? ellipsisWidth : 0 )
               <= available;
    };

    auto first = focusStart;
    auto last = focusStart;
    int used = 0;
    // The mark itself, as much of it as fits.
    while ( last < focusEnd && last < highest && fits( used + widthOf( last ), first, last + 1 ) ) {
        used += widthOf( last );
        ++last;
    }
    if ( last == focusEnd ) {
        bool grew = true;
        while ( grew ) {
            grew = false;
            if ( last < highest && fits( used + widthOf( last ), first, last + 1 ) ) {
                used += widthOf( last );
                ++last;
                grew = true;
            }
            if ( first > lowest && fits( used + widthOf( first - 1 ), first - 1, last ) ) {
                --first;
                used += widthOf( first );
                grew = true;
            }
        }
    }
    // The characters measured one by one are not quite the text measured
    // whole (kerning, rounding): what is still too wide loses a character
    // of what lies around the mark, the side farther from it first.
    const auto plainOf = [ &text, size ]( qsizetype from, qsizetype to ) {
        return ( from > 0 ? QString( Ellipsis ) : QString{} ) + text.mid( from, to - from )
               + ( to < size ? QString( Ellipsis ) : QString{} );
    };
    while ( metrics.horizontalAdvance( plainOf( first, last ) ) > available
            && ( first < focusStart || last > focusEnd ) ) {
        if ( last > focusEnd && ( last - focusEnd >= focusStart - first ) ) {
            --last;
        }
        else {
            ++first;
        }
    }

    QString markOpen;
    QString markClose;
    if ( style_ == MarkStyle::Underline ) {
        markOpen = QStringLiteral( "<u>" );
        markClose = QStringLiteral( "</u>" );
    }
    else {
        markOpen = QStringLiteral( "<span style=\"background-color: %1; color: %2\">" )
                       .arg( palette().color( QPalette::Highlight ).name(),
                             palette().color( QPalette::HighlightedText ).name() );
        markClose = QStringLiteral( "</span>" );
    }

    shownText_.clear();
    QString html = QStringLiteral( "<span style=\"white-space: pre\">" );
    if ( first > 0 ) {
        shownText_ += Ellipsis;
        html += Ellipsis;
    }
    auto position = first;
    for ( const auto& mark : marks_ ) {
        const auto markStart = std::max( mark.start, position );
        const auto markEnd = std::min( mark.start + mark.length, last );
        if ( markStart >= markEnd ) {
            continue;
        }
        const auto before = text.mid( position, markStart - position );
        const auto marked = text.mid( markStart, markEnd - markStart );
        shownText_ += before + marked;
        markedTexts_.append( marked );
        html += before.toHtmlEscaped() + markOpen + marked.toHtmlEscaped() + markClose;
        position = markEnd;
    }
    const auto rest = text.mid( position, last - position );
    shownText_ += rest;
    html += rest.toHtmlEscaped();
    if ( last < size ) {
        shownText_ += Ellipsis;
        html += Ellipsis;
    }
    html += QStringLiteral( "</span>" );
    setText( html );
}

AddToGroupLayout::AddToGroupLayout( QDialog* dialog )
    : dialog_( dialog )
{
    form_ = new QFormLayout;
    form_->setFieldGrowthPolicy( QFormLayout::AllNonFixedFieldsGrow );

    preview_ = new LinePreview( dialog );
    preview_->setObjectName( QStringLiteral( "preview" ) );
    previewNote_ = new QLabel( dialog );
    previewNote_->setObjectName( QStringLiteral( "previewNote" ) );
    previewNote_->hide();
    auto* previewRow = new QHBoxLayout;
    previewRow->addWidget( preview_, 1 );
    previewRow->addWidget( previewNote_ );
    form_->addRow( tr( "Preview:" ), previewRow );
    rowSections_.append( Section::Preview );

    warning_ = new QLabel( dialog );
    warning_->setObjectName( QStringLiteral( "warning" ) );
    warning_->setWordWrap( true );
    warning_->hide();

    buttons_ = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog );
    QObject::connect( buttons_, &QDialogButtonBox::accepted, dialog, &QDialog::accept );
    QObject::connect( buttons_, &QDialogButtonBox::rejected, dialog, &QDialog::reject );

    auto* layout = new QVBoxLayout( dialog );
    layout->addLayout( form_ );
    layout->addWidget( warning_ );
    layout->addStretch( 1 );
    // The buttons apart from the preview, the warning line hidden.
    layout->addSpacing( dialog->style()->pixelMetric( QStyle::PM_LayoutVerticalSpacing ) );
    layout->addWidget( buttons_ );
}

int AddToGroupLayout::endOf( Section section ) const
{
    return static_cast<int>(
        std::count_if( rowSections_.cbegin(), rowSections_.cend(),
                       [ section ]( Section other ) { return other <= section; } ) );
}

void AddToGroupLayout::inserted( Section section, int row )
{
    rowSections_.insert( row, section );
}

void AddToGroupLayout::addRow( Section section, const QString& label, QWidget* field )
{
    const auto row = endOf( section );
    form_->insertRow( row, label, field );
    inserted( section, row );
}

void AddToGroupLayout::addRow( Section section, const QString& label, QLayout* field )
{
    const auto row = endOf( section );
    form_->insertRow( row, label, field );
    inserted( section, row );
}

void AddToGroupLayout::setWarning( const QString& warning )
{
    warning_->setText( warning );
    warning_->setVisible( !warning.isEmpty() );
}

void AddToGroupLayout::fitDialog()
{
    dialog_->resize( DialogWidth, dialog_->sizeHint().height() );
}
