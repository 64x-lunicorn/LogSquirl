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

#include "valuenamesdialog.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSpinBox>
#include <QSplitter>
#include <QStringDecoder>
#include <QTableWidget>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif

#include "groupexchange.h"
#include "iconloader.h"
#include "textencoding.h"
#include "theme.h"
#include "valuenamer.h"
#include "valuenamescollection.h"

using logsquirl::valuenames::CsvImportOptions;
using logsquirl::valuenames::CsvImportWarning;
using logsquirl::valuenames::GroupTable;
using logsquirl::valuenames::NameRow;
using logsquirl::valuenames::NameTable;
using logsquirl::valuenames::NamingGroup;
using logsquirl::valuenames::NamingRule;
using logsquirl::valuenames::Problem;

namespace {

enum RuleColumn { RuleName = 0, RuleRegex = 1, RuleTemplate = 2 };
enum RowColumn { RowKey = 0, RowName = 1 };

QString separatorName( QChar separator )
{
    if ( separator == QLatin1Char( '\t' ) ) {
        return ValueNamesDialog::tr( "tab" );
    }
    if ( separator == QLatin1Char( ';' ) ) {
        return ValueNamesDialog::tr( "semicolon" );
    }
    return ValueNamesDialog::tr( "comma" );
}

// What is wrong with a group, told to the user. Rows are counted from 1.
// Every message takes its arguments in one arg() call: a key or a name may
// hold "%2" itself (a URL-encoded value), which a second call would replace.
QString problemMessage( const Problem& problem )
{
    switch ( problem.kind ) {
    case Problem::Kind::InvalidRuleRegex:
        return ValueNamesDialog::tr( "Rule \"%1\": the regex is not valid: %2" )
            .arg( problem.rule, problem.detail );
    case Problem::Kind::InvalidKeyRegex:
        return ValueNamesDialog::tr( "Table \"%1\", row %2: the key is not a valid regex: %3" )
            .arg( problem.table, QString::number( problem.row + 1 ), problem.detail );
    case Problem::Kind::DuplicateKey:
        return ValueNamesDialog::tr(
                   "Table \"%1\", row %2: the key \"%3\" is already in row %4, so this row "
                   "is never used" )
            .arg( problem.table, QString::number( problem.row + 1 ), problem.detail,
                  QString::number( problem.firstRow + 1 ) );
    case Problem::Kind::MissingKeyGroup:
        return ValueNamesDialog::tr(
                   "Table \"%1\", row %2: the name uses %3, but the key has no such group" )
            .arg( problem.table, QString::number( problem.row + 1 ), problem.detail );
    case Problem::Kind::UnknownTable:
        return ValueNamesDialog::tr( "Rule \"%1\": this group has no Name Table \"%2\"" )
            .arg( problem.rule, problem.detail );
    case Problem::Kind::UnknownCaptureGroup:
        return ValueNamesDialog::tr( "Rule \"%1\": the regex has no capture group %2" )
            .arg( problem.rule, problem.detail );
    case Problem::Kind::DuplicateCaptureGroup:
        return ValueNamesDialog::tr( "Rule \"%1\": capture group %2 is given a Name Table more "
                                     "than once; only the first is used" )
            .arg( problem.rule, problem.detail );
    case Problem::Kind::DuplicateRuleName:
        return ValueNamesDialog::tr( "Rule \"%1\": another rule of the group has this name" )
            .arg( problem.rule );
    case Problem::Kind::ControlCharacterInTemplate:
        return ValueNamesDialog::tr( "Rule \"%1\": the template holds a line break or control "
                                     "character, which is shown as a space" )
            .arg( problem.rule );
    case Problem::Kind::ControlCharacterInName:
        return ValueNamesDialog::tr( "Table \"%1\", row %2: the name holds a line break or "
                                     "control character, which is shown as a space" )
            .arg( problem.table, QString::number( problem.row + 1 ) );
    }
    return {};
}

QString csvWarningMessage( const CsvImportWarning& warning )
{
    switch ( warning.kind ) {
    case CsvImportWarning::Kind::DuplicateKey:
        return ValueNamesDialog::tr(
                   "CSV line %1: the key \"%2\" was already read on line %3; the first wins" )
            .arg( QString::number( warning.line ), warning.key,
                  QString::number( warning.firstLine ) );
    case CsvImportWarning::Kind::MissingColumn:
        return ValueNamesDialog::tr( "CSV line %1: no key or no name; the line is skipped" )
            .arg( warning.line );
    case CsvImportWarning::Kind::EmptyName:
        return ValueNamesDialog::tr( "CSV line %1: the name of \"%2\" is empty" )
            .arg( QString::number( warning.line ), warning.key );
    case CsvImportWarning::Kind::ControlCharacterInName:
        return ValueNamesDialog::tr( "CSV line %1: the name of \"%2\" holds a line break or "
                                     "control character, which is shown as a space" )
            .arg( QString::number( warning.line ), warning.key );
    }
    return {};
}

// The Name Tables of a rule's capture groups once its regex changed from
// oldPattern to newPattern. A table stays with its capture group: a named
// group renamed keeps it under its new name, the whole match's goes to group
// 1 when the first capture group appears, and that of a group gone goes too.
// While the new regex is not valid nothing is changed.
QList<GroupTable> adaptedGroupTables( const QList<GroupTable>& groupTables,
                                      const QString& oldPattern, const QString& newPattern )
{
    const QRegularExpression newRegex( newPattern );
    if ( !newRegex.isValid() ) {
        return groupTables;
    }
    const auto count = newRegex.captureCount();
    const auto newNames = newRegex.namedCaptureGroups();
    const QRegularExpression oldRegex( oldPattern );
    const auto oldNames = oldRegex.isValid() ? oldRegex.namedCaptureGroups() : QStringList{};

    // What a capture group of the new regex is assigned by.
    const auto keyOf = [ &newNames ]( qsizetype number ) {
        const auto name = newNames.value( number );
        return name.isEmpty() ? QString::number( number ) : name;
    };

    QList<GroupTable> adapted;
    QList<qsizetype> taken;
    for ( const auto& groupTable : groupTables ) {
        qsizetype number = -1;
        QString key;
        bool isNumber = false;
        const auto given = groupTable.group.toLongLong( &isNumber );
        if ( isNumber && given == 0 ) {
            number = count == 0 ? 0 : 1;
            key = count == 0 ? QStringLiteral( "0" ) : keyOf( 1 );
        }
        else if ( isNumber ) {
            if ( given >= 1 && given <= count ) {
                number = given;
                key = groupTable.group;
            }
        }
        else if ( const auto now = newNames.indexOf( groupTable.group ); now > 0 ) {
            number = now;
            key = groupTable.group;
        }
        else if ( const auto before = oldNames.indexOf( groupTable.group );
                  before > 0 && before <= count ) {
            number = before;
            key = keyOf( before );
        }

        if ( number < 0 || taken.contains( number ) ) {
            continue;
        }
        taken.append( number );
        adapted.append( GroupTable{ key, groupTable.table } );
    }
    return adapted;
}

QStringList ruleNames( const QList<NamingRule>& rules, int except )
{
    QStringList names;
    for ( int index = 0; index < rules.size(); ++index ) {
        if ( index != except ) {
            names.append( rules[ index ].name );
        }
    }
    return names;
}

QStringList tableNames( const QList<NameTable>& tables, int except )
{
    QStringList names;
    for ( int index = 0; index < tables.size(); ++index ) {
        if ( index != except ) {
            names.append( tables[ index ].name );
        }
    }
    return names;
}

QToolButton* toolButton( const QString& toolTip, QWidget* parent )
{
    auto* button = new QToolButton( parent );
    button->setToolTip( toolTip );
    button->setAutoRaise( true );
    return button;
}

QToolButton* textButton( const QString& text, QWidget* parent )
{
    auto* button = new QToolButton( parent );
    button->setText( text );
    button->setToolButtonStyle( Qt::ToolButtonTextOnly );
    return button;
}

} // namespace

