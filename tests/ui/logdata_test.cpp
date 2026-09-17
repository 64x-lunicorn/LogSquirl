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

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

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

SCENARIO( "A Log File read hiding ANSI color sequences", "[logdata][ansi]" )
{
    constexpr int LineCount = 300;

    // Every third Log Line is colored; the others have no escape character.
    const auto logLine = []( int line, bool colored ) {
        return colored ? QStringLiteral( "\x1B[3%1mline\x1B[0m %2" ).arg( line % 8 ).arg( line )
                       : QStringLiteral( "line %1" ).arg( line );
    };

    QTemporaryFile file{ "logdata_test_ansi_XXXXXX" };
    REQUIRE( file.open() );
    for ( int line = 0; line < LineCount; ++line ) {
        file.write( logLine( line, line % 3 == 0 ).toUtf8() + '\n' );
    }
    file.flush();

    auto policies = testSettingsPolicies();
    policies.decoding.hideAnsiColorSequences = true;
    LogData logData{ policies.indexing, policies.search, policies.fileAccess, policies.decoding };
    {
        SafeQSignalSpy loadEndSpy( &logData, SIGNAL( loadingFinished( LoadingStatus ) ) );
        logData.attachFile( file.fileName() );
        REQUIRE( loadEndSpy.safeWait( 10000 ) );
    }
    REQUIRE( logData.getNbLine() == LinesCount( LineCount ) );

    GIVEN( "Log Lines with and without ANSI color sequences" )
    {
        THEN( "a block of them reads without any sequence" )
        {
            const auto lines = logData.getLines( 0_lnum, LinesCount( LineCount ) );
            REQUIRE( lines.size() == LineCount );
            for ( int line = 0; line < LineCount; ++line ) {
                REQUIRE( lines[ static_cast<std::size_t>( line ) ] == logLine( line, false ) );
            }
        }

        THEN( "several readers, one Log Line at a time, all read them without any sequence" )
        {
            constexpr int ReaderCount = 4;
            std::vector<int> correct( ReaderCount, 0 );
            {
                std::vector<std::jthread> readers;
                for ( int reader = 0; reader < ReaderCount; ++reader ) {
                    readers.emplace_back( [ &, reader ] {
                        for ( int line = 0; line < LineCount; ++line ) {
                            if ( logData.getLineString(
                                     LineNumber( static_cast<uint64_t>( line ) ) )
                                 == logLine( line, false ) ) {
                                ++correct[ static_cast<std::size_t>( reader ) ];
                            }
                        }
                    } );
                }
            }
            REQUIRE( correct == std::vector<int>( ReaderCount, LineCount ) );
        }
    }
}

namespace {

// What reading each Log Line on its own returns, plain or with tabs expanded.
std::vector<QString> linesOneByOne( const LogData& logData, const std::vector<LineNumber>& lines,
                                    bool expanded )
{
    std::vector<QString> text;
    for ( const auto line : lines ) {
        text.push_back( expanded ? logData.getExpandedLineString( line )
                                 : logData.getLineString( line ) );
    }
    return text;
}

std::vector<QString> asStd( const logsquirl::vector<QString>& lines )
{
    return { lines.begin(), lines.end() };
}

} // namespace

