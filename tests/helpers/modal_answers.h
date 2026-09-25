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

#ifndef LOGSQUIRL_TEST_MODAL_ANSWERS_H
#define LOGSQUIRL_TEST_MODAL_ANSWERS_H

// Answers the modal dialogs that a path of the user interface opens (#492).
//
// A modal dialog runs its own event loop inside the call that opened it, so a
// test cannot click it from the line after the action that opened it. A timer
// looks for the modal widget instead, and answers it. This is the one place
// that does that for message boxes and input dialogs; the application has no
// seam for it and needs none.
//
//     ModalAnswers modals;
//     modals.inputText( "12:00:05" ).inputText( "12:00:10" );
//     action->trigger();              // returns when both dialogs are answered
//     REQUIRE( modals.unanswered() == 0 );
//
// Each call queues one answer, used for the next modal widget that opens. A
// message box that opens with no answer queued is answered with a default
// (Ok/Close) and counted as unexpected, so that a test which does not expect
// one can say so; an input dialog with none is cancelled.

#include <QAbstractButton>
#include <QApplication>
#include <QDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QStringList>
#include <QTimer>

#include <deque>
#include <functional>
#include <optional>
#include <utility>

class ModalAnswers {
public:
    explicit ModalAnswers( int intervalMs = 10 )
    {
        // Started again before each look: a step that runs an event loop of its
        // own (a wait, a click that opens another dialog) must still find this
        // timer firing, and a timer does not fire inside its own slot.
        timer_.setSingleShot( true );
        timer_.setInterval( intervalMs );
        QObject::connect( &timer_, &QTimer::timeout, [ this ] {
            timer_.start();
            answerOpenModal();
        } );
        timer_.start();
    }

    // Queues the answer to a message box: the button with this text.
    // beforeAnswer runs while the box is open, before it is answered.
    ModalAnswers& clickButton( const QString& text, std::function<void()> beforeAnswer = {} )
    {
        queue_.push_back( { Kind::MessageBoxText, text, 0, {}, std::move( beforeAnswer ) } );
        return *this;
    }

    // Queues the answer to a message box: one of its standard buttons.
    ModalAnswers& click( QMessageBox::StandardButton button,
                         std::function<void()> beforeAnswer = {} )
    {
        queue_.push_back( { Kind::MessageBoxStandard,
                            {},
                            static_cast<int>( button ),
                            {},
                            std::move( beforeAnswer ) } );
        return *this;
    }

    // Queues the answer to an input dialog asking for text: this text, accepted.
    ModalAnswers& inputText( const QString& text, std::function<void()> beforeAnswer = {} )
    {
        queue_.push_back( { Kind::InputText, text, 0, {}, std::move( beforeAnswer ) } );
        return *this;
    }

    // The same for an input dialog asking for a whole number.
    ModalAnswers& inputInt( int value, std::function<void()> beforeAnswer = {} )
    {
        queue_.push_back( { Kind::InputInt, {}, value, {}, std::move( beforeAnswer ) } );
        return *this;
    }

    // Queues the answer to an input dialog: cancelled.
    ModalAnswers& cancelInput( std::function<void()> beforeAnswer = {} )
    {
        queue_.push_back( { Kind::InputCancel, {}, 0, {}, std::move( beforeAnswer ) } );
        return *this;
    }

    // Queues a step for any modal dialog: it is called with the dialog and
    // answers it however it likes.
    ModalAnswers& answerWith( std::function<void( QDialog& )> answer )
    {
        queue_.push_back( { Kind::Custom, {}, 0, std::move( answer ), {} } );
        return *this;
    }

    // How many queued answers no dialog has taken yet.
    int unanswered() const
    {
        return static_cast<int>( queue_.size() );
    }

    // The window titles of every modal dialog answered so far, in order.
    const QStringList& titles() const
    {
        return titles_;
    }

    // The texts of every message box answered so far, in order.
    const QStringList& messages() const
    {
        return messages_;
    }

    // How many message boxes opened with no answer queued for them.
    int unexpectedMessageBoxes() const
    {
        return unexpected_;
    }

    // Whether any modal dialog was answered.
    bool seen() const
    {
        return !titles_.isEmpty();
    }

private:
    enum class Kind {
        MessageBoxText,
        MessageBoxStandard,
        InputText,
        InputInt,
        InputCancel,
        Custom
    };

    struct Step {
        Kind kind;
        QString text;
        int number;
        std::function<void( QDialog& )> custom;
        std::function<void()> beforeAnswer;
    };

    void answerOpenModal()
    {
        auto* modal = qobject_cast<QDialog*>( QApplication::activeModalWidget() );
        // A step that waits, or clicks something that opens another dialog,
        // runs an event loop, and this timer fires inside it: a dialog is
        // answered once, and a dialog opened by an answer is answered in turn.
        if ( !modal || modal == answering_ ) {
            return;
        }
        auto* const outer = std::exchange( answering_, modal );
        answer( *modal );
        answering_ = outer;
    }

    void answer( QDialog& modal )
    {
        titles_.append( modal.windowTitle() );
        auto* box = qobject_cast<QMessageBox*>( &modal );
        auto* input = qobject_cast<QInputDialog*>( &modal );
        if ( box ) {
            messages_.append( box->text() );
        }

        const auto step = takeStepFor( box, input );
        if ( !step ) {
            if ( box ) {
                ++unexpected_;
                box->done( QMessageBox::Ok );
            }
            else {
                modal.reject();
            }
            return;
        }

        if ( step->beforeAnswer ) {
            step->beforeAnswer();
        }
        switch ( step->kind ) {
        case Kind::MessageBoxText:
            for ( auto* button : box->buttons() ) {
                if ( button->text().remove( '&' ) == step->text ) {
                    button->click();
                    return;
                }
            }
            box->done( QMessageBox::Cancel );
            break;
        case Kind::MessageBoxStandard:
            if ( auto* button
                 = box->button( static_cast<QMessageBox::StandardButton>( step->number ) ) ) {
                button->click();
            }
            else {
                box->done( step->number );
            }
            break;
        case Kind::InputText:
            input->setTextValue( step->text );
            input->accept();
            break;
        case Kind::InputInt:
            input->setIntValue( step->number );
            input->accept();
            break;
        case Kind::InputCancel:
            input->reject();
            break;
        case Kind::Custom:
            step->custom( modal );
            break;
        }
    }

    // The first queued step that fits the dialog that is open.
    std::optional<Step> takeStepFor( const QMessageBox* box, const QInputDialog* input )
    {
        if ( queue_.empty() ) {
            return std::nullopt;
        }
        const auto fits = [ & ]( Kind kind ) {
            switch ( kind ) {
            case Kind::MessageBoxText:
            case Kind::MessageBoxStandard:
                return box != nullptr;
            case Kind::InputText:
            case Kind::InputInt:
            case Kind::InputCancel:
                return input != nullptr;
            case Kind::Custom:
                return true;
            }
            return false;
        };
        if ( !fits( queue_.front().kind ) ) {
            return std::nullopt;
        }
        auto step = std::move( queue_.front() );
        queue_.pop_front();
        return step;
    }

    QTimer timer_;
    std::deque<Step> queue_;
    QStringList titles_;
    QStringList messages_;
    int unexpected_ = 0;
    QDialog* answering_ = nullptr;
};

#endif
