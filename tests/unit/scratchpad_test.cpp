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

// The Scratchpad transforms the text a user pastes into it: encodings, JSON and
// XML formatting, and the number boxes beside the text (#444). The XML cases
// also back the reasoning of the accepted CVE-2026-15037 in
// scripts/sbom/vuln-ignore.yml.

#include <catch2/catch_test_macros.hpp>

#include "scratchpad.h"

#include <QAction>
#include <QDomDocument>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QStatusBar>
#include <QTextCursor>
#include <QToolBar>

namespace {

/// Drives a Scratchpad the way a user does: types text, presses a toolbar
/// button, reads the text edit and the boxes beside it.
class ScratchPadDriver {
public:
    ScratchPadDriver()
        : edit_( pad_.findChild<QPlainTextEdit*>() )
    {
        REQUIRE( edit_ != nullptr );
    }

    void setText( const QString& text )
    {
        edit_->setPlainText( text );
    }

    QString text() const
    {
        return edit_->toPlainText();
    }

    /// Selects the last `count` characters of the text.
    void selectTail( int count )
    {
        auto cursor = edit_->textCursor();
        cursor.movePosition( QTextCursor::End );
        cursor.movePosition( QTextCursor::Left, QTextCursor::KeepAnchor, count );
        edit_->setTextCursor( cursor );
    }

    void press( const QString& button )
    {
        const auto actions = pad_.findChild<QToolBar*>()->actions();
        for ( auto* action : actions ) {
            if ( action->text() == button ) {
                action->trigger();
                return;
            }
        }
        FAIL( "no toolbar button " << button.toStdString() );
    }

    QString status() const
    {
        return pad_.findChild<QStatusBar*>()->currentMessage();
    }

    /// The read-only box beside the text that carries this label.
    QString box( const QString& label ) const
    {
        for ( auto* labelWidget : pad_.findChildren<QLabel*>() ) {
            if ( labelWidget->text() == label ) {
                auto* line = qobject_cast<QLineEdit*>( labelWidget->buddy() );
                REQUIRE( line != nullptr );
                return line->text();
            }
        }
        FAIL( "no box " << label.toStdString() );
        return {};
    }

private:
    ScratchPad pad_;
    QPlainTextEdit* edit_;
};

} // namespace

SCENARIO( "The Scratchpad encodes and decodes the text", "[scratchpad]" )
{
    ScratchPadDriver pad;

    GIVEN( "Some text" )
    {
        pad.setText( "hello" );

        WHEN( "It is encoded to base64" )
        {
            pad.press( "To base64" );

            THEN( "The text is replaced and the user is told it was copied" )
            {
                REQUIRE( pad.text() == "aGVsbG8=" );
                REQUIRE( pad.status() == "Copied to clipboard" );
            }

            AND_WHEN( "It is decoded again" )
            {
                pad.press( "From base64" );

                THEN( "The original comes back" )
                {
                    REQUIRE( pad.text() == "hello" );
                }
            }
        }

        WHEN( "It is encoded to hex" )
        {
            pad.press( "To hex" );

            THEN( "It shows the bytes and decodes back" )
            {
                REQUIRE( pad.text() == "68656c6c6f" );
                pad.press( "From hex" );
                REQUIRE( pad.text() == "hello" );
            }
        }
    }

    GIVEN( "A percent-encoded URL" )
    {
        pad.setText( "a%20b%2Fc%3Fd" );
        pad.press( "Decode url" );

        THEN( "It is decoded" )
        {
            REQUIRE( pad.text() == "a b/c?d" );
        }
    }

    GIVEN( "Text that does not decode to anything" )
    {
        pad.setText( "zz" );
        pad.press( "From hex" );

        THEN( "The text is kept and the user is told the result was empty" )
        {
            REQUIRE( pad.text() == "zz" );
            REQUIRE( pad.status() == "Empty transformation" );
        }
    }

    GIVEN( "Text of which only the end is selected" )
    {
        pad.setText( "keep hello" );
        pad.selectTail( 5 );
        pad.press( "To base64" );

        THEN( "Only the selection is transformed" )
        {
            REQUIRE( pad.text() == "keep aGVsbG8=" );
        }
    }

    GIVEN( "Text with multi-byte characters" )
    {
        pad.setText( QString::fromUtf8( "\xc3\xa4" ) );
        pad.press( "To hex" );

        THEN( "It is encoded as UTF-8" )
        {
            REQUIRE( pad.text() == "c3a4" );
        }
    }
}

