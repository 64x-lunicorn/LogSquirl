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

#include "releasepages.h"
#include "theme.h"
#include "welcomedashboard.h"

#include <QApplication>
#include <QDesktopServices>
#include <QFrame>
#include <QImage>
#include <QLabel>
#include <QMap>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSignalSpy>
#include <QTest>
#include <QTextDocument>
#include <QTranslator>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

// The Dashboard uses readable type and shows its lists as cards (#266).

namespace {

std::vector<QFrame*> cards( const WelcomeDashboard& dashboard )
{
    std::vector<QFrame*> result;
    for ( auto* frame : dashboard.findChildren<QFrame*>( QStringLiteral( "dashboardCard" ) ) ) {
        result.push_back( frame );
    }
    std::sort( result.begin(), result.end(), []( const QFrame* lhs, const QFrame* rhs ) {
        return lhs->mapToGlobal( QPoint{} ).y() < rhs->mapToGlobal( QPoint{} ).y();
    } );
    return result;
}

QLabel* cardTitle( const QFrame* card )
{
    return card->findChild<QLabel*>( QStringLiteral( "dashboardCardTitle" ) );
}

} // namespace

SCENARIO( "The Dashboard shows its lists as cards in one column", "[ui][theme]" )
{
    GIVEN( "a Dashboard shown in a wide window" )
    {
        Theme::apply( Theme::LightKey );
        WelcomeDashboard dashboard;
        dashboard.resize( 1200, 900 );
        dashboard.refresh();
        dashboard.show();
        REQUIRE( QTest::qWaitForWindowExposed( &dashboard ) );
        QTest::qWait( 20 );

        THEN( "Recent Files, Favorites and Plugins are cards of one width, directly below one "
              "another" )
        {
            const auto found = cards( dashboard );
            REQUIRE( found.size() == 3 );
            REQUIRE( cardTitle( found[ 0 ] )->text() == QStringLiteral( "Recent Files" ) );
            REQUIRE( cardTitle( found[ 1 ] )->text() == QStringLiteral( "Favorites" ) );
            REQUIRE( cardTitle( found[ 2 ] )->text() == QStringLiteral( "Plugins" ) );

            for ( std::size_t i = 0; i < found.size(); ++i ) {
                INFO( i );
                REQUIRE( found[ i ]->width() == found[ 0 ]->width() );
                REQUIRE( found[ i ]->mapToGlobal( QPoint{} ).x()
                         == found[ 0 ]->mapToGlobal( QPoint{} ).x() );
                REQUIRE( found[ i ]->width() <= 600 );
                if ( i > 0 ) {
                    const auto gap = found[ i ]->mapToGlobal( QPoint{} ).y()
                                     - found[ i - 1 ]->mapToGlobal( QPoint( 0, 0 ) ).y()
                                     - found[ i - 1 ]->height();
                    REQUIRE( gap >= 0 );
                    REQUIRE( gap <= 24 );
                }
            }
        }

        THEN( "no text on the Dashboard has a fixed pixel size, and body text is the "
              "application font" )
        {
            static const QRegularExpression pixelFont( "font-size\\s*:\\s*\\d+px" );
            for ( auto* widget : dashboard.findChildren<QWidget*>() ) {
                INFO( widget->metaObject()->className()
                      << " " << widget->objectName().toStdString() );
                REQUIRE_FALSE( pixelFont.match( widget->styleSheet() ).hasMatch() );
            }
            for ( auto* label : dashboard.findChildren<QLabel*>() ) {
                if ( label->objectName() == QStringLiteral( "dashboardCardTitle" ) ) {
                    continue;
                }
                INFO( label->text().toStdString() );
                REQUIRE( label->font().pointSizeF() >= QApplication::font().pointSizeF() );
            }
        }

        THEN( "a card's title is bold and a quarter larger than the application font" )
        {
            for ( auto* card : cards( dashboard ) ) {
                const auto* title = cardTitle( card );
                REQUIRE( title != nullptr );
                INFO( title->text().toStdString() );
                REQUIRE( title->font().bold() );
                REQUIRE( title->font().pointSizeF()
                         == Catch::Approx( QApplication::font().pointSizeF() * 1.25 ) );
            }
        }

        THEN( "Open File is the primary action" )
        {
            QPushButton* openFile = nullptr;
            for ( auto* button : dashboard.findChildren<QPushButton*>() ) {
                if ( button->text() == QStringLiteral( "Open File" ) ) {
                    openFile = button;
                }
            }
            REQUIRE( openFile != nullptr );
            REQUIRE( openFile->property( "primaryAction" ).toBool() );
        }

        Theme::apply( Theme::defaultTheme() );
    }
}

