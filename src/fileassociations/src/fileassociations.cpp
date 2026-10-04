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

#include "fileassociations.h"

#include <QCoreApplication>

#if defined( Q_OS_UNIX ) && !defined( Q_OS_MACOS )
#include "xdgfileassociations.h"
#endif

FileAssociations::~FileAssociations() = default;

QString FileAssociations::applyNote() const
{
    return {};
}

FileAssociationStates FileAssociations::states() const
{
    FileAssociationStates current;
    for ( const auto& type : FileTypes::choices() ) {
        current[ type.id ] = state( type );
    }
    return current;
}

FileAssociationPlan FileAssociationPlan::of( const FileAssociationStates& states,
                                             const QStringList& checkedIds )
{
    FileAssociationPlan plan;
    for ( const auto& type : FileTypes::choices() ) {
        const auto it = states.find( type.id );
        const auto isDefault = it != states.end() && it->second == FileAssociationState::Default;
        const auto checked = checkedIds.contains( type.id );
        if ( checked && !isDefault ) {
            plan.makeDefault.push_back( type );
        }
        else if ( !checked && isDefault ) {
            plan.release.push_back( type );
        }
    }
    return plan;
}

#if !defined( Q_OS_UNIX ) || defined( Q_OS_MACOS )
namespace {

// A platform whose own implementation is still to come (#721, #722).
class UnavailableFileAssociations : public FileAssociations {
public:
    bool isAvailable() const override
    {
        return false;
    }

    QString unavailableReason() const override
    {
        return QCoreApplication::translate(
            "FileAssociations",
            "LogSquirl cannot choose the file types it opens on this system yet." );
    }

    FileAssociationState state( const FileType& ) const override
    {
        return FileAssociationState::NotRegistered;
    }

    FileAssociationResult apply( const std::vector<FileType>& makeDefault,
                                 const std::vector<FileType>& release ) override
    {
        FileAssociationResult result;
        for ( const auto* types : { &makeDefault, &release } ) {
            for ( const auto& type : *types ) {
                result.failed << type.id;
            }
        }
        result.error = unavailableReason();
        return result;
    }
};

} // namespace
#endif

std::unique_ptr<FileAssociations> createFileAssociations()
{
#if defined( Q_OS_UNIX ) && !defined( Q_OS_MACOS )
    return std::make_unique<XdgFileAssociations>( XdgFileAssociations::Environment::ofThisRun() );
#else
    return std::make_unique<UnavailableFileAssociations>();
#endif
}