SCENARIO( "The Scratchpad decodes a JWT", "[scratchpad]" )
{
    ScratchPadDriver pad;

    GIVEN( "A token with a subject claim" )
    {
        // {"alg":"none"} . {"sub":"scratch"} . (no signature)
        pad.setText( "eyJhbGciOiJub25lIn0.eyJzdWIiOiJzY3JhdGNoIn0." );
        pad.press( "Decode JWT" );

        THEN( "The claims are shown" )
        {
            REQUIRE( pad.text().contains( "scratch" ) );
            REQUIRE( pad.status() == "Copied to clipboard" );
        }
    }

    GIVEN( "Text that is no token" )
    {
        pad.setText( "not a token" );
        pad.press( "Decode JWT" );

        THEN( "The text is not lost" )
        {
            REQUIRE( !pad.text().isEmpty() );
        }
    }
}

SCENARIO( "The Scratchpad formats JSON", "[scratchpad][json]" )
{
    ScratchPadDriver pad;

    GIVEN( "A compact object" )
    {
        pad.setText( R"({"a":1,"b":[true,null]})" );
        pad.press( "Format json" );

        THEN( "It is indented" )
        {
            REQUIRE( pad.text().contains( "\"a\": 1" ) );
            REQUIRE( pad.text().contains( "\n" ) );
        }
    }

    GIVEN( "A log line with an object after a prefix" )
    {
        pad.setText( R"(payload: {"a":1})" );
        pad.press( "Format json" );

        THEN( "The prefix is dropped and the object is formatted" )
        {
            REQUIRE( pad.text().contains( "\"a\": 1" ) );
            REQUIRE( !pad.text().contains( "payload" ) );
        }
    }

    GIVEN( "A log line with an array after a prefix" )
    {
        pad.setText( "items: [1,2]" );
        pad.press( "Format json" );

        THEN( "The prefix is dropped and the array is formatted" )
        {
            REQUIRE( !pad.text().contains( "items" ) );
            REQUIRE( pad.text().contains( "1," ) );
        }
    }

    GIVEN( "An object followed by more text" )
    {
        pad.setText( R"({"a":1} and then some)" );
        pad.press( "Format json" );

        THEN( "The object is formatted up to where the JSON ends" )
        {
            REQUIRE( pad.text().contains( "\"a\": 1" ) );
            REQUIRE( !pad.text().contains( "then" ) );
        }
    }

    GIVEN( "Text without any JSON" )
    {
        pad.setText( "no json here" );
        pad.press( "Format json" );

        THEN( "The text is kept and the user is told the result was empty" )
        {
            REQUIRE( pad.text() == "no json here" );
            REQUIRE( pad.status() == "Empty transformation" );
        }
    }
}

SCENARIO( "The Scratchpad formats XML", "[scratchpad][xml]" )
{
    ScratchPadDriver pad;

    GIVEN( "Compact XML" )
    {
        pad.setText( "<a><b>x</b></a>" );
        pad.press( "Format xml" );

        THEN( "It is indented by two spaces" )
        {
            REQUIRE( pad.text().trimmed() == "<a>\n  <b>x</b>\n</a>" );
        }
    }

    GIVEN( "XML after a log prefix" )
    {
        pad.setText( "response: <a><b/></a>" );
        pad.press( "Format xml" );

        THEN( "The prefix is dropped" )
        {
            REQUIRE( !pad.text().contains( "response" ) );
            REQUIRE( pad.text().contains( "<a>" ) );
        }
    }

    GIVEN( "XML that is not well formed" )
    {
        pad.setText( "<a><b></a>" );
        pad.press( "Format xml" );

        THEN( "The text is kept and the user is told the result was empty" )
        {
            REQUIRE( pad.text() == "<a><b></a>" );
            REQUIRE( pad.status() == "Empty transformation" );
        }
    }
}

