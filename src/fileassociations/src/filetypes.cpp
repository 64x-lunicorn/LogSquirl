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

#include "filetypes.h"

#include <QCoreApplication>

#include <algorithm>

namespace {

struct FileTypeList {
    std::vector<FileType> choices;
    std::vector<FileType> openWithOnly;
};

FileType makeFileType( const char* id, FileType::Group group, bool checkedByDefault,
                       const char* label, const char* shownAs, const char* name,
                       const char* extensions, const char* mimeType, const char* uti,
                       const char* progId )
{
    FileType type;
    type.id = QString::fromUtf8( id );
    type.group = group;
    type.checkedByDefault = checkedByDefault;
    type.label = QString::fromUtf8( label );
    type.shownAs = QString::fromUtf8( shownAs );
    type.name = QString::fromUtf8( name );
    type.extensions
        = QString::fromUtf8( extensions ).split( QLatin1Char( ' ' ), Qt::SkipEmptyParts );
    type.mimeType = QString::fromUtf8( mimeType );
    type.uti = QString::fromUtf8( uti );
    type.progId = QString::fromUtf8( progId );
    return type;
}

const FileTypeList& fileTypeList()
{
    static const FileTypeList list = [] {
        FileTypeList types;
        const auto add = [ &types ]( FileType type ) {
            auto& into
                = type.group == FileType::Group::OpenWith ? types.openWithOnly : types.choices;
            into.push_back( std::move( type ) );
        };
        constexpr auto Logs = FileType::Group::Logs;
        constexpr auto Optional = FileType::Group::Optional;
        constexpr auto OpenWith = FileType::Group::OpenWith;
#define LOGSQUIRL_FILE_TYPE( id, group, checked, label, shownAs, name, extensions, mimeType, uti,  \
                             progId )                                                              \
    add( makeFileType( id, group, checked, label, shownAs, name, extensions, mimeType, uti,        \
                       progId ) );
// Generated from cmake/FileTypes.cmake.
#include "file_types.inc"
#undef LOGSQUIRL_FILE_TYPE
        Q_UNUSED( Logs );
        Q_UNUSED( Optional );
        Q_UNUSED( OpenWith );
        return types;
    }();
    return list;
}

} // namespace

namespace FileTypes {

const std::vector<FileType>& choices()
{
    return fileTypeList().choices;
}

const std::vector<FileType>& openWithOnly()
{
    return fileTypeList().openWithOnly;
}

const FileType* find( const QString& id )
{
    for ( const auto* types : { &choices(), &openWithOnly() } ) {
        const auto it = std::find_if( types->begin(), types->end(),
                                      [ &id ]( const FileType& type ) { return type.id == id; } );
        if ( it != types->end() ) {
            return &*it;
        }
    }
    return nullptr;
}

QString groupTitle( FileType::Group group )
{
    switch ( group ) {
    case FileType::Group::Logs:
        return QCoreApplication::translate( "FileTypes", "Log files" );
    case FileType::Group::Optional:
        return QCoreApplication::translate( "FileTypes", "More (optional)" );
    case FileType::Group::OpenWith:
        return QCoreApplication::translate( "FileTypes", "Open with" );
    }
    return {};
}

QString label( const FileType& type )
{
    // The labels come from cmake/FileTypes.cmake, so a translation has them
    // under this context by their English text, from translatableLabels().
    return QCoreApplication::translate( "FileTypes", type.label.toUtf8().constData() );
}

QString shownAs( const std::vector<FileType>& types )
{
    QStringList shown;
    for ( const auto& type : types ) {
        shown << type.shownAs;
    }
    return shown.join( QStringLiteral( ", " ) );
}

const std::vector<const char*>& translatableLabels()
{
    // The LABEL of every type of cmake/FileTypes.cmake; a test fails when one
    // is missing here.
    static const std::vector<const char*> labels{
        QT_TRANSLATE_NOOP( "FileTypes", "General log files" ),
        QT_TRANSLATE_NOOP( "FileTypes", "Android Logcat traces" ),
        QT_TRANSLATE_NOOP( "FileTypes", "Program output" ),
        QT_TRANSLATE_NOOP( "FileTypes", "Trace files" ),
        QT_TRANSLATE_NOOP( "FileTypes", "Text files" ),
    };
    return labels;
}

} // namespace FileTypes
