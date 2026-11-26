// SPDX-FileCopyrightText: 2026 Tony Ayre <me@tonyayre.net>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// See desktopstash.h for the why. File layout:
//   ~/.local/state/theranch/parked.json  { "<corralId>": [ {name, origName, ino, btime, parkedAt}, … ] }
//   ~/.local/state/theranch/collected    marker: files are inside corrals right now
//   ~/.local/state/theranch/stash.lock   QLockFile for park/collect

#include "desktopstash.h"
#include "groupstore.h"

#include <KFileUtils>

#include <QDateTime>
#include <QDir>
#include <QHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

#include <csignal>
#include <cstdio>
#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

using namespace Qt::StringLiterals;

namespace
{
QString desktopDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
}

QString parkedPath()
{
    return DesktopStash::stateDir() + u"/parked.json"_s;
}

QString markerPath()
{
    return DesktopStash::stateDir() + u"/collected"_s;
}

// A file's identity: its inode number plus, where the filesystem records one, its
// birth time. The inode alone isn't enough, because filesystems such as ext4 hand a
// deleted file's inode number to the next file created; the birth time tells the
// two apart, and like the inode it survives renames and reboots. (The device
// number is deliberately not used; see collect().)
// AT_SYMLINK_NOFOLLOW: a symlink is identified as itself, not its target.
struct FileId {
    quint64 ino = 0;
    QString btime; // "seconds.nanoseconds"; empty where the filesystem has none
    bool valid = false;
};

FileId fileId(const QString &path)
{
    struct statx sx;
    if (::statx(AT_FDCWD, QFile::encodeName(path).constData(), AT_SYMLINK_NOFOLLOW, STATX_INO | STATX_BTIME, &sx) != 0) {
        return {};
    }
    FileId id;
    id.ino = quint64(sx.stx_ino);
    id.valid = true;
    if (sx.stx_mask & STATX_BTIME) {
        id.btime = QString::number(qint64(sx.stx_btime.tv_sec)) + u'.' + QString::number(quint32(sx.stx_btime.tv_nsec));
    }
    return id;
}

// rename(2) directly: works the same for files, directories and symlinks (moving the
// link itself). Always within one filesystem, since .ranch is inside ~/Desktop.
bool moveEntry(const QString &from, const QString &to)
{
    return ::rename(QFile::encodeName(from).constData(), QFile::encodeName(to).constData()) == 0;
}

// `name` if free in `dir`, otherwise KDE's usual "name (1).ext" style alternative.
QString freeName(const QString &dir, const QString &name)
{
    if (!QFileInfo::exists(dir + u'/' + name) && !QFileInfo(dir + u'/' + name).isSymLink()) {
        return name;
    }
    return KFileUtils::suggestName(QUrl::fromLocalFile(dir + u'/'), name);
}

void writeMarker()
{
    QFile marker(markerPath());
    if (marker.open(QIODevice::WriteOnly)) {
        marker.write(QByteArray::number(qint64(::getpid())));
    }
}

// The process that wrote the marker: -1 if there is no marker, 0 if it can't be read.
qint64 markerPid()
{
    QFile marker(markerPath());
    if (!marker.exists()) {
        return -1;
    }
    if (!marker.open(QIODevice::ReadOnly)) {
        return 0;
    }
    return marker.readAll().trimmed().toLongLong();
}