SCENARIO( "A sparse set of Log Lines reads as each of them does on its own",
          "[logdata][sparse-read]" )
{
    constexpr int LineCount = 2000;

    // Log Lines of different lengths, some with tabs, a carriage return, an
    // ANSI color sequence or characters outside ASCII, some empty, and a last
    // one without a line feed.
    const auto logLine = []( int line ) -> QByteArray {
        switch ( line % 7 ) {
        case 0:
            return QStringLiteral( "%1\tcolumn\tafter tabs" ).arg( line ).toUtf8();
        case 1:
            return QStringLiteral( "%1 ends with a carriage return\r" ).arg( line ).toUtf8();
        case 2:
            return {};
        case 3:
            return QStringLiteral( "\x1B[31m%1 colored\x1B[0m" ).arg( line ).toUtf8();
        case 4:
            return QStringLiteral( "%1 grüße ☃" ).arg( line ).toUtf8();
        default:
            return QStringLiteral( "%1 %2" ).arg( line ).arg( QString( line % 50, 'x' ) ).toUtf8();
        }
    };

    QTemporaryFile file{ "logdata_test_sparse_XXXXXX" };
    REQUIRE( file.open() );
    for ( int line = 0; line < LineCount; ++line ) {
        file.write( logLine( line ) );
        if ( line + 1 < LineCount ) {
            file.write( "\n" );
        }
    }
    file.flush();

    const auto hideAnsiColorSequences = GENERATE( false, true );
    auto policies = testSettingsPolicies();
    policies.decoding.hideAnsiColorSequences = hideAnsiColorSequences;
    LogData logData{ policies.indexing, policies.search, policies.fileAccess, policies.decoding };
    {
        SafeQSignalSpy loadEndSpy( &logData, SIGNAL( loadingFinished( LoadingStatus ) ) );
        logData.attachFile( file.fileName() );
        REQUIRE( loadEndSpy.safeWait( 10000 ) );
    }
    REQUIRE( logData.getNbLine() == LinesCount( LineCount ) );

    const auto expanded = GENERATE( false, true );
    const auto readSparse = [ &logData, expanded ]( const std::vector<LineNumber>& lines ) {
        return asStd( expanded ? logData.getExpandedLinesSparse( lines )
                               : logData.getLinesSparse( lines ) );
    };

    CAPTURE( hideAnsiColorSequences, expanded );

    GIVEN( "a Log File of Log Lines of every kind" )
    {
        THEN( "contiguous Log Lines read as each does on its own" )
        {
            std::vector<LineNumber> lines;
            for ( uint64_t line = 100; line < 150; ++line ) {
                lines.emplace_back( line );
            }
            REQUIRE( readSparse( lines ) == linesOneByOne( logData, lines, expanded ) );
        }

        THEN( "sparse Log Lines, near and far apart, read as each does on its own" )
        {
            std::vector<LineNumber> lines{ 1_lnum,    2_lnum,    5_lnum,   40_lnum,
                                           41_lnum,   700_lnum,  703_lnum, 1500_lnum,
                                           1501_lnum, 1502_lnum, 1777_lnum };
            REQUIRE( readSparse( lines ) == linesOneByOne( logData, lines, expanded ) );
        }

        THEN( "every tenth Log Line reads as each does on its own" )
        {
            std::vector<LineNumber> lines;
            for ( uint64_t line = 3; line < LineCount; line += 10 ) {
                lines.emplace_back( line );
            }
            REQUIRE( readSparse( lines ) == linesOneByOne( logData, lines, expanded ) );
        }

        THEN( "the first and the last Log Line read as each does on its own" )
        {
            const std::vector<LineNumber> lines{ 0_lnum, LineNumber( LineCount - 1 ) };
            const auto text = readSparse( lines );
            REQUIRE( text == linesOneByOne( logData, lines, expanded ) );
            REQUIRE( text.back().startsWith( QString::number( LineCount - 1 ) ) );
        }

        THEN( "Log Lines past the last one read as each does on its own" )
        {
            const std::vector<LineNumber> lines{ LineNumber( LineCount - 2 ),
                                                 LineNumber( LineCount ),
                                                 LineNumber( LineCount + 100 ) };
            REQUIRE( readSparse( lines ) == linesOneByOne( logData, lines, expanded ) );
        }

        THEN( "Log Lines asked for out of order or twice come back in the order asked" )
        {
            const std::vector<LineNumber> lines{ 900_lnum, 3_lnum, 900_lnum, 4_lnum };
            REQUIRE( readSparse( lines ) == linesOneByOne( logData, lines, expanded ) );
        }

        THEN( "no Log Lines read as nothing" )
        {
            REQUIRE( readSparse( {} ).empty() );
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

namespace {

// What the command line tool printed for each Log Line before it read them as
// UTF-8: the Log Line's text, converted to UTF-8, and a line feed.
std::string utf8OneByOne( const LogData& logData, const std::vector<LineNumber>& lines )
{
    std::string text;
    for ( const auto line : lines ) {
        text += logData.getLineString( line ).toStdString();
        text += '\n';
    }
    return text;
}

} // namespace

SCENARIO( "A sparse set of Log Lines reads as UTF-8 byte for byte as their text converted to it",
          "[logdata][sparse-read][utf8]" )
{
    // Log Lines a UTF-8 decoder may treat in a way of its own: valid text in
    // and outside ASCII, a carriage return, tabs, an ANSI color sequence, a
    // NUL, byte order marks, noncharacters, and byte sequences that are not
    // UTF-8 -- overlong, a surrogate, past U+10FFFF, a lone continuation byte
    // and a sequence cut short at the end of the Log Line.
    const std::vector<QByteArray> kinds{
        QByteArray( "\xEF\xBB\xBF"
                    "starts with a byte order mark" ),
        QByteArray( "plain ASCII text" ),
        QByteArray( "ends with a carriage return\r" ),
        QByteArray( "\tcolumn\tafter tabs" ),
        QByteArray(),
        QByteArray( "\x1B[31mcolored\x1B[0m" ),
        QByteArray( "a NUL \0 in the middle", 21 ),
        QByteArray( "gr\xC3\xBC\xC3\x9F"
                    "e \xE2\x98\x83 \xF0\x9F\x90\xBF" ),
        QByteArray( "noncharacters \xEF\xBF\xBE \xEF\xBF\xBF \xF4\x8F\xBF\xBF" ),
        QByteArray( "\xEF\xBB\xBF"
                    "a byte order mark on a later Log Line" ),
        QByteArray( "a zero width no-break space \xEF\xBB\xBF inside" ),
        QByteArray( "overlong \xC0\xAF and \xE0\x80\xAF" ),
        QByteArray( "a surrogate \xED\xA0\x80 here" ),
        QByteArray( "past U+10FFFF \xF4\x90\x80\x80 and \xF5\x80" ),
        QByteArray( "a lone continuation byte \x80 here" ),
        QByteArray( "cut short at the end \xE2\x82" ),
        QByteArray( "latin-1 \xE9t\xE9"
                    "\r" ),
    };

    constexpr int LineCount = 500;
    QTemporaryFile file{ "logdata_test_utf8_XXXXXX" };
    REQUIRE( file.open() );
    for ( int line = 0; line < LineCount; ++line ) {
        file.write( kinds[ static_cast<size_t>( line ) % kinds.size() ] );
        file.write( QByteArray::number( line ) );
        if ( line + 1 < LineCount ) {
            file.write( "\n" );
        }
    }
    file.flush();

    const auto hideAnsiColorSequences = GENERATE( false, true );
    auto policies = testSettingsPolicies();
    policies.decoding.hideAnsiColorSequences = hideAnsiColorSequences;
    LogData logData{ policies.indexing, policies.search, policies.fileAccess, policies.decoding };
    {
        SafeQSignalSpy loadEndSpy( &logData, SIGNAL( loadingFinished( LoadingStatus ) ) );
        logData.attachFile( file.fileName() );
        REQUIRE( loadEndSpy.safeWait( 10000 ) );
    }
    REQUIRE( logData.getNbLine() == LinesCount( LineCount ) );

    const std::string encoding = GENERATE( "", "UTF-8", "ISO-8859-1" );
    if ( !encoding.empty() ) {
        logData.setDisplayEncoding( encoding.c_str() );
    }

    CAPTURE( hideAnsiColorSequences, encoding );

    GIVEN( "a Log File of Log Lines of every kind" )
    {
        THEN( "every Log Line reads as its text converted to UTF-8" )
        {
            std::vector<LineNumber> lines;
            for ( uint64_t line = 0; line < LineCount; ++line ) {
                lines.emplace_back( line );
            }
            REQUIRE( logData.getUtf8LinesSparse( lines ) == utf8OneByOne( logData, lines ) );
        }

        THEN( "sparse Log Lines, out of order, twice and past the last one read the same" )
        {
            const std::vector<LineNumber> lines{ 3_lnum, 14_lnum, 15_lnum,  16_lnum,  400_lnum,
                                                 9_lnum, 9_lnum,  499_lnum, 500_lnum, 1000_lnum };
            REQUIRE( logData.getUtf8LinesSparse( lines ) == utf8OneByOne( logData, lines ) );
        }

        THEN( "no Log Lines read as nothing" )
        {
            REQUIRE( logData.getUtf8LinesSparse( {} ).empty() );
        }
    }
}

namespace {

// Log Lines of one length, each telling its own number, so that a Log Line
// read at any time can be checked against what it must read as.
constexpr int NumberedLineLength = 99;

QString numberedLogLine( std::uint64_t line )
{
    return QStringLiteral( "%1 numbered Log Line" )
        .arg( line, 10, 10, QLatin1Char( '0' ) )
        .leftJustified( NumberedLineLength, QLatin1Char( '.' ) );
}

void writeNumberedLogLines( QFile& file, std::uint64_t lineCount )
{
    QByteArray chunk;
    for ( std::uint64_t line = 0; line < lineCount; ++line ) {
        chunk += numberedLogLine( line ).toLatin1();
        chunk += '\n';
        if ( chunk.size() > 1024 * 1024 ) {
            REQUIRE( file.write( chunk ) == chunk.size() );
            chunk.clear();
        }
    }
    REQUIRE( file.write( chunk ) == chunk.size() );
    REQUIRE( file.flush() );
}

bool isReadWarning( const QString& text )
{
    return text.startsWith( QStringLiteral( "LOGSQUIRL WARNING" ) );
}

} // namespace

SCENARIO( "Log Lines read after their Log File shrank on disk, before it is indexed again",
          "[logdata][concurrent-read]" )
{
    constexpr std::uint64_t LineCount = 500;
    constexpr std::uint64_t LinesLeft = 200;

    QTemporaryFile file{ "logdata_test_shrunk_XXXXXX" };
    REQUIRE( file.open() );
    writeNumberedLogLines( file, LineCount );

    const auto policies = testSettingsPolicies();
    LogData logData{ policies.indexing, policies.search, policies.fileAccess, policies.decoding };
    {
        SafeQSignalSpy loadEndSpy( &logData, SIGNAL( loadingFinished( LoadingStatus ) ) );
        logData.attachFile( file.fileName() );
        REQUIRE( loadEndSpy.safeWait( 10000 ) );
    }
    REQUIRE( logData.getNbLine() == LinesCount( LineCount ) );

    GIVEN( "a Log File cut short after it was indexed" )
    {
        // Nobody tells the log data of the change: its Index still reaches
        // past the end of the Log File.
        REQUIRE( file.resize( static_cast<qint64>( LinesLeft * ( NumberedLineLength + 1 ) ) ) );

        THEN( "the Log Lines still in it read as before" )
        {
            const auto lines = logData.getLines( 150_lnum, 50_lcount );
            REQUIRE( lines.size() == 50 );
            for ( std::uint64_t line = 150; line < LinesLeft; ++line ) {
                REQUIRE( lines[ static_cast<std::size_t>( line - 150 ) ]
                         == numberedLogLine( line ) );
            }
        }

        THEN( "a block of Log Lines reaching past its end reads as warnings there" )
        {
            const auto lines = logData.getLines( 190_lnum, 100_lcount );
            REQUIRE( lines.size() == 100 );
            for ( std::uint64_t line = 190; line < 290; ++line ) {
                const auto& text = lines[ static_cast<std::size_t>( line - 190 ) ];
                CAPTURE( line, text.toStdString() );
                REQUIRE( ( line < LinesLeft ? text == numberedLogLine( line )
                                            : isReadWarning( text ) ) );
            }
        }

        THEN( "a Log Line past its end reads as a warning" )
        {
            REQUIRE( isReadWarning( logData.getLineString( 400_lnum ) ) );
            REQUIRE( isReadWarning( logData.getExpandedLineString( 499_lnum ) ) );
        }

        THEN( "a sparse set of Log Lines reads as warnings past its end" )
        {
            const std::vector<LineNumber> lines{ 10_lnum, 199_lnum, 200_lnum, 450_lnum };
            const auto text = asStd( logData.getLinesSparse( lines ) );
            REQUIRE( text.size() == 4 );
            REQUIRE( text[ 0 ] == numberedLogLine( 10 ) );
            REQUIRE( text[ 1 ] == numberedLogLine( 199 ) );
            REQUIRE( isReadWarning( text[ 2 ] ) );
            REQUIRE( isReadWarning( text[ 3 ] ) );
        }

        THEN( "a sparse set of Log Lines read as UTF-8 reads as warnings past its end" )
        {
            const std::vector<LineNumber> lines{ 10_lnum, 199_lnum, 200_lnum, 450_lnum };
            const auto text = QString::fromStdString( logData.getUtf8LinesSparse( lines ) );
            const auto readLines = text.split( QLatin1Char( '\n' ) );
            REQUIRE( readLines.size() == 5 );
            REQUIRE( readLines[ 0 ] == numberedLogLine( 10 ) );
            REQUIRE( readLines[ 1 ] == numberedLogLine( 199 ) );
            REQUIRE( isReadWarning( readLines[ 2 ] ) );
            REQUIRE( isReadWarning( readLines[ 3 ] ) );
            REQUIRE( readLines[ 4 ].isEmpty() );
        }

        THEN( "a Search reads no more Log Lines past its end than there are bytes for" )
        {
            const auto rawLines = logData.getLinesRaw( 150_lnum, 300_lcount );
            REQUIRE( rawLines.endOfLines.size() == 300 );
            const auto lines = rawLines.buildUtf8View();
            REQUIRE( lines.size() == LinesLeft - 150 );
            REQUIRE( QString::fromUtf8( lines.back().data(),
                                        static_cast<qsizetype>( lines.back().size() ) )
                     == numberedLogLine( LinesLeft - 1 ) );
        }
    }
}

SCENARIO( "Log Lines read while their Log File is indexed", "[logdata][concurrent-read]" )
{
    // Several indexing blocks, so that the Index is published block by block
    // while the readers read.
    constexpr std::uint64_t LineCount = 160'000;

    QTemporaryFile file{ "logdata_test_concurrent_XXXXXX" };
    REQUIRE( file.open() );
    writeNumberedLogLines( file, LineCount );

    const auto policies = testSettingsPolicies();
    LogData logData{ policies.indexing, policies.search, policies.fileAccess, policies.decoding };

    // Reads what the Index has so far until told to stop, and counts the Log
    // Lines that read as anything but what they must.
    const auto readWhileIndexed = [ &logData ]( const std::atomic<bool>& stop,
                                                std::atomic<int>& wrong, std::uint64_t seed ) {
        auto next = seed;
        while ( !stop ) {
            const auto nbLines = logData.getNbLine().get();
            static_cast<void>( logData.getMaxLength() );
            if ( nbLines == 0 ) {
                std::this_thread::yield();
                continue;
            }
            next = next * 6364136223846793005ULL + 1442695040888963407ULL;
            const auto line = ( next >> 17 ) % nbLines;

            const auto text = logData.getLineString( LineNumber( line ) );
            if ( text != numberedLogLine( line ) && !isReadWarning( text ) ) {
                ++wrong;
            }

            const auto count = std::min<std::uint64_t>( 20, nbLines - line );
            const auto block = logData.getLines( LineNumber( line ), LinesCount( count ) );
            for ( std::uint64_t offset = 0; offset < block.size(); ++offset ) {
                const auto& blockText = block[ static_cast<std::size_t>( offset ) ];
                if ( blockText != numberedLogLine( line + offset )
                     && !isReadWarning( blockText ) ) {
                    ++wrong;
                }
            }

            const std::vector<LineNumber> sparse{ LineNumber( line / 2 ), LineNumber( line ) };
            const auto sparseText = asStd( logData.getLinesSparse( sparse ) );
            for ( std::size_t request = 0; request < sparse.size(); ++request ) {
                if ( sparseText[ request ] != numberedLogLine( sparse[ request ].get() )
                     && !isReadWarning( sparseText[ request ] ) ) {
                    ++wrong;
                }
            }

            const auto utf8Lines = QString::fromStdString( logData.getUtf8LinesSparse( sparse ) )
                                       .split( QLatin1Char( '\n' ) );
            if ( utf8Lines.size() != static_cast<qsizetype>( sparse.size() + 1 ) ) {
                ++wrong;
            }
            else {
                for ( std::size_t request = 0; request < sparse.size(); ++request ) {
                    const auto& utf8Line = utf8Lines[ static_cast<qsizetype>( request ) ];
                    if ( utf8Line != numberedLogLine( sparse[ request ].get() )
                         && !isReadWarning( utf8Line ) ) {
                        ++wrong;
                    }
                }
            }
        }
    };

    GIVEN( "readers reading the Log Lines indexed so far" )
    {
        std::atomic<bool> stop{ false };
        std::atomic<int> wrong{ 0 };
        std::vector<std::jthread> readers;
        // Stops and joins the readers however the scenario ends.
        struct ReadersStopper {
            std::atomic<bool>& stop;
            std::vector<std::jthread>& readers;
            ~ReadersStopper()
            {
                stop = true;
                readers.clear();
            }
        } readersStopper{ stop, readers };

        WHEN( "the Log File is indexed" )
        {
            SafeQSignalSpy loadEndSpy( &logData, SIGNAL( loadingFinished( LoadingStatus ) ) );
            logData.attachFile( file.fileName() );
            for ( std::uint64_t reader = 0; reader < 2; ++reader ) {
                readers.emplace_back( readWhileIndexed, std::cref( stop ), std::ref( wrong ),
                                      reader + 1 );
            }
            REQUIRE( loadEndSpy.safeWait( 60000 ) );
            stop = true;
            readers.clear();

            THEN( "every Log Line read as it is in the Log File" )
            {
                REQUIRE( wrong == 0 );
                REQUIRE( logData.getNbLine() == LinesCount( LineCount ) );
                REQUIRE( logData.getLineString( LineNumber( LineCount - 1 ) )
                         == numberedLogLine( LineCount - 1 ) );
            }
        }

        WHEN( "the Log File is cut short and indexed again" )
        {
            {
                SafeQSignalSpy loadEndSpy( &logData, SIGNAL( loadingFinished( LoadingStatus ) ) );
                logData.attachFile( file.fileName() );
                REQUIRE( loadEndSpy.safeWait( 60000 ) );
            }

            SafeQSignalSpy loadEndSpy( &logData, SIGNAL( loadingFinished( LoadingStatus ) ) );
            for ( std::uint64_t reader = 0; reader < 2; ++reader ) {
                readers.emplace_back( readWhileIndexed, std::cref( stop ), std::ref( wrong ),
                                      reader + 1 );
            }
            REQUIRE( file.resize(
                static_cast<qint64>( ( LineCount / 3 ) * ( NumberedLineLength + 1 ) ) ) );
            logData.reload();
            REQUIRE( loadEndSpy.safeWait( 60000 ) );
            stop = true;
            readers.clear();

            THEN( "every Log Line read as it is in the Log File, or as a warning" )
            {
                REQUIRE( wrong == 0 );
                REQUIRE( logData.getNbLine() == LinesCount( LineCount / 3 ) );
            }
        }
    }
}
