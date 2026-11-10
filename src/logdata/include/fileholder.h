#ifndef FILEHOLDER_H
#define FILEHOLDER_H

#include <QFile>
#include <memory>

#include "synchronization.h"

struct FileId {
    uint64_t fileIndex = 0;
    uint64_t volumeIndex = 0;

    bool operator!=( const FileId& other ) const
    {
        return fileIndex != other.fileIndex || volumeIndex != other.volumeIndex;
    }

    static FileId getFileId( const QString& filename );
};

// The bytes of a Log File as its Log Lines are read from them (LogLineReader):
// a FileHolder holds those of a Log File on disk, a test may hold some in
// memory instead.
class LogFileBytes {
public:
    LogFileBytes() = default;
    virtual ~LogFileBytes() = default;

    LogFileBytes( const LogFileBytes& ) = delete;
    LogFileBytes& operator=( const LogFileBytes& ) = delete;
    LogFileBytes( LogFileBytes&& ) = delete;
    LogFileBytes& operator=( LogFileBytes&& ) = delete;

    // Bracket one read of Log Lines: the bytes stay readable -- a Log File
    // kept closed is opened -- and no other read of them runs, from
    // beginRead() until the matching endRead().
    virtual void beginRead() = 0;
    virtual void endRead() = 0;

    // Reads up to size bytes at offset into out, between beginRead() and
    // endRead(): how many were read, fewer than size at the end of the
    // bytes, or -1 when offset cannot be reached, and nothing is read then
    // (#845).
    virtual qint64 readAt( qint64 offset, char* out, qint64 size ) = 0;
};

template <typename T>
class ScopedFileHolder {
public:
    explicit ScopedFileHolder( T* file )
        : file_holder_( file )
    {
        file_holder_->lock();
        file_holder_->attachReader();
    }

    ~ScopedFileHolder()
    {
        file_holder_->unlock();
        file_holder_->detachReader();
    }

    QFile* getFile()
    {
        return file_holder_->getFile();
    }

private:
    ScopedFileHolder( const ScopedFileHolder& ) = delete;
    ScopedFileHolder& operator=( const ScopedFileHolder& ) = delete;

    T* file_holder_;
};

class FileHolder final : public LogFileBytes {
    friend class ScopedFileHolder<FileHolder>;

public:
    explicit FileHolder( bool keepClosed );
    ~FileHolder() override;
    FileId getFileId();
    qint64 size();

    bool isOpen();

    void open( const QString& fileName );

    void lock();
    void unlock();

    void attachReader();
    void detachReader();

    void reOpenFile();

    // Lock the Log File and attach a reader for one read of Log Lines, as a
    // ScopedFileHolder does.
    void beginRead() override;
    void endRead() override;
    qint64 readAt( qint64 offset, char* out, qint64 size ) override;

private:
    QFile* getFile();

private:
    RecursiveMutex file_mutex_;

    QString file_name_;
    std::unique_ptr<QFile> attached_file_;
    FileId attached_file_id_;

    uint32_t counter_ = 0;
    bool keep_closed_ = false;
};

#endif // FILEHOLDER_H