QString decodeCsvFile( const QByteArray& bytes )
{
    // A byte order mark says what it is.
    if ( const auto marked = QStringConverter::encodingForData( bytes ) ) {
        QStringDecoder decoder( *marked );
        return decoder.decode( bytes );
    }

    QStringDecoder utf8( QStringDecoder::Utf8 );
    QString text = utf8.decode( bytes );
    if ( !utf8.hasError() ) {
        return text;
    }

    // Not UTF-8: what a spreadsheet saves as "CSV" is in the ANSI code page.
#ifdef Q_OS_WIN
    const auto codePage = static_cast<int>( ::GetACP() );
#else
    constexpr int codePage = 1252;
#endif
    const auto* ansi = TextEncoding::forWindowsCodePage( codePage );
    if ( ansi == nullptr ) {
        ansi = TextEncoding::forMib( TextEncoding::Latin1Mib );
    }
    return ansi->toUnicode( bytes );
}

// --- NameTableCsvImportDialog ---

NameTableCsvImportDialog::NameTableCsvImportDialog( const QString& text, bool caseSensitive,
                                                    const QString& selectedTable, QWidget* parent )
    : QDialog( parent )
    , separator_( logsquirl::valuenames::detectCsvSeparator( text ) )
    , caseSensitive_( caseSensitive )
{
    setWindowTitle( tr( "Import CSV" ) );

    const auto records = logsquirl::valuenames::csvRecords( text, separator_ );
    qsizetype columns = 2;
    for ( const auto& record : records ) {
        columns = std::max( columns, record.fields.size() );
    }

    auto* layout = new QVBoxLayout( this );
    separatorLabel_ = new QLabel( tr( "Separator detected: %1" ).arg( separatorShown() ), this );
    layout->addWidget( separatorLabel_ );

    auto* form = new QFormLayout;
    keyColumn_ = new QSpinBox( this );
    keyColumn_->setRange( 1, static_cast<int>( columns ) );
    keyColumn_->setValue( 1 );
    nameColumn_ = new QSpinBox( this );
    nameColumn_->setRange( 1, static_cast<int>( columns ) );
    nameColumn_->setValue( 2 );
    form->addRow( tr( "Key column:" ), keyColumn_ );
    form->addRow( tr( "Name column:" ), nameColumn_ );
    target_ = new QComboBox( this );
    target_->addItem( tr( "New table (named after the file)" ),
                      static_cast<int>( CsvImportTarget::NewTable ) );
    if ( !selectedTable.isEmpty() ) {
        target_->addItem( tr( "Replace the rows of \"%1\"" ).arg( selectedTable ),
                          static_cast<int>( CsvImportTarget::ReplaceRows ) );
        target_->addItem( tr( "Append to \"%1\"" ).arg( selectedTable ),
                          static_cast<int>( CsvImportTarget::AppendRows ) );
    }
    form->addRow( tr( "Import into:" ), target_ );
    layout->addLayout( form );

    sameColumns_ = new QLabel( tr( "The key and the name need different columns." ), this );
    layout->addWidget( sameColumns_ );

    hasHeader_ = new QCheckBox( tr( "The first line is a header" ), this );
    layout->addWidget( hasHeader_ );

    // The first records, as they are read.
    constexpr int PreviewRecords = 8;
    auto* preview = new QTableWidget(
        static_cast<int>( std::min<qsizetype>( records.size(), PreviewRecords ) ),
        static_cast<int>( columns ), this );
    preview->setEditTriggers( QAbstractItemView::NoEditTriggers );
    for ( int row = 0; row < preview->rowCount(); ++row ) {
        const auto& fields = records[ row ].fields;
        for ( int column = 0; column < fields.size(); ++column ) {
            preview->setItem( row, column, new QTableWidgetItem( fields[ column ] ) );
        }
    }
    layout->addWidget( preview );

    buttons_ = new QDialogButtonBox( QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this );
    connect( buttons_, &QDialogButtonBox::accepted, this, &QDialog::accept );
    connect( buttons_, &QDialogButtonBox::rejected, this, &QDialog::reject );
    layout->addWidget( buttons_ );

    connect( keyColumn_, &QSpinBox::valueChanged, this,
             &NameTableCsvImportDialog::updateAcceptable );
    connect( nameColumn_, &QSpinBox::valueChanged, this,
             &NameTableCsvImportDialog::updateAcceptable );
    updateAcceptable();
}

void NameTableCsvImportDialog::updateAcceptable()
{
    const bool same = keyColumn_->value() == nameColumn_->value();
    sameColumns_->setVisible( same );
    buttons_->button( QDialogButtonBox::Ok )->setEnabled( !same );
}

CsvImportTarget NameTableCsvImportDialog::target() const
{
    return static_cast<CsvImportTarget>( target_->currentData().toInt() );
}

CsvImportOptions NameTableCsvImportDialog::options() const
{
    CsvImportOptions options;
    options.keyColumn = keyColumn_->value() - 1;
    options.nameColumn = nameColumn_->value() - 1;
    options.hasHeader = hasHeader_->isChecked();
    options.caseSensitive = caseSensitive_;
    return options;
}

QString NameTableCsvImportDialog::separatorShown() const
{
    return separatorName( separator_ );
}

// --- ValueNamesDialog ---

