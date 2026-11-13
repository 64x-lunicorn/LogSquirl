/*
 * Copyright (C) 2019 Anton Filimonov and other contributors
 *
 * This file is part of logsquirl.
 *
 * logsquirl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * logsquirl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with logsquirl.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "scratchpad.h"

#include <memory>
#include <optional>

#include <QAction>
#include <QApplication>
#include <QByteArray>
#include <QClipboard>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDomDocument>
#include <QFormLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QTimeZone>
#include <QToolBar>
#include <QToolTip>
#include <QUrl>
#include <QVBoxLayout>

#include "clipboard.h"
#include "crc32.h"
#include "jwtdecoder.h"

namespace logsquirl {

class DateTimeBox : public QFormLayout {
    // QFormLayout's own tr() would look the labels up in its context, not in
    // the one lupdate puts them in.
    Q_DECLARE_TR_FUNCTIONS( logsquirl::DateTimeBox )

public:
    DateTimeBox();
    ~DateTimeBox() = default;

    QString displayTime( const QString& text );

private:
    QString displayTime();

private:
    std::optional<qint64> timestamp_;
    QLineEdit* timeLine_;
    QComboBox* tzComboBox_;
};
} // namespace logsquirl

namespace {

template <typename T>
QString formatHex( T value )
{
    return QString::fromLatin1( QByteArray::number( value, 16 ).rightJustified( 8, '0', false ) );
}

template <typename T>
QString formatDec( T value )
{
    return QString::fromLatin1( QByteArray::number( value, 10 ) );
}

constexpr int MessageTimeout = 2000;

// Where the splitter between text and fields stands, shared by every
// Scratchpad while LogSquirl runs (#925).
QByteArray& sharedSplitterState()
{
    static QByteArray state;
    return state;
}

constexpr qint64 FileTimeTicks = 10000000;
constexpr qint64 SecondsToEpoch = 11644473600LL;

qint64 windowsTickToUnixSeconds( qint64 windowsTicks )
{
    return ( windowsTicks / FileTimeTicks - SecondsToEpoch );
}

} // namespace

ScratchPad::ScratchPad( QWidget* parent )
    : QWidget( parent )
{
    this->hide();
    auto textEdit = std::make_unique<QPlainTextEdit>();
    textEdit->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );
    // No minimum width: a fixed one held the whole sidebar at almost half the
    // window (#261).
    textEdit->setMinimumHeight( 300 );
    textEdit->setUndoRedoEnabled( true );

    auto toolBar = std::make_unique<QToolBar>();
    toolBar_ = toolBar.get();

    addTransformation( tr( "From base64" ), &ScratchPad::decodeBase64 );
    addTransformation( tr( "To base64" ), &ScratchPad::encodeBase64 );
    addTransformation( tr( "From hex" ), &ScratchPad::decodeHex );
    addTransformation( tr( "To hex" ), &ScratchPad::encodeHex );
    addTransformation( tr( "Decode url" ), &ScratchPad::decodeUrl );
    toolBar_->addSeparator();
    addTransformation( tr( "Decode JWT" ), &ScratchPad::decodeJwt );
    toolBar_->addSeparator();
    addTransformation( tr( "Format json" ), &ScratchPad::formatJson );
    addTransformation( tr( "Format xml" ), &ScratchPad::formatXml );

    toolBar->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Fixed );

    auto transLayout = std::make_unique<QFormLayout>();

    auto addBoxToLayout
        = [ &transLayout, this ]( const QString& label, QLineEdit** widget, auto changeFunction ) {
              auto box = std::make_unique<QLineEdit>();
              box->setReadOnly( true );
              *widget = box.get();
              transLayout->addRow( label, box.release() );

              connect( this, &ScratchPad::updateTransformation, this, changeFunction );
          };

    addBoxToLayout( tr( "CRC32 hex" ), &crc32HexBox_, &ScratchPad::crc32Hex );
    addBoxToLayout( tr( "CRC32 dec" ), &crc32DecBox_, &ScratchPad::crc32Dec );
    addBoxToLayout( tr( "File time" ), &fileTimeBox_, &ScratchPad::fileTime );
    addBoxToLayout( tr( "Dec->Hex" ), &decToHexBox_, &ScratchPad::decToHex );
    addBoxToLayout( tr( "Hex->Dec" ), &hexToDecBox_, &ScratchPad::hexToDec );
    timeBox_ = new logsquirl::DateTimeBox();
    transLayout->addRow( timeBox_ );
    connect( this, &ScratchPad::updateTransformation, [ this ]() {
        transformText( [ this ]( QString text ) { return timeBox_->displayTime( text ); } );
    } );

    auto fields = std::make_unique<QWidget>();
    transLayout->setContentsMargins( 0, 0, 0, 0 );
    fields->setLayout( transLayout.release() );

    // The text and the fields share the width by a splitter (#925). Neither
    // collapses: the fields keep the width their rows need, and the text the
    // larger share until the user moves the splitter. Where it stands is shared
    // by all Scratchpads while LogSquirl runs.
    auto splitter = std::make_unique<QSplitter>( Qt::Horizontal );
    splitter_ = splitter.get();
    splitter_->setChildrenCollapsible( false );
    splitter_->addWidget( textEdit.get() );
    splitter_->addWidget( fields.release() );
    splitter_->setCollapsible( 0, false );
    splitter_->setCollapsible( 1, false );
    splitter_->setStretchFactor( 0, 3 );
    splitter_->setStretchFactor( 1, 2 );
    splitter_->setSizes( { 300, 200 } );
    if ( !sharedSplitterState().isEmpty() ) {
        splitter_->restoreState( sharedSplitterState() );
    }
    connect( splitter_, &QSplitter::splitterMoved, this,
             [ this ] { sharedSplitterState() = splitter_->saveState(); } );

    // A horizontal splitter does not ask for extra height of its own, so the
    // layout would share a tall sidebar's height with the bar of
    // transformations; the text and the fields take all of it (#974).
    splitter_->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );

    auto vLayout = std::make_unique<QVBoxLayout>();
    vLayout->addWidget( toolBar.release() );
    vLayout->addWidget( splitter.release(), 1 );

    textEdit_ = textEdit.release();

    this->setLayout( vLayout.release() );

    connect( textEdit_, &QPlainTextEdit::textChanged, this, &ScratchPad::updateTransformation );
    connect( textEdit_, &QPlainTextEdit::selectionChanged, this,
             &ScratchPad::updateTransformation );
}

void ScratchPad::addTransformation( const QString& text, void ( ScratchPad::*transformation )() )
{
    auto* action = toolBar_->addAction( text );
    connect( action, &QAction::triggered, this, [ this, action, transformation ] {
        runningTransformation_ = action;
        ( this->*transformation )();
        runningTransformation_ = nullptr;
    } );
}

void ScratchPad::showMessage( const QString& message )
{
    QWidget* anchor = toolBar_;
    if ( runningTransformation_ != nullptr ) {
        if ( auto* button = toolBar_->widgetForAction( runningTransformation_ ) ) {
            anchor = button;
        }
    }

    QToolTip::showText( anchor->mapToGlobal( QPoint( 0, anchor->height() ) ), message, anchor, {},
                        MessageTimeout );
}

void ScratchPad::addData( QString newData )
{
    if ( newData.isEmpty() ) {
        return;
    }

    textEdit_->appendPlainText( newData );
}

void ScratchPad::replaceData( QString newData )
{
    if ( newData.isEmpty() ) {
        return;
    }

    textEdit_->setPlainText( newData );
}

QString ScratchPad::transformText( const std::function<QString( QString )>& transform )
{
    auto cursor = textEdit_->textCursor();
    auto text = cursor.selectedText();
    if ( text.isEmpty() ) {
        cursor.select( QTextCursor::Document );
        text = cursor.selectedText();
    }

    return transform( text );
}

void ScratchPad::transformTextInPlace( const std::function<QString( QString )>& transform )
{
    auto cursor = textEdit_->textCursor();
    auto text = cursor.selectedText();
    if ( text.isEmpty() ) {
        cursor.select( QTextCursor::Document );
        text = cursor.selectedText();
    }

    auto transformedText = transform( text );

    if ( !transformedText.isEmpty() ) {
        cursor.insertText( transformedText );
        textEdit_->setTextCursor( cursor );

        sendTextToClipboard( transformedText );

        showMessage( tr( "Copied to clipboard" ) );
    }
    else {
        showMessage( tr( "Empty transformation" ) );
    }
}

void ScratchPad::decodeUrl()
{
    transformTextInPlace(
        []( QString text ) { return QUrl::fromPercentEncoding( text.toUtf8() ); } );
}

void ScratchPad::decodeBase64()
{
    transformTextInPlace( []( QString text ) {
        auto decoded = QByteArray::fromBase64( text.toUtf8() );
        return QString::fromStdString( { decoded.begin(), decoded.end() } );
    } );
}

void ScratchPad::encodeBase64()
{
    transformTextInPlace( []( QString text ) {
        auto encoded = text.toUtf8().toBase64();
        return QString::fromLatin1( encoded );
    } );
}

void ScratchPad::decodeHex()
{
    transformTextInPlace( []( QString text ) {
        auto decoded = QByteArray::fromHex( text.toUtf8() );
        return QString::fromStdString( { decoded.begin(), decoded.end() } );
    } );
}

void ScratchPad::encodeHex()
{
    transformTextInPlace( []( QString text ) {
        auto encoded = text.toUtf8().toHex();
        return QString::fromLatin1( encoded );
    } );
}

void ScratchPad::crc32Hex()
{
    crc32HexBox_->setText( transformText( []( QString text ) {
        const auto decoded = Crc32::calculate( text.toUtf8() );
        return formatHex( decoded ).prepend( "0x" );
    } ) );
}

void ScratchPad::crc32Dec()
{
    crc32DecBox_->setText( transformText( []( QString text ) {
        const auto decoded = Crc32::calculate( text.toUtf8() );
        return formatDec( decoded );
    } ) );
}

void ScratchPad::fileTime()
{
    fileTimeBox_->setText( transformText( []( QString text ) {
        bool isOk = false;
        const auto time = text.toUtf8().toLongLong( &isOk );
        if ( isOk ) {
            QDateTime dateTime;
            dateTime.setTimeZone( QTimeZone::UTC );
            dateTime.setSecsSinceEpoch( windowsTickToUnixSeconds( time ) );
            return dateTime.toString( Qt::ISODate );
        }
        else {
            return QString{};
        }
    } ) );
}

void ScratchPad::decToHex()
{
    decToHexBox_->setText( transformText( []( QString text ) {
        bool isOk = false;
        const auto value = text.toUtf8().toLongLong( &isOk );
        if ( isOk ) {
            return formatHex( value );
        }
        else {
            return QString{};
        }
    } ) );
}

void ScratchPad::hexToDec()
{
    hexToDecBox_->setText( transformText( []( QString text ) {
        bool isOk = false;
        const auto value = text.toUtf8().toLongLong( &isOk, 16 );
        if ( isOk ) {
            return formatDec( value );
        }
        else {
            return QString{};
        }
    } ) );
}

void ScratchPad::formatJson()
{
    transformTextInPlace( []( QString text ) {
        // The first opening brace or bracket, whichever there is: indexOf is
        // -1 for the one that is missing, which must not win the minimum.
        const auto brace = text.indexOf( '{' );
        const auto bracket = text.indexOf( '[' );
        const auto start = brace < 0 ? std::max( bracket, qsizetype{ 0 } )
                                     : ( bracket < 0 ? brace : std::min( brace, bracket ) );

        QJsonParseError parseError;
        auto json = QJsonDocument::fromJson( text.mid( start ).toUtf8(), &parseError );
        if ( json.isNull() ) {
            json = QJsonDocument::fromJson( text.mid( start, parseError.offset ).toUtf8(),
                                            &parseError );
        }

        return json.toJson( QJsonDocument::Indented );
    } );
}

void ScratchPad::formatXml()
{
    transformTextInPlace( []( QString text ) {
        const auto start = text.indexOf( '<' );

        // XML that does not parse is not formatted: the partial document Qt
        // keeps drops what it could not read, and the text would be replaced.
        QDomDocument xml;
        if ( !xml.setContent( text.mid( start ).toUtf8() ) ) {
            return QString{};
        }

        return xml.toString( 2 );
    } );
}

void ScratchPad::decodeJwt()
{
    transformTextInPlace( []( QString text ) { return logsquirl::jwt::decodeToken( text ); } );
}

logsquirl::DateTimeBox::DateTimeBox()
    : QFormLayout()
    , timestamp_()
    , timeLine_( new QLineEdit() )
    , tzComboBox_( new QComboBox() )
{
    addRow( tr( "Time" ), timeLine_ );
    timeLine_->setReadOnly( true );

    addRow( tr( "TimeZone" ), tzComboBox_ );
    connect( tzComboBox_, &QComboBox::currentTextChanged, [ this ] { displayTime(); } );
    auto ids = QTimeZone::availableTimeZoneIds();
    std::for_each( ids.begin(), ids.end(), [ & ]( const auto& item ) {
        if ( item.contains( "UTC" ) ) {
            tzComboBox_->addItem( item );
        }
    } );
}

QString logsquirl::DateTimeBox::displayTime( const QString& text )
{
    bool isOk = false;
    const auto unixTime = text.toUtf8().toLongLong( &isOk );
    if ( !isOk ) {
        timestamp_.reset();
        timeLine_->setText( QString{} );
        return QString{};
    }
    timestamp_ = unixTime;
    return displayTime();
}

QString logsquirl::DateTimeBox::displayTime()
{
    if ( !timestamp_ ) {
        return QString{};
    }

    // Convert to a date string for the selected time zone
    auto tz = QTimeZone( tzComboBox_->currentData( Qt::DisplayRole ).toByteArray() );
    auto dateTime = QDateTime::fromSecsSinceEpoch( timestamp_.value() ).toTimeZone( tz );
    timeLine_->setText( dateTime.toString( Qt::ISODate ) );
    timeLine_->setCursorPosition( 0 );

    return timeLine_->text();
}
