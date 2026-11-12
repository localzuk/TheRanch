// SPDX-FileCopyrightText: 2026 Tony Ayre <me@tonyayre.net>
// SPDX-License-Identifier: GPL-3.0-or-later
//
// See desktopstash.h for the why. File layout:
//   ~/.local/state/theranch/parked.json  { "<corralId>": [ {name, origName, ino, parkedAt}, … ] }
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

// lstat: a symlink is identified as itself, not its target. Only `ino` is used
// for identity; see collect() for why the device number isn't.
struct FileId {
    quint64 dev = 0;
    quint64 ino = 0;
    bool valid = false;
};

FileId fileId(const QString &path)
{
    struct stat st;
    if (::lstat(QFile::encodeName(path).constData(), &st) != 0) {
        return {};
    }
    return {quint64(st.st_dev), quint64(st.st_ino), true};
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

int park()
{
    StashLock lock;
    if (!lock.locked()) {
        qWarning() << "DesktopStash: could not lock, not parking";
        return 0;
    }

    // Merge into any existing record, so a park after a partial collect loses nothing.
    QJsonObject parked = readParked();
    const QString desktop = desktopDir();
    const QDir root(GroupStore::ranchRoot());
    int moved = 0;

    const auto corralDirs = root.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &id : corralDirs) {
        const QDir dir(root.filePath(id));
        QJsonArray entries = parked.value(id).toArray();
        const auto files = dir.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
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
                {u"parkedAt"_s, QDateTime::currentSecsSinceEpoch()},
            });
            ++moved;
        }
        if (!entries.isEmpty()) {
            parked.insert(id, entries);
        }
    }

    // Record first, then drop the marker: if we die in between, the guard parks
    // again, finds nothing to move, and the record stays intact.
    writeParked(parked);
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

    // Index the desktop by inode once. A file is "ours" if its inode matches the one
    // recorded when we parked it: rename(2) preserves inodes, and so do reboots.
    // (Device numbers do NOT survive reboots on btrfs: subvolumes get a fresh
    // anonymous device number at each mount. Matching on device+inode is what made
    // every file look foreign after a reboot, so the device is no longer used.)
    // Matching by inode rather than name also follows files the user renamed on the
    // desktop meanwhile, while a different file that happens to have the same name
    // has a different inode and is left alone.
    const QString desktop = desktopDir();
    QHash<quint64, QString> byInode;
    const auto onDesktop = QDir(desktop).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const QFileInfo &fi : onDesktop) {
        const FileId fid = fileId(fi.absoluteFilePath());
        if (fid.valid) {
            byInode.insert(fid.ino, fi.fileName());
        }
    }

    QJsonObject remaining; // anything not collected stays recorded (never silently dropped)
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    int moved = 0, kept = 0;

    for (auto it = parked.constBegin(); it != parked.constEnd(); ++it) {
        const QString id = it.key();
        if (!knownIds.contains(id)) {
            remaining.insert(id, it.value()); // corral not (or no longer) known: keep
            continue;
        }
        const QString corralDir = GroupStore::ranchRoot() + u'/' + id;
        QDir().mkpath(corralDir);
        QJsonArray left;
        const QJsonArray entries = it.value().toArray();
        for (const QJsonValue &v : entries) {
            const QJsonObject e = v.toObject();
            const quint64 ino = e.value(u"ino").toString().toULongLong();
            const QString current = byInode.value(ino);
            if (ino == 0 || current.isEmpty()) {
                // Not on the desktop (deleted or moved elsewhere while we were closed).
                // Keep the record for 30 days in case it reappears, then let it go.
                const qint64 age = now - e.value(u"parkedAt").toInteger(now);
                if (age < 30 * 24 * 3600) {
                    left.append(e);
                    ++kept;
                }
                continue;
            }
            const QString src = desktop + u'/' + current;
            // Back under its corral name, unless the user renamed it meanwhile.
            const QString wanted = current == e.value(u"name").toString() ? e.value(u"origName").toString() : current;
            const QString dest = corralDir + u'/' + freeName(corralDir, wanted);
            if (moveEntry(src, dest)) {
                byInode.remove(ino);
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
    if (QFile::exists(markerPath())) {
        const int n = park();
        fprintf(stderr, "theranch guard: app %lld ended without tidying up; returned %d item(s) to the desktop\n",
                static_cast<long long>(pid), n);
    }
    return 0;
}
}