ValueNamesDialog::ValueNamesDialog( QWidget* parent )
    : QDialog( parent )
{
    setWindowTitle( tr( "Value Names" ) );
    resize( 1000, 720 );

    auto* mainLayout = new QVBoxLayout( this );
    auto* splitter = new QSplitter( Qt::Horizontal, this );
    mainLayout->addWidget( splitter, 1 );

    // --- Left: the Naming Groups ---
    auto* leftPanel = new QWidget( splitter );
    leftLayout_ = new QVBoxLayout( leftPanel );
    leftLayout_->setContentsMargins( 0, 0, 0, 0 );
    auto* groupsLabel = new QLabel( tr( "Naming Groups" ), leftPanel );
    groupsLabel->setAlignment( Qt::AlignCenter );
    leftLayout_->addWidget( groupsLabel );
    groupList_ = new QListWidget( leftPanel );
    groupList_->setObjectName( QStringLiteral( "groupList" ) );
    leftLayout_->addWidget( groupList_ );
    auto* groupButtons = new QHBoxLayout;
    addGroupButton_ = toolButton( tr( "Add a Naming Group" ), leftPanel );
    removeGroupButton_ = toolButton( tr( "Remove the Naming Group" ), leftPanel );
    upGroupButton_ = toolButton( tr( "Move up" ), leftPanel );
    downGroupButton_ = toolButton( tr( "Move down" ), leftPanel );
    for ( auto* button :
          { addGroupButton_, removeGroupButton_, upGroupButton_, downGroupButton_ } ) {
        groupButtons->addWidget( button );
    }
    groupButtons->addStretch();
    leftLayout_->addLayout( groupButtons );

    // --- Right: the selected group ---
    groupEditor_ = new QWidget( splitter );
    auto* rightLayout = new QVBoxLayout( groupEditor_ );
    rightLayout->setContentsMargins( 0, 0, 0, 0 );
    auto* nameLayout = new QFormLayout;
    groupName_ = new QLineEdit( groupEditor_ );
    groupName_->setObjectName( QStringLiteral( "groupName" ) );
    nameLayout->addRow( tr( "Name:" ), groupName_ );
    rightLayout->addLayout( nameLayout );

    auto* parts = new QSplitter( Qt::Vertical, groupEditor_ );
    rightLayout->addWidget( parts, 1 );

    // The Naming Rules, and the tables of the selected rule's capture groups.
    auto* rulesBox = new QGroupBox( tr( "Naming Rules" ), parts );
    auto* rulesLayout = new QHBoxLayout( rulesBox );
    auto* rulesColumn = new QVBoxLayout;
    rulesTable_ = new QTableWidget( 0, 3, rulesBox );
    rulesTable_->setObjectName( QStringLiteral( "rulesTable" ) );
    rulesTable_->setHorizontalHeaderLabels( { tr( "Name" ), tr( "Regex" ), tr( "Template" ) } );
    rulesTable_->setSelectionBehavior( QAbstractItemView::SelectRows );
    rulesTable_->setSelectionMode( QAbstractItemView::SingleSelection );
    rulesTable_->horizontalHeader()->setSectionResizeMode( RuleRegex, QHeaderView::Stretch );
    rulesTable_->verticalHeader()->hide();
    rulesColumn->addWidget( rulesTable_ );
    auto* ruleButtons = new QHBoxLayout;
    addRuleButton_ = toolButton( tr( "Add a Naming Rule" ), rulesBox );
    removeRuleButton_ = toolButton( tr( "Remove the Naming Rule" ), rulesBox );
    upRuleButton_ = toolButton( tr( "Move up" ), rulesBox );
    downRuleButton_ = toolButton( tr( "Move down" ), rulesBox );
    for ( auto* button : { addRuleButton_, removeRuleButton_, upRuleButton_, downRuleButton_ } ) {
        ruleButtons->addWidget( button );
    }
    ruleButtons->addStretch();
    rulesColumn->addLayout( ruleButtons );
    rulesLayout->addLayout( rulesColumn, 3 );

    auto* captureColumn = new QVBoxLayout;
    auto* captureLabel = new QLabel( tr( "Name Table of each capture group:" ), rulesBox );
    captureLabel->setWordWrap( true );
    captureColumn->addWidget( captureLabel );
    captureGroups_ = new QTableWidget( 0, 2, rulesBox );
    captureGroups_->setObjectName( QStringLiteral( "captureGroups" ) );
    captureGroups_->setHorizontalHeaderLabels( { tr( "Capture group" ), tr( "Name Table" ) } );
    captureGroups_->setEditTriggers( QAbstractItemView::NoEditTriggers );
    captureGroups_->horizontalHeader()->setStretchLastSection( true );
    captureGroups_->verticalHeader()->hide();
    captureColumn->addWidget( captureGroups_ );
    rulesLayout->addLayout( captureColumn, 2 );

    // The Name Tables, and the rows of the selected one.
    auto* tablesBox = new QGroupBox( tr( "Name Tables" ), parts );
    auto* tablesLayout = new QHBoxLayout( tablesBox );
    auto* tablesColumn = new QVBoxLayout;
    tablesList_ = new QListWidget( tablesBox );
    tablesList_->setObjectName( QStringLiteral( "tablesList" ) );
    tablesList_->setToolTip( tr( "Double-click a Name Table to rename it." ) );
    tablesColumn->addWidget( tablesList_ );
    auto* tableButtons = new QHBoxLayout;
    addTableButton_ = toolButton( tr( "Add a Name Table" ), tablesBox );
    removeTableButton_ = toolButton( tr( "Remove the Name Table" ), tablesBox );
    tableButtons->addWidget( addTableButton_ );
    tableButtons->addWidget( removeTableButton_ );
    tableButtons->addStretch();
    tablesColumn->addLayout( tableButtons );
    tablesLayout->addLayout( tablesColumn, 1 );

    auto* rowsColumn = new QVBoxLayout;
    caseSensitive_ = new QCheckBox( tr( "Keys are case-sensitive" ), tablesBox );
    caseSensitive_->setObjectName( QStringLiteral( "caseSensitive" ) );
    rowsColumn->addWidget( caseSensitive_ );
    rowsTable_ = new QTableWidget( 0, 2, tablesBox );
    rowsTable_->setObjectName( QStringLiteral( "rowsTable" ) );
    rowsTable_->setHorizontalHeaderLabels( { tr( "Key regex" ), tr( "Name" ) } );
    rowsTable_->horizontalHeader()->setSectionResizeMode( QHeaderView::Stretch );
    rowsTable_->setSelectionBehavior( QAbstractItemView::SelectRows );
    rowsColumn->addWidget( rowsTable_ );
    auto* rowButtons = new QHBoxLayout;
    addRowButton_ = toolButton( tr( "Add a row" ), tablesBox );
    removeRowButton_ = toolButton( tr( "Remove the selected rows" ), tablesBox );
    importCsvButton_ = textButton( tr( "Import CSV..." ), tablesBox );
    exportCsvButton_ = textButton( tr( "Export CSV..." ), tablesBox );
    pasteButton_ = textButton( tr( "Paste" ), tablesBox );
    pasteButton_->setToolTip( tr( "Adds the rows copied from a spreadsheet: key, then name." ) );
    for ( auto* button : { addRowButton_, removeRowButton_ } ) {
        rowButtons->addWidget( button );
    }
    rowButtons->addStretch();
    for ( auto* button : { importCsvButton_, exportCsvButton_, pasteButton_ } ) {
        rowButtons->addWidget( button );
    }
    rowsColumn->addLayout( rowButtons );
    tablesLayout->addLayout( rowsColumn, 3 );

    // The preview of a sample Log Line, and the warnings.
    auto* previewBox = new QGroupBox( tr( "Preview" ), parts );
    auto* previewLayout = new QVBoxLayout( previewBox );
    previewInput_ = new QLineEdit( previewBox );
    previewInput_->setObjectName( QStringLiteral( "previewInput" ) );
    previewInput_->setPlaceholderText(
        tr( "Paste a sample Log Line here: all rules of the group run on it, "
            "checked or not" ) );
    previewLayout->addWidget( previewInput_ );
    previewResult_ = new QLabel( previewBox );
    previewResult_->setObjectName( QStringLiteral( "previewResult" ) );
    previewResult_->setTextFormat( Qt::RichText );
    previewResult_->setTextInteractionFlags( Qt::TextSelectableByMouse );
    previewResult_->setWordWrap( true );
    previewLayout->addWidget( previewResult_ );
    warnings_ = new QListWidget( previewBox );
    warnings_->setObjectName( QStringLiteral( "warnings" ) );
    previewLayout->addWidget( warnings_ );

    parts->setStretchFactor( 0, 2 );
    parts->setStretchFactor( 1, 3 );
    parts->setStretchFactor( 2, 1 );
    splitter->setStretchFactor( 0, 0 );
    splitter->setStretchFactor( 1, 1 );

    buttonBox_ = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel | QDialogButtonBox::Apply, this );
    mainLayout->addWidget( buttonBox_ );

    connect( addGroupButton_, &QToolButton::clicked, this, &ValueNamesDialog::addGroup );
    connect( removeGroupButton_, &QToolButton::clicked, this, &ValueNamesDialog::removeGroup );
    connect( upGroupButton_, &QToolButton::clicked, this, &ValueNamesDialog::moveGroupUp );
    connect( downGroupButton_, &QToolButton::clicked, this, &ValueNamesDialog::moveGroupDown );
    connect( groupList_, &QListWidget::currentRowChanged, this, &ValueNamesDialog::groupSelected );
    connect( groupName_, &QLineEdit::textEdited, this, &ValueNamesDialog::groupRenamed );
    connect( groupName_, &QLineEdit::editingFinished, this, &ValueNamesDialog::groupNameFinished );

    connect( addRuleButton_, &QToolButton::clicked, this, &ValueNamesDialog::addRule );
    connect( removeRuleButton_, &QToolButton::clicked, this, &ValueNamesDialog::removeRule );
    connect( upRuleButton_, &QToolButton::clicked, this, &ValueNamesDialog::moveRuleUp );
    connect( downRuleButton_, &QToolButton::clicked, this, &ValueNamesDialog::moveRuleDown );
    connect( rulesTable_, &QTableWidget::itemSelectionChanged, this,
             &ValueNamesDialog::ruleSelected );
    connect( rulesTable_, &QTableWidget::itemChanged, this, &ValueNamesDialog::ruleEdited );

    connect( addTableButton_, &QToolButton::clicked, this, &ValueNamesDialog::addTable );
    connect( removeTableButton_, &QToolButton::clicked, this, &ValueNamesDialog::removeTable );
    connect( tablesList_, &QListWidget::currentRowChanged, this, &ValueNamesDialog::tableSelected );
    connect( tablesList_, &QListWidget::itemChanged, this, &ValueNamesDialog::tableRenamed );
    connect( caseSensitive_, &QCheckBox::toggled, this, &ValueNamesDialog::caseSensitivityChanged );
    connect( addRowButton_, &QToolButton::clicked, this, &ValueNamesDialog::addRow );
    connect( removeRowButton_, &QToolButton::clicked, this, &ValueNamesDialog::removeRows );
    connect( rowsTable_, &QTableWidget::itemChanged, this, &ValueNamesDialog::rowEdited );
    connect( importCsvButton_, &QToolButton::clicked, this, &ValueNamesDialog::importCsv );
    connect( exportCsvButton_, &QToolButton::clicked, this, &ValueNamesDialog::exportCsv );
    connect( pasteButton_, &QToolButton::clicked, this, &ValueNamesDialog::paste );

    connect( previewInput_, &QLineEdit::textChanged, this, &ValueNamesDialog::showPreviewLine );
    previewInput_->installEventFilter( this );
    connect( buttonBox_, &QDialogButtonBox::clicked, this, &ValueNamesDialog::resolveDialog );

    // A copy: OK and Apply hand it back, Cancel drops it.
    groups_ = ValueNamesCollection::getSynced().ownGroups();
    populateGroups( groups_.isEmpty() ? -1 : 0 );

    loadIcons();
    Theme::whenApplied( this, [ this ] { loadIcons(); } );
}

