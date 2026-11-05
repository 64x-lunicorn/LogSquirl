//
// Created by omni on 16.12.2019.
//

#ifndef LOGSQUIRL_ENCODINGS_H
#define LOGSQUIRL_ENCODINGS_H

#include "textencoding.h"
#include <QActionGroup>
#include <QApplication>
#include <QCoreApplication>
#include <QMenu>
#include <QString>

#include <mainwindowtext.h>
#include <settingspolicies.h>

// The families are named in the translation files under the EncodingMenu
// context and translated when shown (#915); the Encodings themselves are
// named by their IANA names, in no language.
class EncodingMenu {
public:
    // The Encodings by family, each family by its untranslated name: the
    // menu is ordered by it, whatever the language.
    static std::map<QString, std::vector<int>> supportedEncodings()
    {
        return std::map<QString, std::vector<int>>{
            { QT_TRANSLATE_NOOP( "EncodingMenu", "Unicode" ), { 106, 1013, 1014, 1018, 1019 } },
            { QT_TRANSLATE_NOOP( "EncodingMenu", "Arabic" ), { 82, 2256 } },
            { QT_TRANSLATE_NOOP( "EncodingMenu", "Baltic" ), { 7, 109, 2257 } },
            { QT_TRANSLATE_NOOP( "EncodingMenu", "Celtic" ), { 110 } },
            { QT_TRANSLATE_NOOP( "EncodingMenu", "Cyrillic" ),
              { 8, 2084, 2088, 2027, 2086, 2251 } },
            { QT_TRANSLATE_NOOP( "EncodingMenu", "Central European" ), { 2250 } },
            { QT_TRANSLATE_NOOP( "EncodingMenu", "Chinese" ), { 2026, 2025 } },
            { QT_TRANSLATE_NOOP( "EncodingMenu", "Eastern European" ), { 5 } },
            { QT_TRANSLATE_NOOP( "EncodingMenu", "Greek" ), { 10, 2253 } },
            { QT_TRANSLATE_NOOP( "EncodingMenu", "Hebrew" ), { 85, 2255 } },
            { QT_TRANSLATE_NOOP( "EncodingMenu", "Japanese" ), { 17, 18, 39 } },
            { QT_TRANSLATE_NOOP( "EncodingMenu", "Korean" ), { -949, 38 } },
            { QT_TRANSLATE_NOOP( "EncodingMenu", "Thai" ), { 2259 } },
            { QT_TRANSLATE_NOOP( "EncodingMenu", "Turkish" ), { 6, 12, 2254 } },
            { QT_TRANSLATE_NOOP( "EncodingMenu", "Western European" ),
              { 3, 4, 111, 2009, 2011, 2252 } },
            { QT_TRANSLATE_NOOP( "EncodingMenu", "Vietnamese" ), { 2258 } }
        };
    }

    // The family's name in the current language.
    static QString familyTitle( const QString& family )
    {
        return QCoreApplication::translate( "EncodingMenu", family.toUtf8().constData() );
    }

    // The object names of the Auto and the System entries of the menu, which
    // retranslate() tells them apart by.
    static constexpr auto AutoEntryName = "autoEncoding";
    static constexpr auto SystemEntryName = "systemEncoding";

    // The "Auto" entry, offered by the menu and the Preferences dialog alike,
    // in the current language.
    static QString autoEncodingText()
    {
        return QCoreApplication::translate( "logsquirl::mainwindow::action",
                                            logsquirl::mainwindow::action::autoEncodingText );
    }

    // Builds the Encoding menu, with the Encoding a Log File is read with by
    // default already checked. The Policy is passed in because this is a
    // static method: there is no instance to have been handed one, and the
    // menu must not reach for the settings store itself. The menu is the
    // owner's, and deleted with it: adding it to another menu does not hand
    // it over (#916).
    static QMenu* generate( QActionGroup* actionGroup, const FileAccessPolicy& fileAccess,
                            QWidget* owner )
    {
        const auto supportedEncodings = EncodingMenu::supportedEncodings();
        const auto defaultEncodingMib = fileAccess.defaultEncodingMib;

        QMenu* encodingsMenu = new QMenu( owner );

        // Auto is the one entry without an Encoding.
        auto autoEncoding = encodingsMenu->addAction( QString{} );
        autoEncoding->setObjectName( QLatin1String( AutoEntryName ) );
        autoEncoding->setCheckable( true );
        autoEncoding->setActionGroup( actionGroup );
        autoEncoding->setChecked( defaultEncodingMib < 0 );

        encodingsMenu->addSeparator();

        // The Encoding the system reads text with; on Windows that is the
        // active code page.
        const auto* systemCodec = TextEncoding::forLocale();

        auto systemEncoding = encodingsMenu->addAction( QString{} );
        systemEncoding->setObjectName( QLatin1String( SystemEntryName ) );
        systemEncoding->setCheckable( true );
        systemEncoding->setActionGroup( actionGroup );
        systemEncoding->setData( systemCodec->mibEnum() );

        encodingsMenu->addSeparator();

        for ( const auto& group : supportedEncodings ) {
            auto menu = encodingsMenu->addMenu( QString{} );
            // The family's untranslated name, which retranslate() titles
            // the submenu from.
            menu->menuAction()->setData( group.first );
            for ( const auto mib : group.second ) {
                auto codec = TextEncoding::forMib( mib );
                if ( codec ) {
                    auto action = menu->addAction( QString::fromLatin1( codec->name() ) );
                    action->setActionGroup( actionGroup );
                    action->setData( mib );
                    action->setCheckable( true );
                    action->setChecked( defaultEncodingMib == mib );
                }
            }
        }

        retranslate( *encodingsMenu );
        return encodingsMenu;
    }

    // Titles the menu built by generate(), its entries and the family
    // submenus in the current language.
    static void retranslate( QMenu& encodingsMenu )
    {
        using namespace logsquirl::mainwindow;
        encodingsMenu.setTitle(
            QApplication::translate( "logsquirl::mainwindow::menu", menu::encodingTitle ) );

        for ( auto* entry : encodingsMenu.actions() ) {
            if ( entry->isSeparator() ) {
                continue;
            }
            if ( entry->menu() != nullptr ) {
                entry->menu()->setTitle( familyTitle( entry->data().toString() ) );
            }
            else if ( entry->objectName() == QLatin1String( AutoEntryName ) ) {
                entry->setText( autoEncodingText() );
                entry->setStatusTip( QApplication::translate( "logsquirl::mainwindow::action",
                                                              action::autoEncodingStatusTip ) );
            }
            else if ( entry->objectName() == QLatin1String( SystemEntryName ) ) {
                if ( const auto* system = TextEncoding::forMib( entry->data().toInt() ) ) {
                    entry->setText(
                        QApplication::tr( "System (%1)" ).arg( system->name().constData() ) );
                }
            }
        }
    }
};

#endif // LOGSQUIRL_ENCODINGS_H
