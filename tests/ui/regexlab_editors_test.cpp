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

// The Highlighter and Predefined Filter editors test their pattern in the
// Regex Lab (#660): Test... opens the Lab over the editor with the pattern
// and the options the editor keeps, Apply writes them back the way typing
// them does, and Cancel or closing the Lab changes nothing. For a
// Highlighter the Lab marks what it colors: the whole line, or the matched
// text, in its colors.

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSignalSpy>
#include <QTest>
#include <QTextBlock>
#include <QVBoxLayout>

#include "highlighteredit.h"
#include "highlightersetedit.h"
#include "predefinedfilters.h"
#include "predefinedfiltersetedit.h"
#include "regexlabwindow.h"
#include "regularexpression.h"
#include "searchline.h"
#include "settingspolicies.h"
#include "test_utils.h"

namespace {

const QColor Text( 0x20, 0x30, 0x40 );
const QColor Background( 0xfa, 0xd0, 0x70 );

template <typename Widget>
Widget* part( const QWidget& parent, const char* name )
{
    auto* widget = parent.findChild<Widget*>( QString::fromLatin1( name ) );
    REQUIRE( widget != nullptr );
    return widget;
}

// The Lab Test... opened over the editor, once it has evaluated the pattern.
QPointer<RegexLabWindow> openedLab( const QWidget& editor )
{
    QPointer<RegexLabWindow> lab = editor.window()->findChild<RegexLabWindow*>();
    REQUIRE( !lab.isNull() );
    REQUIRE( waitUiState( [ & ] { return lab->isVisible(); } ) );
    return lab;
}

void typePattern( RegexLabWindow& lab, const QString& pattern )
{
    QSignalSpy evaluated( &lab, &RegexLabWindow::evaluated );
    auto* edit = part<QLineEdit>( lab, "pattern" );
    edit->clear();
    QTest::keyClicks( edit, pattern );
    REQUIRE( waitUiState( [ & ] { return !evaluated.isEmpty(); }, 10'000 ) );
}

// Pastes the sample and waits until the Lab shows what the pattern does on it.
void pasteSample( RegexLabWindow& lab, const QString& text )
{
    lab.setSample( RegexLabWindow::Sample::PastedText );
    part<QPlainTextEdit>( lab, "sampleText" )->setPlainText( text );
    const auto lines = static_cast<std::size_t>( text.count( QChar::LineFeed ) + 1 );
    REQUIRE( waitUiState( [ & ] { return lab.result().lines.size() == lines; }, 10'000 ) );
}

QPushButton* labButton( const RegexLabWindow& lab, QDialogButtonBox::StandardButton which )
{
    return part<QDialogButtonBox>( lab, "buttons" )->button( which );
}

struct Mark {
    QString text;
    QColor background;
    QColor foreground;
};

// What the Lab marks in its sample, in order, with the colors it is marked in.
std::vector<Mark> marks( const RegexLabWindow& lab )
{
    const auto* sample = part<QPlainTextEdit>( lab, "sampleText" );
    std::vector<Mark> found;
    for ( auto block = sample->document()->firstBlock(); block.isValid(); block = block.next() ) {
        for ( const auto& range : block.layout()->formats() ) {
            if ( range.format.boolProperty( QTextFormat::UserProperty + 1 ) ) {
                found.push_back( { block.text().mid( range.start, range.length ),
                                   range.format.background().color(),
                                   range.format.foreground().color() } );
            }
        }
    }
    return found;
}

QStringList markedTexts( const RegexLabWindow& lab )
{
    QStringList texts;
    for ( const auto& mark : marks( lab ) ) {
        texts.append( mark.text );
    }
    return texts;
}

// Closes the editor's dialog and the Lab over it, and lets them go, so that
// no modal window is left for the tests after this one (a window that is to
// be activated there would wait on it).
void leaveNoWindowBehind( QDialog& dialog )
{
    for ( auto* lab : dialog.findChildren<RegexLabWindow*>() ) {
        lab->close();
    }
    dialog.hide();
    QCoreApplication::sendPostedEvents( nullptr, QEvent::DeferredDelete );
    QCoreApplication::processEvents();
}

// A Highlighter Set editor with one Highlighter, in a modal dialog as the
// Highlighters dialog holds it, the Highlighter selected.
struct HighlighterEditor {
    explicit HighlighterEditor( bool onlyMatch,
                                const QString& pattern = QStringLiteral( "user=(\\w+)" ) )
    {
        dialog.setModal( true );
        setEdit = new HighlighterSetEdit( &dialog );
        auto* layout = new QVBoxLayout( &dialog );
        layout->addWidget( setEdit );

        auto set = HighlighterSet::createNewSet( QStringLiteral( "Set" ) );
        set.addHighlighter( Highlighter( pattern, true, onlyMatch, Text, Background ) );
        setEdit->setHighlighters( set );
        dialog.show();

        edit = setEdit->findChild<HighlighterEdit*>();
        REQUIRE( edit != nullptr );
        REQUIRE( waitUiState( [ this ] { return edit->patternEdit->isEnabled(); } ) );
    }

    ~HighlighterEditor()
    {
        leaveNoWindowBehind( dialog );
    }
    HighlighterEditor( const HighlighterEditor& ) = delete;
    HighlighterEditor& operator=( const HighlighterEditor& ) = delete;

    QDialog dialog;
    HighlighterSetEdit* setEdit = nullptr;
    HighlighterEdit* edit = nullptr;
};

// A Predefined Filter group editor with two filters, in a modal dialog as
// the Predefined Filters dialog holds it, the second filter current.
struct FilterEditor {
    explicit FilterEditor( bool readOnly = false, bool searchUsesRegexp = true,
                           bool isRegexp = false )
    {
        dialog.setModal( true );
        edit = new PredefinedFilterSetEdit( &dialog );
        auto* layout = new QVBoxLayout( &dialog );
        layout->addWidget( edit );

        RegexLabAccess access;
        access.searchEngine = RegexpEngine::Vectorscan;
        access.searchMatchesCase = true;
        access.searchUsesRegexp = searchUsesRegexp;
        edit->setRegexLabAccess( access );

        auto group = PredefinedFilterSet::createNewSet( QStringLiteral( "Group" ) );
        group.addFilter( { QStringLiteral( "Users" ), QStringLiteral( "user=" ), false } );
        group.addFilter( { QStringLiteral( "Errors" ), QStringLiteral( "err.r" ), isRegexp } );
        edit->setReadOnly( readOnly );
        edit->setFilterSet( group );
        dialog.show();
    }

    ~FilterEditor()
    {
        leaveNoWindowBehind( dialog );
    }
    FilterEditor( const FilterEditor& ) = delete;
    FilterEditor& operator=( const FilterEditor& ) = delete;

    QDialog dialog;
    PredefinedFilterSetEdit* edit = nullptr;
};

} // namespace

SCENARIO( "Test... opens the Regex Lab with a Highlighter's pattern and options",
          "[ui][regexlab][highlighters]" )
{
    HighlighterEditor editor( true );
    auto* test = part<QPushButton>( *editor.edit, "testPatternButton" );
    REQUIRE( test->isEnabled() );
    test->click();
    const auto lab = openedLab( *editor.edit );

    THEN( "the Lab opens over the editor, which waits for its answer" )
    {
        CHECK( lab->parentWidget()->window() == &editor.dialog );
        CHECK( lab->windowModality() == Qt::WindowModal );
        CHECK( labButton( *lab, QDialogButtonBox::Apply ) != nullptr );
        CHECK( labButton( *lab, QDialogButtonBox::Cancel ) != nullptr );
    }

    THEN( "it holds the pattern read as the Highlighter reads it" )
    {
        const auto pattern = lab->pattern();
        CHECK( pattern.pattern == "user=(\\w+)" );
        CHECK_FALSE( pattern.isCaseSensitive );
        CHECK_FALSE( pattern.isPlainText );
        CHECK_FALSE( pattern.isExclude );
        CHECK_FALSE( pattern.isBoolean );
        // A Highlighter decides with QRegularExpression, whatever a Search
        // runs on.
        CHECK( lab->engine() == RegexpEngine::QRegularExpression );
    }

    THEN( "only the options a Highlighter keeps are offered" )
    {
        CHECK( part<QCheckBox>( *lab, "matchCase" )->isEnabled() );
        CHECK( part<QCheckBox>( *lab, "useRegexp" )->isEnabled() );
        CHECK( part<QCheckBox>( *lab, "inverse" )->isHidden() );
        CHECK( part<QCheckBox>( *lab, "logicalCombination" )->isHidden() );
    }

    THEN( "without a tab to take a sample from, pasted text is the sample" )
    {
        CHECK( lab->sample() == RegexLabWindow::Sample::PastedText );
    }
}

SCENARIO( "The Regex Lab marks what a Highlighter colors, in its colors",
          "[ui][regexlab][highlighters]" )
{
    const auto sample = QStringLiteral( "at 10:00 user=alice id=7 user=bob\nnothing here" );

    GIVEN( "a Highlighter that colors only the matched text" )
    {
        HighlighterEditor editor( true );
        part<QPushButton>( *editor.edit, "testPatternButton" )->click();
        const auto lab = openedLab( *editor.edit );
        pasteSample( *lab, sample );

        THEN( "the text its group takes in each match is marked, in the Highlighter's colors" )
        {
            CHECK( lab->hasLineDecision() );
            const auto found = marks( *lab );
            CHECK( markedTexts( *lab ) == QStringList{ "alice", "bob" } );
            REQUIRE_FALSE( found.empty() );
            CHECK( found.front().background == Background );
            CHECK( found.front().foreground == Text );
        }
    }

    GIVEN( "a Highlighter that colors the whole Log Line" )
    {
        HighlighterEditor editor( false );
        part<QPushButton>( *editor.edit, "testPatternButton" )->click();
        const auto lab = openedLab( *editor.edit );
        pasteSample( *lab, sample );

        THEN( "each matching line is marked whole, in the Highlighter's colors" )
        {
            CHECK( lab->hasLineDecision() );
            CHECK( markedTexts( *lab ) == QStringList{ "at 10:00 user=alice id=7 user=bob" } );
            CHECK( marks( *lab ).front().background == Background );
        }
    }
}

SCENARIO( "The Regex Lab leaves a line alone that the Highlighter leaves alone",
          "[ui][regexlab][highlighters]" )
{
    // The only group takes no part in the match on "bar": the Highlighter
    // then colors nothing, although the pattern matches.
    const auto onlyMatch = GENERATE( false, true );
    HighlighterEditor editor( onlyMatch, QStringLiteral( "(foo)?bar" ) );
    part<QPushButton>( *editor.edit, "testPatternButton" )->click();
    const auto lab = openedLab( *editor.edit );
    pasteSample( *lab, QStringLiteral( "bar\nfoobar" ) );

    CHECK_FALSE( lab->result().lines[ 0 ].isMatch );
    CHECK( lab->result().lines[ 1 ].isMatch );
    CHECK( lab->result().matchingLines == 1 );
    CHECK( markedTexts( *lab ) == QStringList{ onlyMatch ? "foo" : "foobar" } );
}

SCENARIO( "Apply in the Regex Lab writes a Highlighter's pattern and options back",
          "[ui][regexlab][highlighters]" )
{
    HighlighterEditor editor( true );
    QSignalSpy changed( editor.setEdit, &HighlighterSetEdit::changed );
    part<QPushButton>( *editor.edit, "testPatternButton" )->click();
    const auto lab = openedLab( *editor.edit );

    WHEN( "the pattern and options are changed and applied" )
    {
        typePattern( *lab, "id=[0-9]+" );
        part<QCheckBox>( *lab, "matchCase" )->setChecked( true );
        part<QCheckBox>( *lab, "useRegexp" )->setChecked( false );
        labButton( *lab, QDialogButtonBox::Apply )->click();

        THEN( "the editor holds them, as if they had been entered there, and the Lab is gone" )
        {
            const auto highlighter = editor.edit->highlighter();
            CHECK( highlighter.pattern() == "id=[0-9]+" );
            CHECK_FALSE( highlighter.ignoreCase() );
            CHECK_FALSE( highlighter.useRegex() );
            CHECK( editor.edit->patternEdit->text() == "id=[0-9]+" );
            CHECK_FALSE( editor.edit->ignoreCaseCheckBox->isChecked() );
            CHECK( editor.edit->patternTypeComboBox->currentIndex() == 1 );
            // The set editor took them in, as it takes what is typed.
            CHECK( editor.setEdit->highlighterListWidget->item( 0 )->text() == "id=[0-9]+" );
            CHECK_FALSE( changed.isEmpty() );
            CHECK( waitUiState( [ & ] { return lab.isNull(); } ) );
        }
    }

    WHEN( "the pattern is changed and the Lab cancelled" )
    {
        typePattern( *lab, "id=[0-9]+" );
        labButton( *lab, QDialogButtonBox::Cancel )->click();

        THEN( "the editor is as it was" )
        {
            CHECK( editor.edit->highlighter().pattern() == "user=(\\w+)" );
            CHECK( editor.edit->patternEdit->text() == "user=(\\w+)" );
            CHECK( changed.isEmpty() );
            CHECK( waitUiState( [ & ] { return lab.isNull(); } ) );
        }
    }

    WHEN( "the pattern is changed and the Lab closed" )
    {
        typePattern( *lab, "id=[0-9]+" );
        lab->close();

        THEN( "the editor is as it was" )
        {
            CHECK( editor.edit->highlighter().pattern() == "user=(\\w+)" );
            CHECK( changed.isEmpty() );
        }
    }
}

SCENARIO( "Test... opens the Regex Lab with a Predefined Filter's pattern and options",
          "[ui][regexlab][predefinedfilters]" )
{
    FilterEditor editor;
    auto* test = part<QPushButton>( *editor.edit, "testFilterButton" );

    GIVEN( "no filter is current" )
    {
        THEN( "there is nothing to test" )
        {
            CHECK_FALSE( test->isEnabled() );
        }
    }

    GIVEN( "a filter is current" )
    {
        editor.edit->filtersTableWidget->setCurrentCell( 1, 1 );
        REQUIRE( test->isEnabled() );
        test->click();
        const auto lab = openedLab( *editor.edit );

        THEN( "the Lab opens over the editor with its pattern, read as a Search reads it" )
        {
            CHECK( lab->windowModality() == Qt::WindowModal );
            CHECK( labButton( *lab, QDialogButtonBox::Apply ) != nullptr );
            const auto pattern = lab->pattern();
            CHECK( pattern.pattern == "err.r" );
            CHECK( pattern.isPlainText );
            CHECK( pattern.isCaseSensitive );
            CHECK_FALSE( pattern.isBoolean );
            CHECK( lab->engine() == RegexpEngine::Vectorscan );
            CHECK_FALSE( lab->hasLineDecision() );
        }

        THEN( "Regex can be changed; Match case, which comes from the Search Line, is shown fixed" )
        {
            CHECK( part<QCheckBox>( *lab, "useRegexp" )->isEnabled() );
            CHECK_FALSE( part<QCheckBox>( *lab, "matchCase" )->isHidden() );
            CHECK_FALSE( part<QCheckBox>( *lab, "matchCase" )->isEnabled() );
            CHECK( part<QCheckBox>( *lab, "inverse" )->isHidden() );
            CHECK( part<QCheckBox>( *lab, "logicalCombination" )->isHidden() );
        }
    }
}

SCENARIO( "Apply in the Regex Lab writes a Predefined Filter's pattern and options back",
          "[ui][regexlab][predefinedfilters]" )
{
    FilterEditor editor;
    editor.edit->filtersTableWidget->setCurrentCell( 1, 1 );
    QSignalSpy changed( editor.edit, &PredefinedFilterSetEdit::changed );
    part<QPushButton>( *editor.edit, "testFilterButton" )->click();
    const auto lab = openedLab( *editor.edit );
    pasteSample( *lab, QStringLiteral( "an error\nerr.r\nWARN" ) );
    REQUIRE( markedTexts( *lab ) == QStringList{ "err.r" } );

    WHEN( "the pattern is made a regular expression and applied" )
    {
        typePattern( *lab, "err.r|WARN" );
        part<QCheckBox>( *lab, "useRegexp" )->setChecked( true );
        labButton( *lab, QDialogButtonBox::Apply )->click();

        THEN( "the filter holds them, as if they had been entered in the table" )
        {
            const auto filters = editor.edit->filterSet().filters();
            CHECK( filters[ 1 ].pattern == "err.r|WARN" );
            CHECK( filters[ 1 ].useRegex );
            CHECK( filters[ 1 ].name == "Errors" );
            CHECK( editor.edit->filtersTableWidget->item( 1, 1 )->text() == "err.r|WARN" );
            // The other filter is left alone.
            CHECK( filters[ 0 ].pattern == "user=" );
            CHECK_FALSE( changed.isEmpty() );
            CHECK( waitUiState( [ & ] { return lab.isNull(); } ) );
        }
    }

    WHEN( "the pattern is changed and the Lab cancelled" )
    {
        typePattern( *lab, "err.r|WARN" );
        part<QCheckBox>( *lab, "useRegexp" )->setChecked( true );
        labButton( *lab, QDialogButtonBox::Cancel )->click();

        THEN( "the filter is as it was" )
        {
            const auto filters = editor.edit->filterSet().filters();
            CHECK( filters[ 1 ].pattern == "err.r" );
            CHECK_FALSE( filters[ 1 ].useRegex );
            CHECK( changed.isEmpty() );
        }
    }
}

SCENARIO( "A Team group that cannot be changed is tested without Apply",
          "[ui][regexlab][predefinedfilters]" )
{
    FilterEditor editor( true );
    editor.edit->filtersTableWidget->setCurrentCell( 0, 1 );
    auto* test = part<QPushButton>( *editor.edit, "testFilterButton" );
    REQUIRE( test->isEnabled() );
    test->click();
    const auto lab = openedLab( *editor.edit );

    CHECK( lab->pattern().pattern == "user=" );
    CHECK( labButton( *lab, QDialogButtonBox::Apply ) == nullptr );
    CHECK( labButton( *lab, QDialogButtonBox::Close ) != nullptr );
}

SCENARIO( "The Regex Lab decides on a Predefined Filter as the Search Line it goes to does",
          "[ui][regexlab][predefinedfilters]" )
{
    const auto searchUsesRegexp = GENERATE( true, false );
    const auto isRegexp = GENERATE( true, false );
    const QStringList sample{ "an error", "err.r", "ERR.R", "WARN" };

    GIVEN( std::string( "a Search Line " ) + ( searchUsesRegexp ? "in" : "not in" )
           + " regular expression mode and a filter that is "
           + ( isRegexp ? "a regular expression" : "plain text" ) )
    {
        FilterEditor editor( false, searchUsesRegexp, isRegexp );
        editor.edit->filtersTableWidget->setCurrentCell( 1, 1 );
        part<QPushButton>( *editor.edit, "testFilterButton" )->click();
        const auto lab = openedLab( *editor.edit );
        pasteSample( *lab, sample.join( QChar::LineFeed ) );

        THEN( "the Lab matches the lines a Search with the filter used on that Search Line "
              "matches" )
        {
            SearchLine searchLine( QuickFindPolicy{} );
            searchLine.setFlags( SearchLine::Flags{ .matchCase = true,
                                                    .useRegexp = searchUsesRegexp,
                                                    .inverse = false,
                                                    .booleanCombination = false,
                                                    .autoRefresh = false } );
            searchLine.useFilters( { PredefinedFilter{ QStringLiteral( "Errors" ),
                                                       QStringLiteral( "err.r" ), isRegexp } } );
            const RegularExpression expression( searchLine.request(), RegexpEngine::Vectorscan );
            REQUIRE( expression.isValid() );
            const auto matcher = expression.createMatcher();

            REQUIRE( lab->result().lines.size() == static_cast<std::size_t>( sample.size() ) );
            for ( qsizetype i = 0; i < sample.size(); ++i ) {
                const auto utf8 = sample[ i ].toUtf8();
                INFO( "line " << sample[ i ].toStdString() );
                CHECK( lab->result().lines[ static_cast<std::size_t>( i ) ].isMatch
                       == matcher->hasMatch( std::string_view(
                           utf8.constData(), static_cast<std::size_t>( utf8.size() ) ) ) );
            }
        }

        THEN( "whether the filter is a regular expression can be changed only where it counts" )
        {
            CHECK( part<QCheckBox>( *lab, "useRegexp" )->isEnabled() == searchUsesRegexp );
            CHECK_FALSE( part<QCheckBox>( *lab, "useRegexp" )->isHidden() );
        }
    }
}

SCENARIO( "Apply in the Regex Lab keeps a filter's Regex setting where the Search Line does not "
          "read it",
          "[ui][regexlab][predefinedfilters]" )
{
    FilterEditor editor( false, false, true );
    editor.edit->filtersTableWidget->setCurrentCell( 1, 1 );
    part<QPushButton>( *editor.edit, "testFilterButton" )->click();
    const auto lab = openedLab( *editor.edit );
    typePattern( *lab, "error" );
    labButton( *lab, QDialogButtonBox::Apply )->click();

    const auto filters = editor.edit->filterSet().filters();
    CHECK( filters[ 1 ].pattern == "error" );
    CHECK( filters[ 1 ].useRegex );
}