// CVE-2026-15037: text placed into QDom comment, CDATA or processing
// instruction nodes can inject markup when serialized (fixed in Qt 6.12).
// scripts/sbom/vuln-ignore.yml accepts the risk because the Scratchpad only
// gives the user's own text back to the user. That holds while formatting keeps
// what was pasted as it was: the same nodes come out that went in, and pasted
// text that looks like markup never becomes markup.
SCENARIO( "Formatting XML never turns pasted text into other markup", "[scratchpad][xml][cve]" )
{
    ScratchPadDriver pad;

    const auto reparse = [ & ]() {
        QDomDocument document;
        const auto result = document.setContent( pad.text().toUtf8() );
        REQUIRE( bool( result ) );
        return document;
    };

    GIVEN( "Comment, CDATA and processing instruction nodes" )
    {
        pad.setText( "<a><!-- note --><![CDATA[<b>&]]><?target some data?></a>" );
        pad.press( "Format xml" );

        THEN( "The same three nodes come out with the same content" )
        {
            const auto root = reparse().documentElement();
            REQUIRE( root.tagName() == "a" );
            REQUIRE( root.childNodes().size() == 3 );
            REQUIRE( root.childNodes().at( 0 ).nodeType() == QDomNode::CommentNode );
            REQUIRE( root.childNodes().at( 0 ).nodeValue() == " note " );
            REQUIRE( root.childNodes().at( 1 ).nodeType() == QDomNode::CDATASectionNode );
            REQUIRE( root.childNodes().at( 1 ).nodeValue() == "<b>&" );
            REQUIRE( root.childNodes().at( 2 ).nodeType() == QDomNode::ProcessingInstructionNode );
            REQUIRE( root.childNodes().at( 2 ).nodeValue() == "some data" );
        }
    }

    GIVEN( "CDATA that contains the end of a comment and an element" )
    {
        pad.setText( "<a><![CDATA[--><b/>]]></a>" );
        pad.press( "Format xml" );

        THEN( "No element b appears" )
        {
            const auto root = reparse().documentElement();
            REQUIRE( root.elementsByTagName( "b" ).isEmpty() );
            REQUIRE( root.text() == "--><b/>" );
        }
    }

    GIVEN( "A comment that contains a CDATA end and an element" )
    {
        pad.setText( "<a><!-- ]]><b/> --></a>" );
        pad.press( "Format xml" );

        THEN( "No element b appears" )
        {
            const auto root = reparse().documentElement();
            REQUIRE( root.elementsByTagName( "b" ).isEmpty() );
            REQUIRE( root.childNodes().at( 0 ).nodeType() == QDomNode::CommentNode );
        }
    }

    GIVEN( "Escaped markup in text and in an attribute" )
    {
        pad.setText( R"(<a k="&quot;&lt;x&gt;">&lt;script&gt;&amp;</a>)" );
        pad.press( "Format xml" );

        THEN( "It stays text" )
        {
            const auto root = reparse().documentElement();
            REQUIRE( root.attribute( "k" ) == "\"<x>" );
            REQUIRE( root.text() == "<script>&" );
            REQUIRE( root.elementsByTagName( "script" ).isEmpty() );
        }
    }

    GIVEN( "A comment with a double hyphen, which XML does not allow" )
    {
        pad.setText( "<a><!-- x -- y --></a>" );
        pad.press( "Format xml" );

        THEN( "Nothing but the pasted text is left in the edit" )
        {
            // Either refused as malformed or kept whole; never rewritten into
            // other nodes.
            const auto text = pad.text();
            REQUIRE( ( text == "<a><!-- x -- y --></a>" || text.contains( "x -- y" ) ) );
        }
    }
}

SCENARIO( "The Scratchpad shows what its text stands for in numbers", "[scratchpad]" )
{
    ScratchPadDriver pad;

    GIVEN( "A decimal number" )
    {
        pad.setText( "255" );

        THEN( "The boxes show it in hex and its checksum" )
        {
            REQUIRE( pad.box( "Dec->Hex" ) == "000000ff" );
            REQUIRE( pad.box( "Hex->Dec" ) == "597" );
            REQUIRE( pad.box( "CRC32 dec" ).toLongLong() > 0 );
            REQUIRE( pad.box( "CRC32 hex" ).startsWith( "0x" ) );
        }
    }

    GIVEN( "A hex number" )
    {
        pad.setText( "ff" );

        THEN( "The box shows it in decimal and the decimal box stays empty" )
        {
            REQUIRE( pad.box( "Hex->Dec" ) == "255" );
            REQUIRE( pad.box( "Dec->Hex" ).isEmpty() );
        }
    }

    GIVEN( "The standard check text of CRC32" )
    {
        pad.setText( "123456789" );

        THEN( "Both checksum boxes show the well-known value" )
        {
            REQUIRE( pad.box( "CRC32 hex" ) == "0xcbf43926" );
            REQUIRE( pad.box( "CRC32 dec" ) == "3421780262" );
        }
    }

    GIVEN( "A Windows file time at the Unix epoch" )
    {
        pad.setText( "116444736000000000" );

        THEN( "The box shows the epoch in UTC" )
        {
            REQUIRE( pad.box( "File time" ) == "1970-01-01T00:00:00Z" );
        }
    }

    GIVEN( "Text that is no number" )
    {
        pad.setText( "not a number" );

        THEN( "The number boxes are empty" )
        {
            REQUIRE( pad.box( "Dec->Hex" ).isEmpty() );
            REQUIRE( pad.box( "Hex->Dec" ).isEmpty() );
            REQUIRE( pad.box( "File time" ).isEmpty() );
        }
    }

    GIVEN( "Text of which only the end is selected" )
    {
        pad.setText( "abc 7" );
        pad.selectTail( 1 );

        THEN( "The boxes follow the selection" )
        {
            REQUIRE( pad.box( "Dec->Hex" ) == "00000007" );
        }
    }
}