SCENARIO( "The Dashboard's cards take the surface color of every Theme", "[ui][theme]" )
{
    for ( const auto& name : Theme::builtInThemes() ) {
        GIVEN( "a Dashboard shown under " + name.toStdString() )
        {
            Theme::apply( name );
            WelcomeDashboard dashboard;
            dashboard.resize( 1000, 900 );
            dashboard.refresh();
            dashboard.show();
            REQUIRE( QTest::qWaitForWindowExposed( &dashboard ) );
            QTest::qWait( 20 );

            THEN( "every card is filled with the Panel color" )
            {
                const auto panel = Theme::active().color( ColorToken::Panel );
                for ( auto* card : cards( dashboard ) ) {
                    const auto image = card->grab().toImage();
                    // Inside the border and the rounded corner, left of any text.
                    const QColor inside = image.pixelColor( 5, image.height() - 5 );
                    INFO( cardTitle( card )->text().toStdString() );
                    REQUIRE( inside.name() == panel.name() );
                }
            }

            Theme::apply( Theme::defaultTheme() );
        }
    }
}

// The Dashboard has a quiet footer with links to the project (#933).

namespace {

QLabel* footerLinks( const WelcomeDashboard& dashboard )
{
    return dashboard.findChild<QLabel*>( QStringLiteral( "dashboardFooterLinks" ) );
}

/// The links of a rich-text label: the target of each, by its text.
QMap<QString, QString> linksOf( const QLabel& label )
{
    static const QRegularExpression anchor(
        QStringLiteral( "<a\\s+href=\"([^\"]*)\"[^>]*>([^<]*)</a>" ) );
    QMap<QString, QString> result;
    auto matches = anchor.globalMatch( label.text() );
    while ( matches.hasNext() ) {
        const auto match = matches.next();
        result.insert( match.captured( 2 ), match.captured( 1 ) );
    }
    return result;
}

/// The colors the links of a rich-text label are written in.
QStringList linkColorsOf( const QLabel& label )
{
    static const QRegularExpression anchor(
        QStringLiteral( "<a\\s[^>]*style=\"[^\"]*color:\\s*(#[0-9a-fA-F]{6})" ) );
    QStringList result;
    auto matches = anchor.globalMatch( label.text() );
    while ( matches.hasNext() ) {
        result.append( matches.next().captured( 1 ).toLower() );
    }
    return result;
}

QString plainTextOf( const QLabel& label )
{
    QTextDocument document;
    document.setHtml( label.text() );
    return document.toPlainText();
}

double distance( const QColor& lhs, const QColor& rhs )
{
    return std::hypot( lhs.red() - rhs.red(), lhs.green() - rhs.green(), lhs.blue() - rhs.blue() );
}

/// The color farthest from the background in the label's rendering: the
/// color its text is drawn in.
QColor inkOf( QLabel& label )
{
    const auto image = label.grab().toImage();
    const auto background = image.pixelColor( 0, 0 );
    QColor ink = background;
    for ( int y = 0; y < image.height(); ++y ) {
        for ( int x = 0; x < image.width(); ++x ) {
            const auto pixel = image.pixelColor( x, y );
            if ( distance( pixel, background ) > distance( ink, background ) ) {
                ink = pixel;
            }
        }
    }
    return ink;
}

// Takes the web pages the Dashboard asks to open, instead of a browser.
class DashboardWebPages : public QObject {
    Q_OBJECT
public:
    DashboardWebPages()
    {
        QDesktopServices::setUrlHandler( "https", this, "open" );
    }
    ~DashboardWebPages() override
    {
        QDesktopServices::unsetUrlHandler( "https" );
    }