void ValueNamesDialog::loadIcons()
{
    loadListEditIcons( addGroupButton_, removeGroupButton_, upGroupButton_, downGroupButton_ );
    loadListEditIcons( addRuleButton_, removeRuleButton_, upRuleButton_, downRuleButton_ );
    // The tables and rows are only added and removed.
    QToolButton unusedUp;
    QToolButton unusedDown;
    loadListEditIcons( addTableButton_, removeTableButton_, &unusedUp, &unusedDown );
    loadListEditIcons( addRowButton_, removeRowButton_, &unusedUp, &unusedDown );
}

// --- What is selected ---

NamingGroup* ValueNamesDialog::currentGroup()
{
    return groupRow_ >= 0 && groupRow_ < groups_.size() ? &groups_[ groupRow_ ] : nullptr;
}

const NamingGroup* ValueNamesDialog::currentGroup() const
{
    return groupRow_ >= 0 && groupRow_ < groups_.size() ? &groups_[ groupRow_ ] : nullptr;
}

const NameTable* ValueNamesDialog::currentTable() const
{
    const auto* group = currentGroup();
    if ( group == nullptr || tableRow_ < 0 || tableRow_ >= group->tables().size() ) {
        return nullptr;
    }
    return &group->tables()[ tableRow_ ];
}

