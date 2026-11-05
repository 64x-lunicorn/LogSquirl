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

// The Chart Panel's series and its Value Count tabs, without a Log File (#444).

#include <catch2/catch_test_macros.hpp>

#include <functional>

#include <QAction>
#include <QApplication>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>

#include "chartpanel.h"
#include "chartseries.h"
#include "chartseriesdialog.h"

namespace {

ChartSeriesDefinition seriesNamed( const QString& id, const QString& name )
{
    ChartSeriesDefinition def;
    def.id = id;
    def.name = name;
    def.pattern = R"(took (\d+) ms)";
    def.captureGroup = 1;
    def.compilePattern();
    return def;
}

QTabWidget* tabsOf( ChartPanel& panel )
{
    auto* tabs = panel.findChild<QTabWidget*>();
    REQUIRE( tabs != nullptr );
    return tabs;
}

// What the Chart Series dialog showed when it opened.
struct Shown {
    QString name;
    QString pattern;
    int captureGroup = 0;
};

// Runs opening, which opens the Chart Series dialog, notes what the dialog
// shows, fills in name and pattern and accepts, or cancels.
Shown inChartSeriesDialog( const std::function<void()>& opening, const QString& name,
                           const QString& pattern, bool accept )
{
    bool opened = false;
    Shown shown;
    QTimer::singleShot( 0, [ & ] {
        auto* dialog = qobject_cast<ChartSeriesDialog*>( QApplication::activeModalWidget() );
        REQUIRE( dialog != nullptr );
        opened = true;
        auto* nameEdit = dialog->findChild<QLineEdit*>( QStringLiteral( "seriesName" ) );
        auto* patternEdit = dialog->findChild<QLineEdit*>( QStringLiteral( "seriesPattern" ) );
        auto* group = dialog->findChild<QSpinBox*>( QStringLiteral( "seriesCaptureGroup" ) );
        REQUIRE( nameEdit != nullptr );
        REQUIRE( patternEdit != nullptr );
        REQUIRE( group != nullptr );
        shown = Shown{ nameEdit->text(), patternEdit->text(), group->value() };
        nameEdit->setText( name );
        patternEdit->setText( pattern );
        dialog->findChild<QDialogButtonBox*>()
            ->button( accept ? QDialogButtonBox::Ok : QDialogButtonBox::Cancel )
            ->click();
    } );
    opening();
    REQUIRE( opened );
    return shown;
}

QAction* addSeriesAction( ChartPanel& panel )
{
    for ( auto* action : panel.findChildren<QAction*>() ) {
        if ( action->text() == QStringLiteral( "+ Add Series" ) ) {
            return action;
        }
    }
    FAIL( "no + Add Series" );
    return nullptr;
}

} // namespace

TEST_CASE( "A Chart Panel starts with no series and gives back the ones it was given",
           "[chartpanel]" )
{
    ChartPanel panel;
    CHECK( panel.seriesDefinitions().isEmpty() );

    panel.setSeriesDefinitions( { seriesNamed( "a", "First" ), seriesNamed( "b", "Second" ) } );
    const auto definitions = panel.seriesDefinitions();
    REQUIRE( definitions.size() == 2 );
    CHECK( definitions[ 0 ].id == "a" );
    CHECK( definitions[ 0 ].name == "First" );
    CHECK( definitions[ 1 ].id == "b" );
    CHECK( definitions[ 1 ].pattern == R"(took (\d+) ms)" );

    panel.setSeriesDefinitions( {} );
    CHECK( panel.seriesDefinitions().isEmpty() );
}

TEST_CASE( "A Filter frequency series counts each pattern with the Search's Match case",
           "[chartpanel]" )
{
    ChartPanel panel;

    panel.addFilterFrequencySeries( { "error", "warn" }, false );
    const auto definitions = panel.seriesDefinitions();
    REQUIRE( definitions.size() == 2 );
    CHECK( definitions[ 0 ].pattern.contains( "error" ) );
    CHECK( definitions[ 1 ].pattern.contains( "warn" ) );
    for ( const auto& definition : definitions ) {
        CHECK_FALSE( definition.matchCase );
        // A count, not a value: the whole match.
        CHECK( definition.captureGroup == 0 );
    }
}

TEST_CASE( "A Value Count needs a Log File, and a Log Format for a field", "[chartpanel]" )
{
    ChartPanel panel;
    const auto tabsBefore = tabsOf( panel )->count();

    panel.countFieldValues( "level" );
    panel.countCaptureGroupValues( QRegularExpression( "(\\w+)" ), 1, "group 1" );

    CHECK( tabsOf( panel )->count() == tabsBefore );
}

TEST_CASE( "+ Add Series and a proposed series open the Chart Series dialog, and OK adds the "
           "series",
           "[chartpanel]" )
{
    ChartPanel panel;

    SECTION( "+ Add Series opens it empty, capture group 1" )
    {
        const auto shown = inChartSeriesDialog( [ & ] { addSeriesAction( panel )->trigger(); },
                                                "Took", R"(took (\d+) ms)", true );
        CHECK( shown.name.isEmpty() );
        CHECK( shown.pattern.isEmpty() );
        CHECK( shown.captureGroup == 1 );
        const auto definitions = panel.seriesDefinitions();
        REQUIRE( definitions.size() == 1 );
        CHECK( definitions[ 0 ].name == "Took" );
        CHECK( definitions[ 0 ].pattern == R"(took (\d+) ms)" );
        CHECK( definitions[ 0 ].color.isValid() );
    }

    SECTION( "a proposed series opens it filled in, and tells it was added" )
    {
        bool added = false;
        const auto shown = inChartSeriesDialog(
            [ & ] { added = panel.addProposedSeries( "took", R"(took (\d+))" ); }, "took",
            R"(took (\d+))", true );
        CHECK( shown.name == "took" );
        CHECK( shown.pattern == R"(took (\d+))" );
        CHECK( shown.captureGroup == 1 );
        CHECK( added );
        REQUIRE( panel.seriesDefinitions().size() == 1 );
    }

    SECTION( "Cancel adds nothing, and tells so" )
    {
        bool added = true;
        inChartSeriesDialog( [ & ] { added = panel.addProposedSeries( "took", R"(took (\d+))" ); },
                             "took", R"(took (\d+))", false );
        CHECK_FALSE( added );
        CHECK( panel.seriesDefinitions().isEmpty() );
    }
}
