/*
 * Copyright (C) 2026 LogSquirl contributors
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

#ifndef LOGSQUIRL_TEXTENCODING_H
#define LOGSQUIRL_TEXTENCODING_H

#include <QByteArray>
#include <QByteArrayView>
#include <QString>
#include <QStringConverter>
#include <QStringEncoder>

#include <memory>
#include <optional>
#include <utility>

// A text Encoding a Log File can be read with (the Qt 6 replacement for the
// Qt 5 codec class the engine used to pass around).
//
// Instances are interned: every lookup returns a pointer to one immutable
// object that lives for the whole process, so Encodings compare by pointer,
// can be handed across threads and stored in Index jobs without ownership.
// A null pointer means "no Encoding chosen" or "unknown name", as it did
// with that class.
//
// The IANA MIB enum is what the settings store, so it stays the way to name
// an Encoding.
class TextEncoding {
public:
    static constexpr int Utf8Mib = 106;
    static constexpr int Utf16BEMib = 1013;
    static constexpr int Utf16LEMib = 1014;
    static constexpr int UsAsciiMib = 3;
    static constexpr int Latin1Mib = 4;

    // Case-insensitive; accepts the canonical name and the aliases uchardet
    // and older Index caches use. nullptr if unknown or if this Qt cannot
    // decode it.
    static const TextEncoding* forName( QByteArrayView name );
    static const TextEncoding* forMib( int mib );
    // The Encoding the system uses for text without a declared one.
    static const TextEncoding* forLocale();
    // Looks for a byte order mark; `fallback` (or the locale Encoding) if
    // there is none.
    static const TextEncoding* forUtfText( QByteArrayView data,
                                           const TextEncoding* fallback = nullptr );

    const QByteArray& name() const
    {
        return name_;
    }
    int mibEnum() const
    {
        return mib_;
    }

    // Stateful: keeps a partial multi-byte sequence between calls. By default
    // a byte order mark at the start of the stream is dropped; pass
    // QStringConverter::Flag::ConvertInitialBom to keep it as U+FEFF.
    std::unique_ptr<QStringDecoder> makeDecoder( QStringConverter::Flags flags
                                                 = QStringConverter::Flag::Default ) const;
    // One-shot, no byte order mark written.
    QByteArray fromUnicode( QStringView text ) const;
    QString toUnicode( QByteArrayView bytes ) const;
    bool canEncode( QStringView text ) const;

    // Only the look-ups above hand out Encodings a program should use. This
    // is public for them and for a test that needs one nothing can decode:
    // asked to convert, it throws std::runtime_error.
    // `iconvIndex` is what the macOS converters of iconvconverter.h are looked
    // up with, for an Encoding Qt does not convert itself; -1 for the rest.
    TextEncoding( int mib, QByteArray name, std::optional<QStringConverter::Encoding> builtin,
                  int iconvIndex = -1 )
        : mib_( mib )
        , name_( std::move( name ) )
        , builtin_( builtin )
        , iconvIndex_( iconvIndex )
    {
    }

private:
    QStringEncoder makeEncoder() const;

    int mib_;
    QByteArray name_;
    std::optional<QStringConverter::Encoding> builtin_;
    int iconvIndex_;
};

#endif // LOGSQUIRL_TEXTENCODING_H