void ValueNamesDialog::changeRules( const std::function<void( QList<NamingRule>& )>& change )
{
    auto* group = currentGroup();
    if ( group == nullptr ) {
        return;
    }
    auto rules = group->rules();
    change( rules );
    group->setRules( rules );
}

void ValueNamesDialog::changeTables( const std::function<void( QList<NameTable>& )>& change )
{
    auto* group = currentGroup();
    if ( group == nullptr ) {
        return;
    }
    auto tables = group->tables();
    change( tables );
    group->setTables( tables );
}

// --- Naming Groups ---

void ValueNamesDialog::populateGroups( int selectRow )
{
    updating_ = true;
    groupList_->clear();
    for ( const auto& group : groups_ ) {
        groupList_->addItem( group.name() );
    }
    groupList_->setCurrentRow( selectRow );
    updating_ = false;
    groupSelected();
}

void ValueNamesDialog::groupSelected()
{
    if ( updating_ ) {
        return;
    }
    groupRow_ = groupList_->currentRow();
    csvWarnings_.clear();

    const auto* group = currentGroup();
    groupEditor_->setEnabled( group != nullptr );
    groupName_->setText( group != nullptr ? group->name() : QString{} );
    populateRules( group != nullptr && !group->rules().isEmpty() ? 0 : -1 );
    populateTables( group != nullptr && !group->tables().isEmpty() ? 0 : -1 );
    updateButtons();
    updatePreview();
}

void ValueNamesDialog::groupRenamed( const QString& name )
{
    auto* group = currentGroup();
    if ( group == nullptr ) {
        return;
    }
    group->setName( name );
    groupList_->item( groupRow_ )->setText( name );
}

void ValueNamesDialog::groupNameFinished()
{
    makeGroupNamesUnique();
    if ( const auto* group = currentGroup();
         group != nullptr && groupName_->text() != group->name() ) {
        groupName_->setText( group->name() );
    }
}

void ValueNamesDialog::makeGroupNamesUnique()
{
    QStringList taken;
    for ( int row = 0; row < groups_.size(); ++row ) {
        auto& group = groups_[ row ];
        auto name = group.name().trimmed();
        if ( name.isEmpty() ) {
            name = tr( "New Naming Group" );
        }
        name = logsquirl::groupexchange::firstFreeName( name, taken );
        taken.append( name );
        if ( name != group.name() ) {
            group.setName( name );
            groupList_->item( row )->setText( name );
        }
    }
}

void ValueNamesDialog::addGroup()
{
    QStringList taken;
    for ( const auto& group : groups_ ) {
        taken.append( group.name() );
    }
    groups_.append( NamingGroup::createNewGroup(
        logsquirl::groupexchange::firstFreeName( tr( "New Naming Group" ), taken ) ) );
    populateGroups( static_cast<int>( groups_.size() ) - 1 );
}

void ValueNamesDialog::removeGroup()
{
    if ( currentGroup() == nullptr ) {
        return;
    }
    const auto row = groupRow_;
    groups_.removeAt( row );
    populateGroups( std::min( row, static_cast<int>( groups_.size() ) - 1 ) );
}

void ValueNamesDialog::moveGroupUp()
{
    if ( groupRow_ <= 0 ) {
        return;
    }
    const auto row = groupRow_;
    groups_.move( row, row - 1 );
    populateGroups( row - 1 );
}

void ValueNamesDialog::moveGroupDown()
{
    if ( groupRow_ < 0 || groupRow_ >= groups_.size() - 1 ) {
        return;
    }
    const auto row = groupRow_;
    groups_.move( row, row + 1 );
    populateGroups( row + 1 );
}

// --- Naming Rules ---

void ValueNamesDialog::populateRules( int selectRow )
{
    updating_ = true;
    const auto* group = currentGroup();
    const auto rules = group != nullptr ? group->rules() : QList<NamingRule>{};
    rulesTable_->setRowCount( static_cast<int>( rules.size() ) );
    for ( int row = 0; row < rules.size(); ++row ) {
        const auto& rule = rules[ row ];
        rulesTable_->setItem( row, RuleName, new QTableWidgetItem( rule.name ) );
        rulesTable_->setItem( row, RuleRegex, new QTableWidgetItem( rule.pattern ) );
        rulesTable_->setItem( row, RuleTemplate, new QTableWidgetItem( rule.displayTemplate ) );
    }
    rulesTable_->clearSelection();
    if ( selectRow >= 0 ) {
        rulesTable_->selectRow( selectRow );
    }
    updating_ = false;

    ruleRow_ = selectRow;
    populateCaptureGroups();
    updateButtons();
}

void ValueNamesDialog::ruleSelected()
{
    if ( updating_ ) {
        return;
    }
    const auto selected = rulesTable_->selectionModel()->selectedRows();
    ruleRow_ = selected.isEmpty() ? -1 : selected.first().row();
    populateCaptureGroups();
    updateButtons();
}

void ValueNamesDialog::ruleEdited( QTableWidgetItem* item )
{
    if ( updating_ || item == nullptr ) {
        return;
    }
    const auto row = item->row();
    auto text = item->text();
    bool patternChanged = false;

    changeRules( [ & ]( QList<NamingRule>& rules ) {
        if ( row < 0 || row >= rules.size() ) {
            return;
        }
        auto& rule = rules[ row ];
        switch ( item->column() ) {
        case RuleName:
            // Its name is what the rule's check is kept by: unique in the group.
            text = text.trimmed();
            if ( text.isEmpty() ) {
                text = tr( "Rule" );
            }
            text = logsquirl::groupexchange::firstFreeName( text, ruleNames( rules, row ) );
            rule.name = text;
            break;
        case RuleRegex:
            patternChanged = rule.pattern != text;
            rule.groupTables = adaptedGroupTables( rule.groupTables, rule.pattern, text );
            rule.pattern = text;
            break;
        case RuleTemplate:
            if ( text.isEmpty() ) {
                text = NamingRule::defaultTemplate();
            }
            rule.displayTemplate = text;
            break;
        default:
            break;
        }
    } );

    if ( text != item->text() ) {
        updating_ = true;
        item->setText( text );
        updating_ = false;
    }
    if ( patternChanged && row == ruleRow_ ) {
        populateCaptureGroups();
    }
    updatePreview();
}