QJsonObject readParked()
{
    QFile f(parkedPath());
    if (!f.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(f.readAll()).object();
}

bool writeParked(const QJsonObject &o)
{
    if (o.isEmpty()) {
        QFile::remove(parkedPath());
        return true;
    }
    QSaveFile f(parkedPath()); // atomic: a crash mid-write can't corrupt the record
    if (!f.open(QIODevice::WriteOnly)) {
        return false;
    }
    f.write(QJsonDocument(o).toJson());
    return f.commit();
}

// Held for the duration of a park/collect. A stale lock left by a crashed process
// is taken over after the timeout.
class StashLock
{
public:
    StashLock()
        : m_lock(DesktopStash::stateDir() + u"/stash.lock"_s)
    {
        QDir().mkpath(DesktopStash::stateDir());
        m_lock.setStaleLockTime(30'000);
        m_locked = m_lock.tryLock(10'000);
    }
    bool locked() const { return m_locked; }

private:
    QLockFile m_lock;
    bool m_locked = false;
};
}

namespace DesktopStash
{
QString stateDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + u"/theranch"_s;
}

int park(qint64 ownerPid)
{
    StashLock lock;
    if (!lock.locked()) {
        qWarning() << "DesktopStash: could not lock, not parking";
        return 0;
    }

    // Checked under the lock, so it can't race with another instance's collect().
    if (ownerPid != 0) {
        const qint64 writer = markerPid();
        if (writer < 0 || (writer > 0 && writer != ownerPid)) {
            return -1; // already parked, or the files now belong to a newer instance
        }
    }

    // Merge into any existing record, so a park after a partial collect loses nothing.
    QJsonObject parked = readParked();
    QList<std::pair<QString, QString>> undo; // {where it is now, where it was}
    const QString desktop = desktopDir();
    const QDir root(GroupStore::ranchRoot());
    int moved = 0;

    // NoSymLinks: a symlink in .ranch isn't a corral we made, and following it would
    // empty the folder it points at onto the desktop.
    const auto corralDirs = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    for (const QString &id : corralDirs) {
        const QDir dir(root.filePath(id));
        QJsonArray entries = parked.value(id).toArray();
        // No QDir::Hidden: dotfiles (e.g. the ".directory" Dolphin writes when you
        // change a folder's view) were never icons, and would only clash with the
        // desktop's own.
        const auto files = dir.entryInfoList(QDir::AllEntries | QDir::System | QDir::NoDotAndDotDot);
        for (const QFileInfo &fi : files) {
            const QString origName = fi.fileName();
            const QString name = freeName(desktop, origName);
            const QString dest = desktop + u'/' + name;
            if (!moveEntry(fi.absoluteFilePath(), dest)) {
                qWarning() << "DesktopStash: could not park" << fi.absoluteFilePath();
                continue;
            }
            const FileId fid = fileId(dest);
            entries.append(QJsonObject{
                {u"name"_s, name},          // as it sits on the desktop now
                {u"origName"_s, origName},  // as it was in the corral
                {u"ino"_s, QString::number(fid.ino)}, // the identity (see collect())
                {u"btime"_s, fid.btime},              // …together with this, where known
                {u"parkedAt"_s, QDateTime::currentSecsSinceEpoch()},
            });
            undo.append({dest, fi.absoluteFilePath()});
            ++moved;
        }
        if (!entries.isEmpty()) {
            parked.insert(id, entries);
        }
    }

    // Record first, then drop the marker: if we die in between, the guard parks
    // again, finds nothing to move, and the record stays intact.
    if (!writeParked(parked)) {
        // Without the record these files would just be loose desktop files next time,
        // their corrals forgotten. Better to leave them in their corrals (and the
        // marker in place): nothing is lost, and the next start shows them as before.
        for (const auto &[now, was] : std::as_const(undo)) {
            if (!moveEntry(now, was)) {
                qWarning() << "DesktopStash: could not move" << now << "back into its corral";
            }
        }
        qWarning() << "DesktopStash: could not write" << parkedPath() << "- corral files were left in their corrals";
        return 0;
    }
    QFile::remove(markerPath());
    fprintf(stderr, "theranch: parked %d item(s) on the desktop\n", moved);
    return moved;
}

int collect(const QStringList &knownIds)
{
    StashLock lock;
    if (!lock.locked()) {
        qWarning() << "DesktopStash: could not lock, not collecting";
        return 0;
    }

    const QJsonObject parked = readParked();
    if (parked.isEmpty()) {
        writeMarker();
        return 0;
    }
    // Keep a copy of every record we act on, so a mistake can always be undone.
    QFile::remove(parkedPath() + u".prev"_s);
    QFile::copy(parkedPath(), parkedPath() + u".prev"_s);

    // Index the desktop by inode once. A file is "ours" if its inode (and birth
    // time, see FileId) matches the one recorded when we parked it: rename(2)
    // preserves both, and so do reboots.
    // (Device numbers do NOT survive reboots on btrfs: subvolumes get a fresh
    // anonymous device number at each mount. Matching on device+inode is what made
    // every file look foreign after a reboot, so the device is no longer used.)
    // Matching by inode rather than name also follows files the user renamed on the
    // desktop meanwhile, while a different file that happens to have the same name
    // has a different inode and is left alone.
    const QString desktop = desktopDir();
    // Hard links give several names one inode, hence a list per inode.
    struct Candidate {
        QString name;
        QString btime;
    };
    QHash<quint64, QList<Candidate>> byInode;
    const auto onDesktop = QDir(desktop).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const QFileInfo &fi : onDesktop) {
        const FileId fid = fileId(fi.absoluteFilePath());
        if (fid.valid) {
            byInode[fid.ino].append({fi.fileName(), fid.btime});
        }
    }

    QJsonObject remaining; // anything not collected stays recorded (never silently dropped)
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    int moved = 0, kept = 0;

    // An unmatched record is kept for 30 days in case its file reappears, then let go.
    const auto fresh = [now](const QJsonObject &e) {
        return now - e.value(u"parkedAt").toInteger(now) < 30 * 24 * 3600;
    };
    // Index (in byInode[ino]) of the desktop entry a record refers to, or -1. Where
    // both sides know the birth time it must agree, which is what stops a new file
    // that inherited a deleted file's inode number from being taken. Among hard
    // links, the one still under its parked name wins.
    const auto findMatch = [&byInode](quint64 ino, const QString &btime, const QString &name) {
        const QList<Candidate> candidates = byInode.value(ino);
        int found = -1;
        for (int i = 0; i < candidates.size(); ++i) {
            const Candidate &c = candidates.at(i);
            if (!btime.isEmpty() && !c.btime.isEmpty() && c.btime != btime) {
                continue;
            }
            if (c.name == name) {
                return i;
            }
            if (found < 0) {
                found = i;
            }
        }
        return found;
    };

    for (auto it = parked.constBegin(); it != parked.constEnd(); ++it) {
        const QString id = it.key();
        const QJsonArray entries = it.value().toArray();
        if (!GroupStore::isValidId(id)) {
            qWarning() << "DesktopStash: ignoring record for invalid corral id" << id;
            continue;
        }
        QJsonArray left;
        if (!knownIds.contains(id)) {
            // Corral not (or no longer) known: its files stay on the desktop. The
            // record is kept for a while in case the corral comes back, not forever.
            for (const QJsonValue &v : entries) {
                if (fresh(v.toObject())) {
                    left.append(v);
                    ++kept;
                }
            }
            if (!left.isEmpty()) {
                remaining.insert(id, left);
            }
            continue;
        }
        const QString corralDir = GroupStore::ranchRoot() + u'/' + id;
        QDir().mkpath(corralDir);
        for (const QJsonValue &v : entries) {
            const QJsonObject e = v.toObject();
            const quint64 ino = e.value(u"ino").toString().toULongLong();
            const int match = ino == 0 ? -1 : findMatch(ino, e.value(u"btime").toString(), e.value(u"name").toString());
            if (match < 0) {
                // Not on the desktop (deleted or moved elsewhere while we were closed).
                if (fresh(e)) {
                    left.append(e);
                    ++kept;
                }
                continue;
            }
            const QString current = byInode[ino].at(match).name;
            const QString src = desktop + u'/' + current;
            // Back under its corral name, unless the user renamed it meanwhile.
            // The recorded name is only trusted if it's a plain name in this folder (no
            // "/" or ".."); otherwise the file keeps its current desktop name.
            const QString origName = e.value(u"origName").toString();
            const QString wanted = current == e.value(u"name").toString() && GroupStore::isValidId(origName) ? origName : current;
            const QString dest = corralDir + u'/' + freeName(corralDir, wanted);
            if (moveEntry(src, dest)) {
                byInode[ino].removeAt(match);
                ++moved;
            } else {
                qWarning() << "DesktopStash: could not collect" << src;
                left.append(e);
                ++kept;
            }
        }
        if (!left.isEmpty()) {
            remaining.insert(id, left);
        }
    }

    writeParked(remaining);
    writeMarker();
    // stderr rather than qInfo(): KDE's logging rules hide info messages, and this
    // line is what you'd look for in `journalctl --user` when something seems off.
    fprintf(stderr, "theranch: collected %d item(s) into corrals; %d record(s) kept for later\n", moved, kept);
    return moved;
}

int runGuard(qint64 pid)
{
    // We must outlive the app to do our job, including when the whole session is
    // being terminated. We exit as soon as the app is gone, so this can't hold up
    // a logout for long.
    std::signal(SIGTERM, SIG_IGN);
    std::signal(SIGHUP, SIG_IGN);
    std::signal(SIGINT, SIG_IGN);

    // pidfd: the kernel tells us when that exact process exits (no PID-reuse races,
    // no polling).
    // Via syscall(): glibc's pidfd_open() wrapper isn't declared extern "C" for C++.
    const int fd = int(::syscall(SYS_pidfd_open, pid_t(pid), 0));
    if (fd < 0) {
        // Already gone (or never existed): fall through and clean up if needed.
    } else {
        struct pollfd pfd{fd, POLLIN, 0};
        while (::poll(&pfd, 1, -1) < 0 && errno == EINTR) {
        }
        ::close(fd);
    }

    // The app parks on any orderly exit and removes the marker. If it's still here,
    // the app crashed or was killed: park on its behalf.
    // (park(pid) re-checks, under the lock, that the marker is still that app's: if
    // a new instance has started and collected meanwhile, the files are its to keep.)
    if (QFile::exists(markerPath())) {
        const int n = park(pid);
        if (n >= 0) {
            fprintf(stderr, "theranch guard: app %lld ended without tidying up; returned %d item(s) to the desktop\n",
                    static_cast<long long>(pid), n);
        }
    }
    return 0;
}
}
