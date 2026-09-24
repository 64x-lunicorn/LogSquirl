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

#include "groupimportprompt.h"

#include <QCheckBox>
#include <QCoreApplication>
#include <QMessageBox>
#include <QPushButton>

namespace logsquirl::groupexchange {

namespace {

struct Prompt {
    Q_DECLARE_TR_FUNCTIONS( GroupImportPrompt )
};

} // namespace

ConflictResolver askUser( QWidget* parent, const QString& title )
{
    return [ parent, title ]( const ConflictQuestion& question ) {
        QMessageBox box( QMessageBox::Question, title, QString(), QMessageBox::NoButton, parent );
        if ( question.kind == ConflictKind::SameId ) {
            box.setText( Prompt::tr( "The group \"%1\" is already in the list (the imported one "
                                     "is called \"%2\")." )
                             .arg( question.existingName, question.importedName ) );
        }
        else {
            box.setText( Prompt::tr( "A group named \"%1\" is already in the list." )
                             .arg( question.existingName ) );
        }
        box.setInformativeText(
            Prompt::tr( "Replace it, keep both, or skip the imported group?" ) );

        QPushButton* replace = nullptr;
        if ( question.replaceAllowed ) {
            replace = box.addButton( Prompt::tr( "Replace" ), QMessageBox::AcceptRole );
        }
        auto* keepBoth = box.addButton( Prompt::tr( "Keep both" ), QMessageBox::AcceptRole );
        auto* skip = box.addButton( Prompt::tr( "Skip" ), QMessageBox::RejectRole );

        const bool replaceFirst = question.preselected == ConflictAnswer::Replace && replace;
        box.setDefaultButton( replaceFirst ? replace : keepBoth );
        box.setEscapeButton( skip );

        auto* applyToAll = new QCheckBox( Prompt::tr( "Apply to all remaining conflicts" ), &box );
        box.setCheckBox( applyToAll );

        box.exec();

        ConflictDecision decision;
        if ( replace && box.clickedButton() == replace ) {
            decision.answer = ConflictAnswer::Replace;
        }
        else if ( box.clickedButton() == keepBoth ) {
            decision.answer = ConflictAnswer::KeepBoth;
        }
        else {
            decision.answer = ConflictAnswer::Skip;
        }
        decision.applyToAll = applyToAll->isChecked();
        return decision;
    };
}

void reportImportError( QWidget* parent, const QString& title, const QString& file,
                        const ImportResult& result )
{
    switch ( result.error ) {
    case ReadError::None:
        return;
    case ReadError::Unreadable:
        QMessageBox::warning( parent, title,
                              Prompt::tr( "The file %1 could not be read." ).arg( file ) );
        return;
    case ReadError::NoGroups:
        QMessageBox::warning( parent, title,
                              Prompt::tr( "The file %1 holds no group." ).arg( file ) );
        return;
    }
}

} // namespace logsquirl::groupexchange