void ValueNamesDialog::addRule()
{
    auto* group = currentGroup();
    if ( group == nullptr ) {
        return;
    }
    changeRules( []( QList<NamingRule>& rules ) {
        NamingRule rule;
        rule.name = logsquirl::groupexchange::firstFreeName( tr( "Rule" ), ruleNames( rules, -1 ) );
        rules.append( rule );
    } );
    const auto row = static_cast<int>( group->rules().size() ) - 1;
    populateRules( row );
    rulesTable_->editItem( rulesTable_->item( row, RuleRegex ) );
    updatePreview();
}

void ValueNamesDialog::removeRule()
{
    const auto* group = currentGroup();
    if ( group == nullptr || ruleRow_ < 0 || ruleRow_ >= group->rules().size() ) {
        return;
    }
    const auto row = ruleRow_;
    changeRules( [ row ]( QList<NamingRule>& rules ) { rules.removeAt( row ); } );
    populateRules( std::min( row, static_cast<int>( group->rules().size() ) - 1 ) );
    updatePreview();
}

void ValueNamesDialog::moveRuleUp()
{
    if ( ruleRow_ <= 0 ) {
        return;
    }
    const auto row = ruleRow_;
    changeRules( [ row ]( QList<NamingRule>& rules ) { rules.move( row, row - 1 ); } );
    populateRules( row - 1 );
    updatePreview();
}

void ValueNamesDialog::moveRuleDown()
{
    const auto* group = currentGroup();
    if ( group == nullptr || ruleRow_ < 0 || ruleRow_ >= group->rules().size() - 1 ) {
        return;
    }
    const auto row = ruleRow_;
    changeRules( [ row ]( QList<NamingRule>& rules ) { rules.move( row, row + 1 ); } );
    populateRules( row + 1 );
    updatePreview();
}

void ValueNamesDialog::populateCaptureGroups()
{
    captureGroups_->setRowCount( 0 );
    const auto* group = currentGroup();
    if ( group == nullptr || ruleRow_ < 0 || ruleRow_ >= group->rules().size() ) {
        return;
    }
    const auto rule = group->rules()[ ruleRow_ ];
    const QRegularExpression regex( rule.pattern );
    if ( !regex.isValid() ) {
        return;
    }

    struct CaptureGroup {
        QString label;
        // What the rule names it by: its name if it has one, else its number.
        QString key;
        // Its number, when it is named.
        QString numbered;
    };
    QList<CaptureGroup> captureGroups;
    const auto count = regex.captureCount();
    if ( count == 0 ) {
        captureGroups.append( { tr( "whole match" ), QStringLiteral( "0" ), {} } );
    }
    const auto names = regex.namedCaptureGroups();
    for ( int number = 1; number <= count; ++number ) {
        const auto name = names.value( number );
        const auto numberText = QString::number( number );
        if ( name.isEmpty() ) {
            captureGroups.append( { numberText, numberText, {} } );
        }
        else {
            captureGroups.append(
                { QStringLiteral( "%1 (%2)" ).arg( numberText, name ), name, numberText } );
        }
    }

    captureGroups_->setRowCount( static_cast<int>( captureGroups.size() ) );
    for ( int row = 0; row < captureGroups.size(); ++row ) {
        const auto& captureGroup = captureGroups[ row ];
        captureGroups_->setItem( row, 0, new QTableWidgetItem( captureGroup.label ) );

        auto* combo = new QComboBox( captureGroups_ );
        combo->setObjectName( QStringLiteral( "captureGroupTable" ) );
        combo->addItem( tr( "(none)" ), QString{} );
        for ( const auto& table : group->tables() ) {
            combo->addItem( table.name, table.name );
        }
        auto chosen = rule.tableFor( captureGroup.key );
        if ( chosen.isEmpty() && !captureGroup.numbered.isEmpty() ) {
            chosen = rule.tableFor( captureGroup.numbered );
        }
        auto index = combo->findData( chosen );
        if ( index < 0 && !chosen.isEmpty() ) {
            // A table the group does not have: shown, so that it is not lost
            // unseen. The warnings tell of it.
            combo->addItem( chosen, chosen );
            index = combo->count() - 1;
        }
        combo->setCurrentIndex( std::max( index, 0 ) );
        connect( combo, &QComboBox::currentIndexChanged, this,
                 [ this, combo, key = captureGroup.key, numbered = captureGroup.numbered ] {
                     captureGroupTableChosen( key, numbered, combo->currentData().toString() );
                 } );
        captureGroups_->setCellWidget( row, 1, combo );
    }
}

void ValueNamesDialog::captureGroupTableChosen( const QString& group, const QString& numbered,
                                                const QString& table )
{
    const auto row = ruleRow_;
    changeRules( [ & ]( QList<NamingRule>& rules ) {
        if ( row < 0 || row >= rules.size() ) {
            return;
        }
        auto& groupTables = rules[ row ].groupTables;
        groupTables.removeIf( [ & ]( const GroupTable& groupTable ) {
            return groupTable.group == group
                   || ( !numbered.isEmpty() && groupTable.group == numbered );
        } );
        if ( !table.isEmpty() ) {
            groupTables.append( GroupTable{ group, table } );
        }
    } );
    updatePreview();
}

// --- Name Tables ---

void ValueNamesDialog::populateTables( int selectRow )
{
    updating_ = true;
    tablesList_->clear();
    if ( const auto* group = currentGroup() ) {
        for ( const auto& table : group->tables() ) {
            auto* item = new QListWidgetItem( table.name, tablesList_ );
            item->setFlags( item->flags() | Qt::ItemIsEditable );
        }
    }
    tablesList_->setCurrentRow( selectRow );
    updating_ = false;

    tableRow_ = selectRow;
    populateRows();
    updateButtons();
}

void ValueNamesDialog::tableSelected()
{
    if ( updating_ ) {
        return;
    }
    tableRow_ = tablesList_->currentRow();
    csvWarnings_.clear();
    populateRows();
    updateButtons();
    updatePreview();
}

void ValueNamesDialog::tableRenamed( QListWidgetItem* item )
{
    if ( updating_ || item == nullptr ) {
        return;
    }
    const auto row = tablesList_->row( item );
    const auto* group = currentGroup();
    if ( group == nullptr || row < 0 || row >= group->tables().size() ) {
        return;
    }

    // Unique in the group: the rules find their tables by name.
    auto name = item->text().trimmed();
    if ( name.isEmpty() ) {
        name = tr( "Table" );
    }
    name = logsquirl::groupexchange::firstFreeName( name, tableNames( group->tables(), row ) );
    const auto oldName = group->tables()[ row ].name;

    changeTables( [ & ]( QList<NameTable>& tables ) { tables[ row ].name = name; } );
    // The rules that used the table use it under its new name.
    changeRules( [ & ]( QList<NamingRule>& rules ) {
        for ( auto& rule : rules ) {
            for ( auto& groupTable : rule.groupTables ) {
                if ( groupTable.table == oldName ) {
                    groupTable.table = name;
                }
            }
        }
    } );

    if ( name != item->text() ) {
        updating_ = true;
        item->setText( name );
        updating_ = false;
    }
    populateCaptureGroups();
    updatePreview();
}

