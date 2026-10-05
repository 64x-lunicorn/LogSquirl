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

// The registry and the shell of Windows (#722), through the Win32 API. Only
// this file talks to the system; WindowsFileAssociations decides what to ask.

#include "windowsfileassociations.h"

#include <QUrl>

#include <string>
#include <vector>

#include "log.h"

// After everything else: windows.h defines macros other headers do not expect.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <shellapi.h>
#include <shlobj.h>

namespace {

std::wstring wide( const QString& text )
{
    return text.toStdWString();
}

HKEY rootOf( WindowsFileAssociations::Hive hive )
{
    return hive == WindowsFileAssociations::Hive::CurrentUser ? HKEY_CURRENT_USER
                                                              : HKEY_LOCAL_MACHINE;
}

// An open key, closed when it goes.
class Key {
public:
    Key( WindowsFileAssociations::Hive hive, const QString& path )
    {
        const auto name = wide( path );
        if ( RegOpenKeyExW( rootOf( hive ), name.c_str(), 0, KEY_READ, &key_ ) != ERROR_SUCCESS ) {
            key_ = nullptr;
        }
    }

    ~Key()
    {
        if ( key_ != nullptr ) {
            RegCloseKey( key_ );
        }
    }

    Key( const Key& ) = delete;
    Key& operator=( const Key& ) = delete;

    HKEY handle() const
    {
        return key_;
    }

private:
    HKEY key_ = nullptr;
};

// The number of values or of subkeys of a key, and the longest name of them.
struct KeyInfo {
    DWORD subkeys = 0;
    DWORD longestSubkey = 0;
    DWORD values = 0;
    DWORD longestValueName = 0;
};

KeyInfo infoOf( HKEY key )
{
    KeyInfo info;
    if ( RegQueryInfoKeyW( key, nullptr, nullptr, nullptr, &info.subkeys, &info.longestSubkey,
                           nullptr, &info.values, &info.longestValueName, nullptr, nullptr,
                           nullptr )
         != ERROR_SUCCESS ) {
        return {};
    }
    return info;
}

class Win32System : public WindowsFileAssociations::System {
public:
    std::optional<QString> value( Hive hive, const QString& key,
                                  const QString& name ) const override
    {
        const Key opened( hive, key );
        if ( opened.handle() == nullptr ) {
            return std::nullopt;
        }
        const auto valueName = wide( name );
        DWORD type = 0;
        DWORD size = 0;
        if ( RegQueryValueExW( opened.handle(), valueName.c_str(), nullptr, &type, nullptr, &size )
             != ERROR_SUCCESS ) {
            return std::nullopt;
        }
        if ( type != REG_SZ && type != REG_EXPAND_SZ ) {
            return QString{};
        }
        std::vector<wchar_t> data( size / sizeof( wchar_t ) + 1, L'\0' );
        DWORD dataSize = static_cast<DWORD>( data.size() * sizeof( wchar_t ) );
        if ( RegQueryValueExW( opened.handle(), valueName.c_str(), nullptr, &type,
                               reinterpret_cast<LPBYTE>( data.data() ), &dataSize )
             != ERROR_SUCCESS ) {
            return std::nullopt;
        }
        // Not always terminated in the registry; the buffer has a spare one.
        return QString::fromWCharArray( data.data() );
    }

    bool hasKey( Hive hive, const QString& key ) const override
    {
        return Key( hive, key ).handle() != nullptr;
    }

    QStringList valueNames( Hive hive, const QString& key ) const override
    {
        QStringList names;
        const Key opened( hive, key );
        if ( opened.handle() == nullptr ) {
            return names;
        }
        const auto info = infoOf( opened.handle() );
        std::vector<wchar_t> name( info.longestValueName + 1, L'\0' );
        for ( DWORD index = 0; index < info.values; ++index ) {
            DWORD length = static_cast<DWORD>( name.size() );
            if ( RegEnumValueW( opened.handle(), index, name.data(), &length, nullptr, nullptr,
                                nullptr, nullptr )
                 == ERROR_SUCCESS ) {
                names << QString::fromWCharArray( name.data(), static_cast<qsizetype>( length ) );
            }
        }
        return names;
    }

    QStringList subkeys( Hive hive, const QString& key ) const override
    {
        QStringList names;
        const Key opened( hive, key );
        if ( opened.handle() == nullptr ) {
            return names;
        }
        const auto info = infoOf( opened.handle() );
        std::vector<wchar_t> name( info.longestSubkey + 1, L'\0' );
        for ( DWORD index = 0; index < info.subkeys; ++index ) {
            DWORD length = static_cast<DWORD>( name.size() );
            if ( RegEnumKeyExW( opened.handle(), index, name.data(), &length, nullptr, nullptr,
                                nullptr, nullptr )
                 == ERROR_SUCCESS ) {
                names << QString::fromWCharArray( name.data(), static_cast<qsizetype>( length ) );
            }
        }
        return names;
    }

    bool setValue( const QString& key, const QString& name, const QString& value ) override
    {
        const auto keyName = wide( key );
        const auto valueName = wide( name );
        const auto data = wide( value );
        const auto size = static_cast<DWORD>( ( data.size() + 1 ) * sizeof( wchar_t ) );
        const auto status = RegSetKeyValueW( HKEY_CURRENT_USER, keyName.c_str(), valueName.c_str(),
                                             REG_SZ, data.c_str(), size );
        if ( status != ERROR_SUCCESS ) {
            LOG_WARNING << "Could not write HKEY_CURRENT_USER\\" << key.toStdString() << " "
                        << name.toStdString() << ": error " << status;
        }
        return status == ERROR_SUCCESS;
    }

    bool removeValue( const QString& key, const QString& name ) override
    {
        const auto keyName = wide( key );
        const auto valueName = wide( name );
        const auto status
            = RegDeleteKeyValueW( HKEY_CURRENT_USER, keyName.c_str(), valueName.c_str() );
        return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
    }

    bool removeKey( const QString& key ) override
    {
        const auto keyName = wide( key );
        // Given a subkey, RegDeleteTreeW deletes it with everything under it.
        const auto status = RegDeleteTreeW( HKEY_CURRENT_USER, keyName.c_str() );
        return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
    }

    void associationsChanged() override
    {
        SHChangeNotify( SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr );
    }

    bool openDefaultApps( const QString& registeredApplication ) override
    {
        // Windows 11 opens the page of the application; Windows 10 ignores the
        // query and opens the Default apps page.
        const auto uri
            = wide( QStringLiteral( "ms-settings:defaultapps?registeredAppUser=" )
                    + QString::fromLatin1( QUrl::toPercentEncoding( registeredApplication ) ) );
        const auto result
            = ShellExecuteW( nullptr, L"open", uri.c_str(), nullptr, nullptr, SW_SHOWNORMAL );
        // ShellExecuteW returns a value above 32 when it succeeded.
        return reinterpret_cast<INT_PTR>( result ) > 32;
    }
};

} // namespace

std::unique_ptr<WindowsFileAssociations::System> WindowsFileAssociations::windowsSystem()
{
    return std::make_unique<Win32System>();
}
