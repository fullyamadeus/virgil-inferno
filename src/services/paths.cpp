#include "services/paths.h"
#include "domain/error.h"

#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#ifdef Q_OS_LINUX
#include <sys/stat.h>
#include <sys/syscall.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#endif

namespace virgil::paths {

std::filesystem::path native(const QString& path)
{
#ifdef Q_OS_WIN
    return std::filesystem::path(path.toStdWString());
#else
    return std::filesystem::path(path.toUtf8().constData());
#endif
}

QString fromNative(const std::filesystem::path& path)
{
#ifdef Q_OS_WIN
    return QString::fromStdWString(path.wstring());
#else
    return QString::fromUtf8(path.c_str());
#endif
}

QString rootDirectory(const QString& path)
{
    if (path.isEmpty())
        throw Error("Choose a library directory.");
    const QString absolute = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    // Reject symlinks in ancestors as well as at the leaf. This is deliberately
    // conservative; choose the real directory if your shortcut is a symlink.
    QString cursor = absolute;
    while (true) {
        const QFileInfo info(cursor);
        if (info.isSymLink())
            throw Error("Symbolic links are not supported for libraries. Choose the real directory: " + cursor);
        const QString parent = info.dir().absolutePath();
        if (parent == cursor)
            break;
        cursor = parent;
    }
    const QFileInfo info(absolute);
    if (!info.exists() || !info.isDir() || !info.isReadable())
        throw Error("Library directory is unavailable or unreadable. Reconnect its drive or check permissions: " + absolute);
    if (QDir(absolute).isRoot())
        throw Error("Choose a dedicated directory, not a filesystem root.");
    return info.canonicalFilePath();
}

void validateName(const QString& name)
{
    static const QRegularExpression forbidden(R"([<>:"/\\|?*\x{0000}-\x{001F}])");
    static const QRegularExpression device(R"(^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])($|\.))",
                                           QRegularExpression::CaseInsensitiveOption);
    if (name.isEmpty() || name != name.trimmed() || name == "." || name == ".."
        || name.endsWith('.') || forbidden.match(name).hasMatch()
        || device.match(name).hasMatch() || name.compare(".virgil", Qt::CaseInsensitive) == 0)
        throw Error("Choose a nonempty filename without leading/trailing spaces, a trailing dot, "
                    "control characters, reserved device names, or <>:\"/\\|?*. Unicode letters are welcome.");
}

void validateRelative(const QString& path, bool allowMetadata)
{
    if (path.isEmpty() || QDir::isAbsolutePath(path) || path.contains(QChar::Null)
#ifdef Q_OS_WIN
        || path.contains('\\')
#endif
    )
        throw Error("Invalid library-relative path: " + path);
    const auto parts = path.split('/');
    for (const auto& part : parts) {
        if (part.isEmpty() || part == "." || part == ".."
            || (!allowMetadata && part.compare(".virgil", Qt::CaseInsensitive) == 0))
            throw Error("Path escapes the materials area or uses a reserved directory: " + path);
    }
    // Do not apply validateName here: external files keep their actual names.
}

QString inside(const QString& root, const QString& relativePath, bool allowMetadata)
{
    validateRelative(relativePath, allowMetadata);
    QString absolute = root;
    const auto parts = relativePath.split('/');
    for (const auto& part : parts) {
        absolute = QDir(absolute).filePath(part);
        if (QFileInfo(absolute).isSymLink())
            throw Error("Symbolic links are not followed: " + absolute);
    }
    return absolute;
}

QString relative(const QString& root, const QString& absolute)
{
    const QString result = QDir(root).relativeFilePath(QFileInfo(absolute).absoluteFilePath());
    validateRelative(result);
    (void)inside(root, result);
    return result;
}

bool isWithin(const QString& root, const QString& absolute)
{
    const QString path = QDir(root).relativeFilePath(QFileInfo(absolute).absoluteFilePath());
    return path == "." || (!QDir::isAbsolutePath(path) && path != ".." && !path.startsWith("../"));
}

bool nameExists(const QString& directory, const QString& name)
{
    // A conservative collision policy also avoids surprises on case-insensitive
    // filesystems. Existing external names are never rewritten.
    const auto entries = QDir(directory).entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const auto& entry : entries) {
        if (entry.normalized(QString::NormalizationForm_C).toCaseFolded()
            == name.normalized(QString::NormalizationForm_C).toCaseFolded())
            return true;
    }
    return false;
}

QString identity(const QString& path)
{
#ifdef Q_OS_LINUX
    struct statx info {};
    const auto bytes = QFileInfo(path).absoluteFilePath().toUtf8();
    if (::statx(AT_FDCWD, bytes.constData(), AT_SYMLINK_NOFOLLOW,
                STATX_INO | STATX_BTIME | STATX_TYPE, &info) == 0
        && (info.stx_mask & STATX_BTIME) && (info.stx_mode & S_IFMT) != S_IFLNK)
        return QString("%1:%2:%3:%4:%5").arg(info.stx_dev_major).arg(info.stx_dev_minor)
            .arg(info.stx_ino).arg(info.stx_btime.tv_sec).arg(info.stx_btime.tv_nsec);
#endif
    return {}; // Never guess identity from bytes, size or timestamp alone.
}

void renameWithoutOverwrite(const QString& source, const QString& destination)
{
#ifdef Q_OS_LINUX
    const auto from = source.toUtf8(), to = destination.toUtf8();
    if (::syscall(SYS_renameat2, AT_FDCWD, from.constData(), AT_FDCWD, to.constData(), RENAME_NOREPLACE) == 0)
        return;
    throw Error("Cannot move without replacement (collision, different filesystem or unsupported filesystem): "
                + QString::fromLocal8Bit(std::strerror(errno)));
#else
    Q_UNUSED(source); Q_UNUSED(destination);
    throw Error("Safe file/folder moves are currently supported on Linux only.");
#endif
}

void finalizeWithoutOverwrite(const QString& staged, const QString& destination)
{
    // Creating a hard link is a no-replace operation, even if an external writer
    // races us. Unlinking the staging name later leaves one ordinary material.
    // Fail safely on filesystems without hard links or across mount boundaries.
    std::error_code error;
    std::filesystem::create_hard_link(native(staged), native(destination), error);
    if (error)
        throw Error("Cannot safely finalize the import (destination exists, is on a different filesystem, "
                    "or hard links are unsupported). The source is unchanged. Destination: "
                    + destination + "\n" + QString::fromStdString(error.message()));
}

} // namespace virgil::paths