    DashboardWebPages( const DashboardWebPages& ) = delete;
    DashboardWebPages& operator=( const DashboardWebPages& ) = delete;

    QList<QUrl> opened;

public Q_SLOTS:
    void open( const QUrl& url )
    {
        opened.append( url );
    }
};

// Marks every text of the Dashboard it translates.
class MarkingDashboardTranslator : public QTranslator {
public:
    QString translate( const char* context, const char* sourceText, const char* /*disambiguation*/,
                       int /*n*/ ) const override
    {
        if ( QString::fromLatin1( context ) != QStringLiteral( "WelcomeDashboard" ) ) {
            return {};
        }
        return u'[' + QString::fromUtf8( sourceText ) + u']';
    }

    bool isEmpty() const override
    {
        return false;
    }
};

} // namespace

SCENARIO( "The Dashboard has a footer with links to the project at its bottom edge",
          "[ui][dashboard]" )
{
    GIVEN( "a Dashboard shown in a window" )
    {
        Theme::apply( Theme::LightKey );
        WelcomeDashboard dashboard;
        dashboard.resize( 1200, 900 );
        dashboard.refresh();
        dashboard.show();
        REQUIRE( QTest::qWaitForWindowExposed( &dashboard ) );
        QTest::qWait( 20 );

        auto* footer = footerLinks( dashboard );
        REQUIRE( footer != nullptr );

        const auto atBottomEdge = [ & ] {
            const auto* scrollArea = dashboard.findChild<QScrollArea*>();
            REQUIRE( scrollArea != nullptr );
            const auto top = footer->mapTo( &dashboard, QPoint{} ).y();
            const auto bottom = top + footer->height();
            INFO( "footer " << top << ".." << bottom << " in " << dashboard.height()
                            << ", content ends at " << scrollArea->geometry().bottom() );
            // Below the content, never over it, and close to the edge.
            CHECK( top > scrollArea->geometry().bottom() );
            CHECK( bottom <= dashboard.height() );
            CHECK( bottom >= dashboard.height() - 24 );
        };

        THEN( "it is one line of text and links, at the bottom edge" )
        {
            CHECK( footer->isVisible() );
            CHECK( plainTextOf( *footer )
                   == QStringLiteral(
                       "LogSquirl is open source \u00B7 GitHub \u00B7 Report an Issue \u00B7 "
                       "Documentation" ) );
            CHECK( linksOf( *footer ).keys()
                   == QStringList{ QStringLiteral( "Documentation" ), QStringLiteral( "GitHub" ),
                                   QStringLiteral( "Report an Issue" ) } );
            CHECK_FALSE( footer->wordWrap() );
            CHECK_FALSE( footer->openExternalLinks() );
            atBottomEdge();
        }

        THEN( "it lies on the Dashboard's own background, without a band or frame" )
        {
            const auto image = dashboard.grab().toImage();
            const auto top = footer->mapTo( &dashboard, QPoint{} ).y();
            const auto besideFooter = image.pixelColor( 4, top + footer->height() / 2 );
            const auto aboveFooter = image.pixelColor( 4, top - 20 );
            CHECK( besideFooter.name() == aboveFooter.name() );
        }

        WHEN( "the window is made smaller and larger" )
        {
            for ( const auto& size : { QSize( 900, 600 ), QSize( 1600, 1100 ) } ) {
                dashboard.resize( size );
                QTest::qWait( 20 );
                INFO( size.width() << "x" << size.height() );
                THEN( "the footer stays at the bottom edge" )
                {
                    atBottomEdge();
                }
            }
        }

        WHEN( "GitHub is clicked" )
        {
            DashboardWebPages pages;
            const QSignalSpy reports( &dashboard, &WelcomeDashboard::reportIssueRequested );
            const QSignalSpy documentation( &dashboard, &WelcomeDashboard::documentationRequested );
            Q_EMIT footer->linkActivated( linksOf( *footer ).value( QStringLiteral( "GitHub" ) ) );

            THEN( "the project's repository opens, and nothing else" )
            {
                CHECK( pages.opened
                       == QList<QUrl>{ QUrl( logsquirl::versioncheck::ProjectPageUrl ) } );
                CHECK( reports.isEmpty() );
                CHECK( documentation.isEmpty() );
            }
        }

        WHEN( "Report an Issue is clicked" )
        {
            DashboardWebPages pages;
            const QSignalSpy reports( &dashboard, &WelcomeDashboard::reportIssueRequested );
            const QSignalSpy documentation( &dashboard, &WelcomeDashboard::documentationRequested );
            Q_EMIT footer->linkActivated(
                linksOf( *footer ).value( QStringLiteral( "Report an Issue" ) ) );

            THEN( "the Dashboard asks to report an issue, as Help > Report Issue... does" )
            {
                CHECK( reports.size() == 1 );
                CHECK( documentation.isEmpty() );
                CHECK( pages.opened.isEmpty() );
            }
        }

        WHEN( "Documentation is clicked" )
        {
            DashboardWebPages pages;
            const QSignalSpy reports( &dashboard, &WelcomeDashboard::reportIssueRequested );
            const QSignalSpy documentation( &dashboard, &WelcomeDashboard::documentationRequested );
            Q_EMIT footer->linkActivated(
                linksOf( *footer ).value( QStringLiteral( "Documentation" ) ) );

            THEN( "the Dashboard asks for the documentation, as Help > Documentation... does" )
            {
                CHECK( documentation.size() == 1 );
                CHECK( reports.isEmpty() );
                CHECK( pages.opened.isEmpty() );
            }
        }

        Theme::apply( Theme::defaultTheme() );
    }
}