void ValueNamesDialog::addTable()
{
    const auto* group = currentGroup();
    if ( group == nullptr ) {
        return;
    }
    changeTables( []( QList<NameTable>& tables ) {
        NameTable table;
        table.name
            = logsquirl::groupexchange::firstFreeName( tr( "Table" ), tableNames( tables, -1 ) );
        tables.append( table );
    } );
    const auto row = static_cast<int>( group->tables().size() ) - 1;
    populateTables( row );
    populateCaptureGroups();
    tablesList_->editItem( tablesList_->item( row ) );
    updatePreview();
}

void ValueNamesDialog::removeTable()
{
    const auto* group = currentGroup();
    if ( currentTable() == nullptr ) {
        return;
    }
    const auto row = tableRow_;
    const auto removedName = currentTable()->name;
    changeTables( [ row ]( QList<NameTable>& tables ) { tables.removeAt( row ); } );
    // The rules that used it use no table there any more, as a rename
    // follows into them.
    changeRules( [ & ]( QList<NamingRule>& rules ) {
        for ( auto& rule : rules ) {
            rule.groupTables.removeIf(
                [ & ]( const GroupTable& groupTable ) { return groupTable.table == removedName; } );
        }
    } );
    populateTables( std::min( row, static_cast<int>( group->tables().size() ) - 1 ) );
    populateCaptureGroups();
    updatePreview();
}

void ValueNamesDialog::caseSensitivityChanged( bool caseSensitive )
{
    if ( updating_ || currentTable() == nullptr ) {
        return;
    }
    const auto row = tableRow_;
    changeTables( [ row, caseSensitive ]( QList<NameTable>& tables ) {
        tables[ row ].caseSensitive = caseSensitive;
    } );
    updatePreview();
}

void ValueNamesDialog::populateRows()
{
    updating_ = true;
    const auto* table = currentTable();
    const auto rows = table != nullptr ? table->rows : QList<NameRow>{};
    rowsTable_->setRowCount( static_cast<int>( rows.size() ) );
    for ( int row = 0; row < rows.size(); ++row ) {
        rowsTable_->setItem( row, RowKey, new QTableWidgetItem( rows[ row ].key ) );
        rowsTable_->setItem( row, RowName, new QTableWidgetItem( rows[ row ].name ) );
    }
    caseSensitive_->setChecked( table != nullptr && table->caseSensitive );
    caseSensitive_->setEnabled( table != nullptr );
    rowsTable_->setEnabled( table != nullptr );
    updating_ = false;
}

void ValueNamesDialog::rowEdited( QTableWidgetItem* item )
{
    if ( updating_ || item == nullptr || currentTable() == nullptr ) {
        return;
    }
    const auto tableRow = tableRow_;
    const auto row = item->row();
    const auto column = item->column();
    const auto text = item->text();
    changeTables( [ & ]( QList<NameTable>& tables ) {
        auto& rows = tables[ tableRow ].rows;
        if ( row < 0 || row >= rows.size() ) {
            return;
        }
        if ( column == RowKey ) {
            rows[ row ].key = text;
        }
        else {
            rows[ row ].name = text;
        }
    } );
    updatePreview();
}

void ValueNamesDialog::addRow()
{
    const auto* table = currentTable();
    if ( table == nullptr ) {
        return;
    }
    const auto tableRow = tableRow_;
    changeTables(
        [ tableRow ]( QList<NameTable>& tables ) { tables[ tableRow ].rows.append( NameRow{} ); } );
    populateRows();
    const auto row = rowsTable_->rowCount() - 1;
    rowsTable_->selectRow( row );
    rowsTable_->editItem( rowsTable_->item( row, RowKey ) );
}

void ValueNamesDialog::removeRows()
{
    if ( currentTable() == nullptr ) {
        return;
    }
    QList<int> rows;
    for ( const auto& index : rowsTable_->selectionModel()->selectedRows() ) {
        rows.append( index.row() );
    }
    std::sort( rows.begin(), rows.end(), std::greater<>() );
    const auto tableRow = tableRow_;
    changeTables( [ & ]( QList<NameTable>& tables ) {
        for ( const auto row : rows ) {
            tables[ tableRow ].rows.removeAt( row );
        }
    } );
    populateRows();
    updatePreview();
}

void ValueNamesDialog::importCsvText( const QString& text, const CsvImportOptions& options,
                                      CsvImportTarget target, const QString& newTableName )
{
    const auto* group = currentGroup();
    if ( group == nullptr ) {
        return;
    }
    const auto imported = logsquirl::valuenames::importCsv( text, options );

    if ( target == CsvImportTarget::NewTable || currentTable() == nullptr ) {
        changeTables( [ & ]( QList<NameTable>& tables ) {
            NameTable table;
            table.name = logsquirl::groupexchange::firstFreeName(
                newTableName.isEmpty() ? tr( "Table" ) : newTableName, tableNames( tables, -1 ) );
            table.caseSensitive = options.caseSensitive;
            table.rows = imported.rows;
            tables.append( table );
        } );
        populateTables( static_cast<int>( group->tables().size() ) - 1 );
        populateCaptureGroups();
    }
    else {
        const auto tableRow = tableRow_;
        const bool append = target == CsvImportTarget::AppendRows;
        changeTables( [ & ]( QList<NameTable>& tables ) {
            auto& rows = tables[ tableRow ].rows;
            if ( !append ) {
                rows.clear();
            }
            rows.append( imported.rows );
        } );
        populateRows();
    }
    showCsvWarnings( imported.warnings );
    updatePreview();
}

QString ValueNamesDialog::exportCsvText() const
{
    const auto* table = currentTable();
    return table != nullptr ? logsquirl::valuenames::exportCsv( table->rows ) : QString{};
}

void ValueNamesDialog::pasteRows( const QString& text )
{
    const auto* group = currentGroup();
    if ( group == nullptr ) {
        return;
    }
    if ( currentTable() == nullptr ) {
        changeTables( []( QList<NameTable>& tables ) {
            NameTable table;
            table.name = logsquirl::groupexchange::firstFreeName( tr( "Table" ),
                                                                  tableNames( tables, -1 ) );
            tables.append( table );
        } );
        populateTables( static_cast<int>( group->tables().size() ) - 1 );
        populateCaptureGroups();
    }

    CsvImportOptions options;
    options.caseSensitive = currentTable()->caseSensitive;
    const auto imported = logsquirl::valuenames::importCsv( text, options );
    const auto tableRow = tableRow_;
    changeTables(
        [ & ]( QList<NameTable>& tables ) { tables[ tableRow ].rows.append( imported.rows ); } );
    populateRows();
    showCsvWarnings( imported.warnings );
    updatePreview();
}

