/*
 * Copyright (C) 2020, 2021 Anton Filimonov and other contributors
 *
 * This file is part of logsquirl.
 *
 * logsquirl is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * logsquirl is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with logsquirl.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "crashhandler.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTimer>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <qthreadpool.h>
#include <string_view>
#include <utility>
#include <vector>

#ifdef LOGSQUIRL_USE_MIMALLOC
#include <mimalloc.h>
#endif

#include "client/crash_report_database.h"
#include "sentry.h"

#include "cpu_info.h"
#include "crashreports.h"
#include "issuereporter.h"
#include "log.h"
#include "logsquirl_version.h"
#include "memory_info.h"
#include "openfilehelper.h"

namespace {

constexpr const char* DSN
    = "https://1efd12558459df096d7774f048927775@o4511077090459648.ingest.de.sentry.io/"
      "4511083891916880";

QString sentryDatabasePath()
{
#ifdef LOGSQUIRL_PORTABLE
    auto basePath = QCoreApplication::applicationDirPath();
#else
    auto basePath = QStandardPaths::writableLocation( QStandardPaths::AppDataLocation );
#endif

    return crashDumpDirectory( basePath );
}

void logSentry( sentry_level_t level, const char* message, va_list args, void* userdata )
{
#if defined __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wformat-nonliteral"
#elif defined __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-nonliteral"
#endif

    Q_UNUSED( userdata );
    QString formattedMessage;
    switch ( level ) {
    case SENTRY_LEVEL_WARNING:
        qWarning( message, args );
        break;
    case SENTRY_LEVEL_ERROR:
        qCritical( message, args );
        break;
    default:
        qInfo( message, args );
        break;
    }

#if defined __clang__
#pragma clang diagnostic pop
#elif defined __GNUC__
#pragma GCC diagnostic pop
#endif
}

QDialog::DialogCode askUserConfirmation( const QString& formattedReport, const QString& reportPath )
{
    auto message = std::make_unique<QLabel>();
    message->setText(
        CrashHandler::tr( "LogSquirl encountered an unexpected error during its last run." ) );

    auto crashReportHeader = std::make_unique<QLabel>();
    crashReportHeader->setText( CrashHandler::tr( "We collected the following crash report:" ) );

    auto report = std::make_unique<QPlainTextEdit>();
    report->setReadOnly( true );
    report->setPlainText( formattedReport );
    report->setSizePolicy( QSizePolicy::Expanding, QSizePolicy::Expanding );

    auto sendReportLabel = std::make_unique<QLabel>();
    sendReportLabel->setText(
        CrashHandler::tr( "LogSquirl can send this report to sentry.io so that the "
                          "developers can analyze and fix the issue." ) );

    auto privacyPolicy = std::make_unique<QLabel>();
    privacyPolicy->setText(
        QString(
            "<a href=\"https://github.com/64x-lunicorn/LogSquirl/blob/master/SECURITY.md\">%1</a>" )
            .arg( CrashHandler::tr( "Privacy policy" ) ) );

    privacyPolicy->setTextFormat( Qt::RichText );
    privacyPolicy->setTextInteractionFlags( Qt::TextBrowserInteraction );
    privacyPolicy->setOpenExternalLinks( true );

    auto exploreButton = std::make_unique<QPushButton>();
    exploreButton->setText( CrashHandler::tr( "Open report directory" ) );
    exploreButton->setFlat( true );
    QObject::connect( exploreButton.get(), &QPushButton::clicked,
                      [ &reportPath ] { showPathInFileExplorer( reportPath ); } );

    auto privacyLayout = std::make_unique<QHBoxLayout>();
    privacyLayout->addWidget( privacyPolicy.release() );
    privacyLayout->addStretch();
    privacyLayout->addWidget( exploreButton.release() );

    auto buttonBox = std::make_unique<QDialogButtonBox>();
    buttonBox->addButton( CrashHandler::tr( "Send report" ), QDialogButtonBox::AcceptRole );
    buttonBox->addButton( CrashHandler::tr( "Discard report" ), QDialogButtonBox::RejectRole );

    auto confirmationDialog = std::make_unique<QDialog>();
    confirmationDialog->resize( 800, 600 );

    QObject::connect( buttonBox.get(), &QDialogButtonBox::accepted, confirmationDialog.get(),
                      &QDialog::accept );
    QObject::connect( buttonBox.get(), &QDialogButtonBox::rejected, confirmationDialog.get(),
                      &QDialog::reject );

    auto layout = std::make_unique<QVBoxLayout>();
    layout->addWidget( message.release() );
    layout->addWidget( crashReportHeader.release() );
    layout->addWidget( report.release() );
    layout->addWidget( sendReportLabel.release() );
    layout->addLayout( privacyLayout.release() );
    layout->addWidget( buttonBox.release() );

    confirmationDialog->setLayout( layout.release() );

    return static_cast<QDialog::DialogCode>( confirmationDialog->exec() );
}

bool checkCrashpadReports( const QString& databasePath )
{
    using namespace crashpad;

#ifdef Q_OS_WIN
    auto database = CrashReportDatabase::InitializeWithoutCreating(
        base::FilePath{ databasePath.toStdWString() } );
#else
    auto database = CrashReportDatabase::InitializeWithoutCreating(
        base::FilePath{ databasePath.toStdString() } );
#endif

    std::vector<CrashReportDatabase::Report> completedReports;
    database->GetCompletedReports( &completedReports );
    LOG_INFO << "Pending reports " << completedReports.size();

    std::vector<PendingCrashReport> pendingReports;
    pendingReports.reserve( completedReports.size() );
    for ( const auto& completed : completedReports ) {
#ifdef Q_OS_WIN
        auto dumpFile = QString::fromStdWString( completed.file_path.value() );
#else
        auto dumpFile = QString::fromStdString( completed.file_path.value() );
#endif
        pendingReports.push_back( { QString::fromStdString( completed.uuid.ToString() ),
                                    std::move( dumpFile ), completed.uploaded } );
    }

    const auto stackwalker = crashpadToolPath( QCoreApplication::applicationDirPath(),
                                               QStringLiteral( "logsquirl_minidump_dump" ) );

    CrashReportActions actions;
    actions.symbolize = [ &stackwalker ]( const QString& dumpFile ) {
        QProcess stackProcess;
        stackProcess.start( stackwalker, QStringList() << dumpFile );
        stackProcess.waitForFinished();
        return QString::fromUtf8( stackProcess.readAllStandardOutput() );
    };
    actions.askUser = []( const QString& formattedReport, const QString& dumpFile ) {
        return QDialog::Accepted == askUserConfirmation( formattedReport, dumpFile )
                   ? CrashReportChoice::Send
                   : CrashReportChoice::Discard;
    };
    actions.send = [ &database, &completedReports ]( std::size_t index ) {
        database->RequestUpload( completedReports[ index ].uuid );
    };
    actions.discard = [ &database, &completedReports ]( std::size_t index ) {
        database->DeleteReport( completedReports[ index ].uuid );
    };
    actions.offerIssue = []( const QString& crashId ) {
        IssueReporter::askUserAndReportIssue( IssueTemplate::Crash, crashId );
    };

    return handlePendingCrashReports( pendingReports, actions );
}
} // namespace

CrashHandler::CrashHandler()
{
    const auto dumpPath = sentryDatabasePath();
    const auto hasDumpDir = QDir{ dumpPath }.mkpath( "." );

    const auto needWaitForUpload = hasDumpDir ? checkCrashpadReports( dumpPath ) : false;

    sentry_options_t* sentryOptions = sentry_options_new();

    sentry_options_set_logger( sentryOptions, logSentry, nullptr );
#ifndef NDEBUG
    // Sentry's own debug output only in debug builds: in a release it adds
    // log work at startup and on every report for no one to read.
    sentry_options_set_debug( sentryOptions, 1 );
#endif

    const auto handlerPath = crashpadToolPath( QCoreApplication::applicationDirPath(),
                                               QStringLiteral( "logsquirl_crashpad_handler" ) );
#ifdef Q_OS_WIN
    sentry_options_set_database_pathw( sentryOptions, dumpPath.toStdWString().c_str() );
    sentry_options_set_handler_pathw( sentryOptions, handlerPath.toStdWString().c_str() );
#else
    sentry_options_set_database_path( sentryOptions, dumpPath.toStdString().c_str() );
    sentry_options_set_handler_path( sentryOptions, handlerPath.toStdString().c_str() );
#endif

    sentry_options_set_dsn( sentryOptions, DSN );

    // logsquirl asks confirmation and sends reports using crashpad
    sentry_options_set_require_user_consent( sentryOptions, true );

    sentry_options_set_auto_session_tracking( sentryOptions, false );

    sentry_options_set_symbolize_stacktraces( sentryOptions, true );

    sentry_options_set_environment( sentryOptions, "development" );
    sentry_options_set_release( sentryOptions, logsquirlVersion().data() );

    sentry_init( sentryOptions );

    sentry_set_tag( "commit", logsquirlCommit().data() );
    sentry_set_tag( "qt", qVersion() );
    sentry_set_tag( "build_arch", QSysInfo::buildCpuArchitecture().toLatin1().data() );

    auto addExtra = []( const char* name, auto value ) {
        sentry_set_extra( name, sentry_value_new_string( std::to_string( value ).c_str() ) );
        LOG_INFO << "Process stats: " << name << " - " << value;
    };

    addExtra( "memory", physicalMemory() );

    addExtra( "cpuInstructions", static_cast<unsigned>( supportedCpuInstructions() ) );
    addExtra( "concurrency", QThreadPool::globalInstance()->maxThreadCount() );

    memoryUsageTimer_ = std::make_unique<QTimer>();
    QObject::connect( memoryUsageTimer_.get(), &QTimer::timeout, [ addExtra ]() {
        const auto vmUsed = usedMemory();
        addExtra( "vm_used", vmUsed );

#ifdef LOGSQUIRL_USE_MIMALLOC
        size_t elapsedMsecs, userMsecs, systemMsecs, currentRss, peakRss, currentCommit, peakCommit,
            pageFaults;

        mi_process_info( &elapsedMsecs, &userMsecs, &systemMsecs, &currentRss, &peakRss,
                         &currentCommit, &peakCommit, &pageFaults );

        addExtra( "elapsed_msecs", elapsedMsecs );
        addExtra( "user_msecs", userMsecs );
        addExtra( "system_msecs", systemMsecs );
        addExtra( "current_rss", currentRss );
        addExtra( "peak_rss", peakRss );
        addExtra( "current_commit", currentCommit );
        addExtra( "peak_commit", peakCommit );
        addExtra( "page_faults", pageFaults );
#endif
    } );
    memoryUsageTimer_->start( 10000 );

    if ( needWaitForUpload ) {
        QProgressDialog progressDialog;
        progressDialog.setLabelText( CrashHandler::tr( "Uploading crash reports" ) );
        progressDialog.setRange( 0, 0 );

        QTimer::singleShot( 30 * 1000, &progressDialog, &QProgressDialog::cancel );
        progressDialog.exec();
    }
}

CrashHandler::~CrashHandler()
{
    memoryUsageTimer_->stop();
    sentry_shutdown();
}
