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

#pragma once

#include <deque>
#include <functional>
#include <memory>
#include <optional>

#include <QFutureWatcher>
#include <QObject>
#include <QString>
#include <QStringList>

class AtomicFlag;

// Where a decompressed Log File came from: the archive the user opened, and
// the member taken from it at every level (#596). A compressed single file
// (.gz, .xz, ...) holds one member, named by an empty string; an archive
// holding several (.zip, .tar, ...) names the one the user picked by its path
// inside it. An archive inside an archive adds a level:
//
//   app.log.gz           {"app.log.gz", {""}}
//   logs.zip, a/app.log  {"logs.zip", {"a/app.log"}}
//   logs.zip, app.log.gz {"logs.zip", {"app.log.gz", ""}}
//
// A Log File opened from its own path has none. The temporary file a
// decompressed Log File is read from is gone by the next start, so the Session
// saves this instead and decompresses the archive again.
struct ArchiveMember {
    QString archive;
    QStringList members;

    bool isEmpty() const
    {
        return archive.isEmpty();
    }

    // The member `member` of what this names, one level down: an empty
    // `member` for the one file of a compressed single file. A Log File with
    // no archive is its own archive.
    ArchiveMember inside( const QString& fileName, const QString& member ) const
    {
        auto nested = isEmpty() ? ArchiveMember{ fileName, {} } : *this;
        nested.members.append( member );
        return nested;
    }

    bool operator==( const ArchiveMember& other ) const = default;
};

// Decompresses the archive again, level by level, into a new directory or
// file inside `directory`, and returns the path of the member there. Returns
// an empty string when the archive is gone, a member is not in it, or it
// cannot be decompressed. Blocks until it is done.
QString decompressArchiveMember( const ArchiveMember& member, const QString& directory );

// The same, which stops early and returns an empty string once `interrupt` is
// set.
QString decompressArchiveMember( const ArchiveMember& member, const QString& directory,
                                 AtomicFlag& interrupt );

// Decompresses archive members again one after another, away from the thread
// that asks, so that a window restoring its Session stays usable while a
// large archive decompresses (#610). Each request is answered on the asking
// thread, in the order asked, with the path of its member inside `directory`,
// or an empty string when it cannot be decompressed or was cancelled.
//
// What is still queued or decompressing when this goes, or is cancelled with
// cancelAll(), is interrupted and never answered; the destructor waits for
// the decompression under way, so nothing is written into `directory` after.
class ArchiveMemberDecompression : public QObject {
    Q_OBJECT
public:
    using Answer = std::function<void( const QString& fileName )>;

    explicit ArchiveMemberDecompression( QString directory, QObject* parent = nullptr );
    ~ArchiveMemberDecompression() override;

    ArchiveMemberDecompression( const ArchiveMemberDecompression& ) = delete;
    ArchiveMemberDecompression& operator=( const ArchiveMemberDecompression& ) = delete;

    void decompress( const ArchiveMember& member, Answer answer );

    // Interrupts the member decompressing now, which is answered with an
    // empty string; the next one starts.
    void cancelCurrent();

    // Interrupts the member decompressing now and forgets the queued ones;
    // none of them is answered.
    void cancelAll();

    // Whether no member is queued or decompressing.
    bool isIdle() const;

Q_SIGNALS:
    // A member of `archive` starts decompressing.
    void decompressing( const QString& archive );
    // The last member queued is answered, or cancelAll() was called.
    void idle();

private:
    void startNext();
    void finishCurrent();

    struct Request {
        ArchiveMember member;
        Answer answer;
    };

    QString directory_;
    std::deque<Request> queued_;
    // The request decompressing now; its answer is empty once cancelled.
    std::optional<Request> current_;
    // Shared with the thread decompressing, which may outlive a request.
    std::shared_ptr<AtomicFlag> interrupt_;
    QFutureWatcher<QString> watcher_;
};