void ValueNamesDialog::importCsv()
{
    if ( currentGroup() == nullptr ) {
        return;
    }
    const auto file = QFileDialog::getOpenFileName(
        this, tr( "Import CSV" ), {}, tr( "CSV files (*.csv *.tsv *.txt);;All files (*)" ) );
    if ( file.isEmpty() ) {
        return;
    }
    QFile input( file );
    if ( !input.open( QIODevice::ReadOnly ) ) {
        QMessageBox::warning( this, tr( "Import CSV" ),
                              tr( "The file %1 could not be read." ).arg( file ) );
        return;
    }
    const auto text = decodeCsvFile( input.readAll() );

    const auto* table = currentTable();
    NameTableCsvImportDialog options( text, table != nullptr && table->caseSensitive,
                                      table != nullptr ? table->name : QString{}, this );
    if ( options.exec() != QDialog::Accepted ) {
        return;
    }
    importCsvText( text, options.options(), options.target(),
                   QFileInfo( file ).completeBaseName() );
}

void ValueNamesDialog::exportCsv()
{
    const auto* table = currentTable();
    if ( table == nullptr ) {
        return;
    }
    const auto file = QFileDialog::getSaveFileName( this, tr( "Export CSV" ),
                                                    table->name + QStringLiteral( ".csv" ),
                                                    tr( "CSV files (*.csv);;All files (*)" ) );
    if ( file.isEmpty() ) {
        return;
    }
    QFile output( file );
    if ( !output.open( QIODevice::WriteOnly | QIODevice::Truncate )
         || output.write( exportCsvText().toUtf8() ) < 0 ) {
        QMessageBox::warning( this, tr( "Export CSV" ),
                              tr( "The file %1 could not be written." ).arg( file ) );
    }
}

void ValueNamesDialog::paste()
{
    pasteRows( QGuiApplication::clipboard()->text() );
}

void ValueNamesDialog::showCsvWarnings( const QList<CsvImportWarning>& warnings )
{
    csvWarnings_.clear();
    for ( const auto& warning : warnings ) {
        csvWarnings_.append( csvWarningMessage( warning ) );
    }
}

// --- Preview and warnings ---

void ValueNamesDialog::updatePreview()
{
    warnings_->clear();
    const auto* group = currentGroup();
    if ( group == nullptr ) {
        previewNamer_ = {};
        showPreviewLine();
        return;
    }

    for ( const auto& problem : logsquirl::valuenames::validate( *group ) ) {
        warnings_->addItem( problemMessage( problem ) );
    }
    warnings_->addItems( csvWarnings_ );

    // Every rule of the group, checked or not: the preview shows what the
    // group does.
    auto previewed = *group;
    previewed.setEnabled( true );
    auto rules = previewed.rules();
    for ( auto& rule : rules ) {
        rule.enabled = true;
    }
    previewed.setRules( rules );
    previewNamer_ = logsquirl::valuenames::ValueNamer( { previewed } );

    showPreviewLine();
}

void ValueNamesDialog::showPreviewLine()
{
    const auto sample = previewInput_->text();
    if ( sample.isEmpty() || currentGroup() == nullptr ) {
        shownPreview_ = sample;
        previewResult_->clear();
        return;
    }

    const auto namedValues = previewNamer_.namedValues( sample );
    shownPreview_ = logsquirl::valuenames::shownLine( sample, namedValues );

    // The Named Values underlined, as the views mark them.
    QString html = QStringLiteral( "<span style=\"white-space: pre-wrap\">" );
    qsizetype position = 0;
    for ( const auto& namedValue : namedValues ) {
        html += sample.mid( position, namedValue.start - position ).toHtmlEscaped();
        html += QStringLiteral( "<u>" ) + namedValue.shown.toHtmlEscaped()
                + QStringLiteral( "</u>" );
        position = namedValue.end();
    }
    html += sample.mid( position ).toHtmlEscaped() + QStringLiteral( "</span>" );
    previewResult_->setText( html );
}

bool ValueNamesDialog::eventFilter( QObject* watched, QEvent* event )
{
    // Enter in the sample Log Line is no OK.
    if ( watched == previewInput_ && event->type() == QEvent::KeyPress ) {
        const auto key = static_cast<QKeyEvent*>( event )->key();
        if ( key == Qt::Key_Return || key == Qt::Key_Enter ) {
            return true;
        }
    }
    return QDialog::eventFilter( watched, event );
}

void ValueNamesDialog::updateButtons()
{
    const auto* group = currentGroup();
    const auto groupCount = static_cast<int>( groups_.size() );
    removeGroupButton_->setEnabled( group != nullptr );
    upGroupButton_->setEnabled( group != nullptr && groupRow_ > 0 );
    downGroupButton_->setEnabled( group != nullptr && groupRow_ < groupCount - 1 );

    const auto ruleCount = group != nullptr ? static_cast<int>( group->rules().size() ) : 0;
    const bool ruleSelected = ruleRow_ >= 0 && ruleRow_ < ruleCount;
    removeRuleButton_->setEnabled( ruleSelected );
    upRuleButton_->setEnabled( ruleSelected && ruleRow_ > 0 );
    downRuleButton_->setEnabled( ruleSelected && ruleRow_ < ruleCount - 1 );

    const bool tableSelected = currentTable() != nullptr;
    removeTableButton_->setEnabled( tableSelected );
    addRowButton_->setEnabled( tableSelected );
    removeRowButton_->setEnabled( tableSelected );
    exportCsvButton_->setEnabled( tableSelected );
}

// --- OK / Apply / Cancel ---

void ValueNamesDialog::resolveDialog( QAbstractButton* button )
{
    const auto role = buttonBox_->buttonRole( button );
    if ( role == QDialogButtonBox::RejectRole ) {
        reject();
        return;
    }
    if ( role != QDialogButtonBox::AcceptRole && role != QDialogButtonBox::ApplyRole ) {
        return;
    }

    groupNameFinished();
    auto& collection = ValueNamesCollection::get();
    const bool changed = collection.setGroups( groups_ );
    collection.save();
    if ( changed ) {
        Q_EMIT valueNamesChanged();
    }

    if ( role == QDialogButtonBox::AcceptRole ) {
        accept();
    }
}
