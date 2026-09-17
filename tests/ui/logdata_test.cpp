/*
 * Copyright (C) 2016 -- 2019 Anton Filimonov and other contributors
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

#include <catch2/catch.hpp>

#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>

#include <QFileInfo>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTest>
#include <QTextCodec>
#include <QThread>

#include "file_write_helper.h"
#include "log.h"
#include "test_policies.h"
#include "test_utils.h"

#include "logdata.h"

static const qint64 SL_NB_LINES = 500LL;
static const qint64 VBL_NB_LINES = 50000LL;

namespace {

class WriteFileThread : public QThread {
    Q_OBJECT
public:
    WriteFileThread( QFile* file, int numberOfLines = 200,
                     WriteFileModification flag = WriteFileModification::None )
        : file_{ file }
        , numberOfLines_{ numberOfLines }
        , flag_{ flag }
    {
    }

    bool isSucceeded() const
    {
        return result_ == 0;
    }

protected:
    void run() override
    {
        QString writeHelper = QCoreApplication::applicationDirPath() + QDir::separator()
                              + QLatin1String( "file_write_helper" );
        QStringList arguments;
        arguments << file_->fileName() << QString::number( numberOfLines_ )
                  << QString::number( static_cast<uint8_t>( flag_ ) );

        LOG_INFO << "Executing write helper " << writeHelper << " " << arguments;
        QProcess writeHelperProcess;
        writeHelperProcess.start( writeHelper, arguments );
        writeHelperProcess.waitForFinished( -1 );
        result_ = writeHelperProcess.exitCode();
        LOG_INFO << "Write helper result " << result_ << ", exit status "
                 << writeHelperProcess.exitStatus();
    }

private:
    QFile* file_;
    int numberOfLines_;
    WriteFileModification flag_;

    int result_{};
};

} // namespace

// Include MOC outside anonymous namespace to avoid Qt 6.10 q20::identity resolution conflict
#include "logdata_test.moc"

namespace {

#ifdef _WIN32
// Calls whenWritten, on context's thread, once the file has been written.
void writeDataToFileBackground( QFile& file, int numberOfLines, WriteFileModification flag,
                                QObject* context, std::function<void()> whenWritten )
{
    auto thread = new WriteFileThread( &file, numberOfLines, flag );
    QObject::connect( thread, &WriteFileThread::finished, context, std::move( whenWritten ) );
    QObject::connect( thread, &WriteFileThread::finished, thread, &WriteFileThread::deleteLater );
    thread->start();
}
#endif
void writeDataToFile( QFile& file, int numberOfLines = 200,
                      WriteFileModification flag = WriteFileModification::None )
{
    auto thread = new WriteFileThread( &file, numberOfLines, flag );
    thread->start();
    thread->wait();
    REQUIRE( thread->isSucceeded() );
    thread->deleteLater();
}
} // namespace

TEST_CASE( "Logdata decoding lines", "[logdata]" )
{
    QTemporaryFile file{ "testdecode_XXXXXX" };
    if ( file.open() ) {
        writeDataToFile( file );
    }

    writeDataToFile( file, 199, WriteFileModification::EndWithPartialLineBegin );

    const auto policies = testSettingsPolicies();
    LogData logData{ policies.indexing, policies.search, policies.fileAccess, policies.decoding };

    auto finishedSpy
        = std::make_unique<SafeQSignalSpy>( &logData, SIGNAL( loadingFinished( LoadingStatus ) ) );

    logData.attachFile( QFileInfo{ file }.absoluteFilePath() );

    REQUIRE( finishedSpy->safeWait() );
    REQUIRE( finishedSpy->count() == 1 );
    REQUIRE( logData.getNbLine() == 400_lcount );

    const auto rawLines = logData.getLinesRaw( 200_lnum, 200_lcount );
    REQUIRE( rawLines.startLine == 200_lnum );
    REQUIRE( rawLines.endOfLines.size() == 200 );

    const auto utf8View = rawLines.buildUtf8View();

    REQUIRE( rawLines.endOfLines.size() == utf8View.size() );
}

TEST_CASE( "Logdata reading changing file", "[logdata]" )
{
    // The log data watches nothing itself (#249): whoever follows the Log
    // File tells it of a change on disk, and here the test does, once it has
    // changed the file.
    const auto policies = testSettingsPolicies();
    LogData logData{ policies.indexing, policies.search, policies.fileAccess, policies.decoding };

    SafeQSignalSpy changedSpy( &logData, SIGNAL( fileChanged( MonitoredFileStatus ) ) );

    // Generate a small file
    QTemporaryFile file{ "testdecode_XXXXXX" };
    if ( file.open() ) {
        writeDataToFile( file );
    }

    SafeQSignalSpy finishedSpy( &logData, SIGNAL( loadingFinished( LoadingStatus ) ) );
    // Start loading it
    const auto path = QFileInfo{ file }.absoluteFilePath();
    logData.attachFile( path );
    waitUiState( [ &logData ] { return logData.getNbLine() == 200_lcount; } );
    REQUIRE( finishedSpy.safeWait() );
    REQUIRE( finishedSpy.count() == 1 );

    // Check we have the small file
    REQUIRE( logData.getNbLine() == 200_lcount );
    REQUIRE( logData.getMaxLength() == LineLength( SL_LINE_LENGTH ) );
    REQUIRE( logData.getFileSize() == 200 * ( SL_LINE_LENGTH + 1LL ) );

    // Add some data to it
    if ( file.isOpen() ) {
        // To test the edge case when the final line is not complete
#ifdef Q_OS_WIN
        writeDataToFileBackground( file, 200, WriteFileModification::EndWithPartialLineBegin,
                                   &logData, [ & ] { logData.fileChangedOnDisk( path ); } );
#else
        writeDataToFile( file, 200, WriteFileModification::EndWithPartialLineBegin );
        logData.fileChangedOnDisk( path );
#endif
    }

    waitUiState( [ &logData ] { return logData.getNbLine() == 401_lcount; } );

    // Check we have a bigger file
    REQUIRE( changedSpy.count() >= 1 );
    REQUIRE( logData.getNbLine() == 401_lcount );
    REQUIRE( logData.getMaxLength() == LineLength( SL_LINE_LENGTH ) );
    REQUIRE( logData.getFileSize()
             == (qint64)( 400 * ( SL_LINE_LENGTH + 1LL ) + strlen( partial_line_begin ) ) );

    {
        // Add a couple more lines, including the end of the unfinished one.
        if ( file.isOpen() ) {
#ifdef Q_OS_WIN
            writeDataToFileBackground( file, 20, WriteFileModification::StartWithPartialLineEnd,
                                       &logData, [ & ] { logData.fileChangedOnDisk( path ); } );
#else
            writeDataToFile( file, 20, WriteFileModification::StartWithPartialLineEnd );
            logData.fileChangedOnDisk( path );
#endif
        }

        waitUiState( [ &logData ] { return logData.getNbLine() == 421_lcount; } );

        // Check we have a bigger file
        REQUIRE( changedSpy.count() >= 2 );
        REQUIRE( logData.getNbLine() == 421_lcount );
        REQUIRE( logData.getMaxLength() == LineLength( SL_LINE_LENGTH ) );
        REQUIRE( logData.getFileSize()
                 == (qint64)( 420 * ( SL_LINE_LENGTH + 1LL ) + strlen( partial_line_begin )
                              + strlen( partial_line_end ) ) );
    }

    {
        // Truncate the file
        writeDataToFile( file, 0, WriteFileModification::Truncate );
        logData.fileChangedOnDisk( path );

        waitUiState( [ &logData ] { return logData.getNbLine() == 0_lcount; } );

        // Check we have an empty file
        REQUIRE( changedSpy.count() >= 3 );
        REQUIRE( logData.getNbLine() == 0_lcount );
        REQUIRE( logData.getMaxLength().get() == 0 );
        REQUIRE( logData.getFileSize() == 0LL );
    }
}

SCENARIO( "Attaching log data to files", "[logdata]" )
{

    GIVEN( "Small and big files" )
    {

        QTemporaryFile smallFile{ "logdata_test_small_XXXXXX" };
        QTemporaryFile bigFile{ "logdata_test_big_XXXXXX" };

        if ( smallFile.open() ) {
            writeDataToFile( smallFile, SL_NB_LINES );
        }

        if ( bigFile.open() ) {
            writeDataToFile( bigFile, VBL_NB_LINES );
        }

        WHEN( "Interrupt loading" )
        {
            const auto policies = testSettingsPolicies();
            LogData log_data{ policies.indexing, policies.search, policies.fileAccess,
                              policies.decoding };
            SafeQSignalSpy endSpy( &log_data, SIGNAL( loadingFinished( LoadingStatus ) ) );

            // Start loading the VBL
            log_data.attachFile( QFileInfo{ bigFile }.absoluteFilePath() );

            // Immediately interrupt the loading
            log_data.interruptLoading();

            REQUIRE( endSpy.safeWait( 10000 ) );

            THEN( "No file is attached" )
            {
                // Check we have an empty file
                REQUIRE( endSpy.count() == 1 );
                QList<QVariant> arguments = endSpy.takeFirst();
                REQUIRE( arguments.at( 0 ).toInt()
                         == static_cast<int>( LoadingStatus::Interrupted ) );

                REQUIRE( log_data.getNbLine() == 0_lcount );
                REQUIRE( log_data.getMaxLength().get() == 0 );
                REQUIRE( log_data.getFileSize() == 0LL );
            }
        }

        WHEN( "Try to reattach" )
        {
            const auto policies = testSettingsPolicies();
            LogData log_data{ policies.indexing, policies.search, policies.fileAccess,
                              policies.decoding };
            SafeQSignalSpy endSpy( &log_data, SIGNAL( loadingFinished( LoadingStatus ) ) );

            log_data.attachFile( QFileInfo{ smallFile }.absoluteFilePath() );
            endSpy.safeWait( 10000 );

            THEN( "Throws" )
            {
                CHECK_THROWS_AS( log_data.attachFile( QFileInfo{ bigFile }.absoluteFilePath() ),
                                 CantReattachErr );
            }
        }
    }
}

namespace {

// An Encoding the indexing fails on: asked for its name by any thread but the
// one that made it, it throws. That thread is the one a test runs on, so
// only the indexing, which runs on a thread of its own, fails.
class UnusableEncoding : public QTextCodec {
public:
    ~UnusableEncoding() override = default;

    QByteArray name() const override
    {
        if ( QThread::currentThread() != owner_ ) {
            throw std::runtime_error( "the Encoding cannot be used" );
        }
        return "LogSquirl-Unusable-Encoding";
    }

    int mibEnum() const override
    {
        return -4242;
    }

protected:
    QString convertToUnicode( const char* in, int length, ConverterState* ) const override
    {
        return QString::fromLatin1( in, length );
    }

    QByteArray convertFromUnicode( const QChar* in, int length, ConverterState* ) const override
    {
        return QString( in, length ).toLatin1();
    }

private:
    const QThread* owner_ = QThread::currentThread();
};

} // namespace

SCENARIO( "A Log File that fails to index reports the failure as its loading status", "[logdata]" )
{
    QTemporaryFile file{ "logdata_test_failure_XXXXXX" };
    if ( file.open() ) {
        writeDataToFile( file, SL_NB_LINES );
    }

    // Made only once the Log File has loaded, and gone only once the Log
    // File is: while it exists, every look-up of an Encoding by name asks it.
    std::unique_ptr<UnusableEncoding> unusableEncoding;

    const auto policies = testSettingsPolicies();
    LogData logData{ policies.indexing, policies.search, policies.fileAccess, policies.decoding };

    GIVEN( "a loaded Log File" )
    {
        SafeQSignalSpy endSpy( &logData, SIGNAL( loadingFinished( LoadingStatus, QString ) ) );
        logData.attachFile( QFileInfo{ file }.absoluteFilePath() );
        REQUIRE( endSpy.safeWait( 10000 ) );
        REQUIRE( logData.getNbLine() == LinesCount( SL_NB_LINES ) );
        endSpy.clear();

        WHEN( "it is reloaded with an Encoding the indexing fails on" )
        {
            unusableEncoding = std::make_unique<UnusableEncoding>();
            logData.reload( unusableEncoding.get() );

            REQUIRE( endSpy.safeWait( 10000 ) );

            THEN( "loading finishes Failed, with a description, and no dialog" )
            {
                REQUIRE( endSpy.count() == 1 );
                const auto arguments = endSpy.takeFirst();
                REQUIRE( arguments.at( 0 ).value<LoadingStatus>() == LoadingStatus::Failed );
                REQUIRE( arguments.at( 1 ).toString().contains( "the Encoding cannot be used" ) );
            }

            THEN( "the Index it had is dropped" )
            {
                REQUIRE( logData.getNbLine() == 0_lcount );
            }
        }
    }
}

namespace {

// Lines of about 1 KiB each, numbered from first.
QByteArray numberedLines( int first, int count )
{
    QByteArray content;
    const QByteArray padding( 1000, 'x' );
    for ( int i = first; i < first + count; ++i ) {
        content += "line " + QByteArray::number( i ) + " " + padding + "\n";
    }
    return content;
}

void writeBytes( const QString& path, const QByteArray& content, QIODevice::OpenMode mode )
{
    QFile file( path );
    REQUIRE( file.open( QIODevice::WriteOnly | mode ) );
    REQUIRE( file.write( content ) == content.size() );
}

} // namespace

SCENARIO( "A followed Log File that changed where it is not checked is read again on a reload",
          "[logdata][follow]" )
{
    QTemporaryDir logDir;
    REQUIRE( logDir.isValid() );
    const auto path = logDir.filePath( "followed.log" );

    // About 11 MiB: more than the header and tail digests cover.
    const auto content = numberedLines( 0, 11000 );
    writeBytes( path, content, QIODevice::Truncate );

    const auto policies = testSettingsPolicies();
    LogData logData{ policies.indexing, policies.search, policies.fileAccess, policies.decoding };

    SafeQSignalSpy changedSpy( &logData, SIGNAL( fileChanged( MonitoredFileStatus, QString ) ) );
    SafeQSignalSpy endSpy( &logData, SIGNAL( loadingFinished( LoadingStatus, QString ) ) );
    logData.attachFile( path );
    REQUIRE( endSpy.safeWait( 60000 ) );
    REQUIRE( logData.getNbLine() == 11000_lcount );
    endSpy.clear();

    GIVEN( "a line between the header and the tail split in two in place, and lines appended" )
    {
        // Same size: a newline takes the place of one padding byte.
        auto changed = content;
        const auto splitAt = changed.indexOf( "line 6000 " ) + 20;
        changed[ splitAt ] = '\n';
        writeBytes( path, changed + numberedLines( 11000, 10 ), QIODevice::Truncate );

        logData.fileChangedOnDisk( path );
        REQUIRE( endSpy.safeWait( 60000 ) );
        endSpy.clear();

        THEN( "following it takes the change for an append" )
        {
            REQUIRE( changedSpy.count() == 1 );
            REQUIRE( changedSpy.at( 0 ).at( 0 ).value<MonitoredFileStatus>()
                     == MonitoredFileStatus::DataAdded );
            REQUIRE( logData.getNbLine() == 11010_lcount );
        }

        WHEN( "it is reloaded" )
        {
            logData.reload();
            REQUIRE( endSpy.safeWait( 60000 ) );

            THEN( "all of it is indexed again, the split line too" )
            {
                REQUIRE( logData.getNbLine() == 11011_lcount );
                REQUIRE( logData.getLineString( 6000_lnum ) == "line 6000 " + QString( 10, 'x' ) );
                REQUIRE( logData.getLineString( 11010_lnum ).startsWith( "line 11009 " ) );
            }
        }
    }

    GIVEN( "it replaced by a larger Log File with another header" )
    {
        writeBytes( path, "another header\n" + numberedLines( 0, 11100 ), QIODevice::Truncate );

        logData.fileChangedOnDisk( path );
        REQUIRE( endSpy.safeWait( 60000 ) );

        THEN( "following it tells it was truncated and indexes it again" )
        {
            REQUIRE( changedSpy.count() == 1 );
            REQUIRE( changedSpy.at( 0 ).at( 0 ).value<MonitoredFileStatus>()
                     == MonitoredFileStatus::Truncated );
            REQUIRE( logData.getNbLine() == 11101_lcount );
            REQUIRE( logData.getLineString( 0_lnum ) == "another header" );
        }
    }
}