SCENARIO( "The Dashboard's footer is drawn in the secondary text color of the Theme",
          "[ui][dashboard][theme]" )
{
    for ( const auto& name : { QString( Theme::LightKey ), QString( Theme::DarkKey ) } ) {
        GIVEN( "a Dashboard shown under " + name.toStdString() )
        {
            Theme::apply( Theme::LightKey );
            WelcomeDashboard dashboard;
            dashboard.resize( 1000, 800 );
            dashboard.refresh();
            dashboard.show();
            REQUIRE( QTest::qWaitForWindowExposed( &dashboard ) );
            Theme::apply( name );
            QCoreApplication::processEvents();
            QTest::qWait( 20 );

            auto* footer = footerLinks( dashboard );
            REQUIRE( footer != nullptr );
            const auto secondary = Theme::active().color( ColorToken::SecondaryText );
            const auto link = footer->palette().color( QPalette::Link );

            THEN( "its text and its links take the secondary text color, as the hints do" )
            {
                CHECK( footer->property( "secondaryText" ).toBool() );
                CHECK( footer->palette().color( QPalette::WindowText ) == secondary );
                const auto colors = linkColorsOf( *footer );
                CHECK( colors.size() == 3 );
                for ( const auto& color : colors ) {
                    CHECK( color == secondary.name() );
                }
                const auto ink = inkOf( *footer );
                INFO( "ink " << ink.name().toStdString() << ", secondary "
                             << secondary.name().toStdString() << ", link "
                             << link.name().toStdString() );
                CHECK( distance( ink, secondary ) < distance( ink, link ) );
            }

            THEN( "it has no styling of its own and the application font" )
            {
                CHECK( footer->styleSheet().isEmpty() );
                CHECK( footer->font().pointSizeF() == QApplication::font().pointSizeF() );
            }

            Theme::apply( Theme::defaultTheme() );
        }
    }
}

SCENARIO( "The Dashboard's footer is translated", "[ui][dashboard]" )
{
    GIVEN( "a translation of the Dashboard" )
    {
        MarkingDashboardTranslator translator;
        REQUIRE( QCoreApplication::installTranslator( &translator ) );

        WHEN( "a Dashboard is made" )
        {
            WelcomeDashboard dashboard;
            auto* footer = footerLinks( dashboard );
            REQUIRE( footer != nullptr );

            THEN( "its footer is shown translated, the names GitHub and LogSquirl aside" )
            {
                CHECK( plainTextOf( *footer )
                       == QStringLiteral( "[LogSquirl is open source] \u00B7 GitHub \u00B7 "
                                          "[Report an Issue] \u00B7 [Documentation]" ) );
            }
        }

        QCoreApplication::removeTranslator( &translator );
    }
}

#include "welcomedashboard_layout_test.moc"
