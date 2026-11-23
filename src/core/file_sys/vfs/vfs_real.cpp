// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <cstddef>
#include <iterator>
#include <utility>
#ifdef _WIN32
#include <windows.h>
#endif
#include "common/assert.h"
#include "common/fs/file.h"
#include "common/fs/fs.h"
#include "common/fs/path_util.h"
#include "common/logging.h"
#include "common/scope_exit.h"
#include "core/file_sys/errors.h"
#include "core/file_sys/vfs/vfs.h"
#include "core/file_sys/vfs/vfs_real.h"

// For FileTimeStampRaw
#include <sys/stat.h>

#ifdef _WIN32
#include <cstdio> // _getmaxstdio
#else
#include <sys/resource.h> // getrlimit
#endif

#ifdef _MSC_VER
#define stat _stat64
#endif

#ifdef __ANDROID__
#include "common/fs/fs_android.h"
#endif

namespace FileSys {

namespace FS = Common::FS;

namespace {

// Files the cache keeps open. It has to stay below the process's open-file limit (the C runtime's
// on Windows, RLIMIT_NOFILE elsewhere) with room to spare: at the limit every other open fails
// (saves, shader caches, and the cache's own next file, which it only evicts for once it reaches
// its cap).
size_t MaxOpenFiles() {
#ifdef _WIN32
    static const size_t limit = static_cast<size_t>((std::max)(_getmaxstdio() - 512, 64));
#else
    static const size_t limit = [] {
        struct rlimit rl;
        if (getrlimit(RLIMIT_NOFILE, &rl) != 0 || rl.rlim_cur == RLIM_INFINITY) {
            return size_t{8192};
        }
        return static_cast<size_t>(std::max<rlim_t>(rl.rlim_cur, 576) - 512);
    }();
#endif
    return (std::min)(size_t{8192}, limit);
}

bool IsWithinRoot(std::string_view root, std::string_view full_path) {
    if (root.empty())
        return true;

    if (full_path.size() < root.size() || full_path.substr(0, root.size()) != root)
        return false;

    return full_path.size() == root.size() || full_path[root.size()] == '/' || full_path[root.size()] == '\\';
}

constexpr FS::FileAccessMode ModeFlagsToFileAccessMode(OpenMode mode) {
    switch (mode) {
    case OpenMode::Read:
        return FS::FileAccessMode::Read;
    case OpenMode::Write:
    case OpenMode::ReadWrite:
    case OpenMode::AllowAppend:
    case OpenMode::All:
        return FS::FileAccessMode::ReadWrite;
    default:
        return {};
    }
}

} // Anonymous namespace

RealVfsFilesystem::RealVfsFilesystem() : VfsFilesystem(nullptr) {}
RealVfsFilesystem::~RealVfsFilesystem() {
    in_dtor = true;
}

std::string RealVfsFilesystem::GetName() const {
    return "Real";
}

bool RealVfsFilesystem::IsReadable() const {
    return true;
}

bool RealVfsFilesystem::IsWritable() const {
    return true;
}

VfsEntryType RealVfsFilesystem::GetEntryType(std::string_view path_) const {
    const auto path = FS::SanitizePath(path_, FS::DirectorySeparator::PlatformDefault);
    if (!FS::Exists(path)) {
        return VfsEntryType::None;
    }
    if (FS::IsDir(path)) {
        return VfsEntryType::Directory;
    }

    return VfsEntryType::File;
}

VirtualFile RealVfsFilesystem::OpenFileFromEntry(std::string_view path_, std::optional<u64> size,
                                                 OpenMode perms) {
    const auto path = FS::SanitizePath(path_, FS::DirectorySeparator::PlatformDefault);
    std::scoped_lock lk{list_lock};
    return OpenFileLocked(path, size, perms);
}

VirtualFile RealVfsFilesystem::OpenFileLocked(const std::string& path, std::optional<u64> size,
                                              OpenMode perms) {
    if (auto it = cache.find(path); it != cache.end()) {
        if (auto file = it->second.file.lock(); file) {
            return file;
        }
        CloseReferenceLocked(*it->second.reference);
        DetachReferenceLocked(it);
    }

    if (!size && !FS::IsFile(path)) {
        return nullptr;
    }

    auto reference = std::make_unique<FileReference>();
    reference->path = path;
    reference->size = size;
    auto file = std::make_shared<RealVfsFile>(*this, std::move(reference), perms);
    InsertReferenceIntoListLocked(*file->reference);
    cache[path] = {file, file->reference.get()};

    return file;
}

VirtualFile RealVfsFilesystem::OpenFile(std::string_view path_, OpenMode perms) {
    return OpenFileFromEntry(path_, {}, perms);
}

VirtualFile RealVfsFilesystem::CreateFile(std::string_view path_, OpenMode perms) {
    const auto path = FS::SanitizePath(path_, FS::DirectorySeparator::PlatformDefault);
#ifdef _WIN32
    // Declared before list_lock so dropping the last reference never re-enters DropReference
    // while list_lock is still held.
    std::shared_ptr<VfsFile> closing_owner;
#endif
    std::unique_lock lk{list_lock};
    auto entry = cache.find(path);
#ifdef _WIN32
    FileReference* closing_ref = nullptr;
    if (entry != cache.end()) {
        closing_owner = entry->second.file.lock();
        if (closing_owner) {
            closing_ref = entry->second.reference;
            BeginCloseReferenceLocked(lk, *closing_ref);
            entry = cache.find(path);
        } else {
            CloseReferenceLocked(*entry->second.reference);
        }
    }
    SCOPE_EXIT {
        if (closing_ref != nullptr) {
            EndCloseReferenceLocked(*closing_ref);
        }
    };
#endif

    // Current usages of CreateFile expect to delete the contents of an existing file.
    if (FS::IsFile(path)) {
        FS::IOFile temp{path, FS::FileAccessMode::Write, FS::FileType::BinaryFile};

        if (!temp.IsOpen()) {
            return nullptr;
        }
    } else if (!FS::NewFile(path)) {
        return nullptr;
    }

    if (entry != cache.end()) {
        entry->second.reference->size.reset();
        DetachReferenceLocked(entry);
    }
    return OpenFileLocked(path, {}, perms);
}

VirtualFile RealVfsFilesystem::CopyFile(std::string_view old_path_, std::string_view new_path_) {
    // Unused
    return nullptr;
}

VirtualFile RealVfsFilesystem::MoveFile(std::string_view old_path_, std::string_view new_path_) {
    const auto old_path = FS::SanitizePath(old_path_, FS::DirectorySeparator::PlatformDefault);
    const auto new_path = FS::SanitizePath(new_path_, FS::DirectorySeparator::PlatformDefault);
#ifdef _WIN32
    std::shared_ptr<VfsFile> closing_owner;
    std::unique_lock lk{list_lock};
    if (!RenameFileLocked(lk, old_path, new_path, closing_owner)) {
        return nullptr;
    }
#else
    std::unique_lock lk{list_lock};
    if (!RenameFileLocked(lk, old_path, new_path)) {
        return nullptr;
    }
#endif
    return OpenFileLocked(new_path, {}, OpenMode::ReadWrite);
}

#ifdef _WIN32
bool RealVfsFilesystem::RenameFileLocked(std::unique_lock<std::mutex>& lk,
                                         const std::string& old_path, const std::string& new_path,
                                         std::shared_ptr<VfsFile>& closing_owner) {
#else
bool RealVfsFilesystem::RenameFileLocked(std::unique_lock<std::mutex>& lk,
                                         const std::string& old_path, const std::string& new_path) {
#endif
    if (old_path == new_path) {
        return false;
    }
    auto source = cache.find(old_path);
#ifdef _WIN32
    FileReference* closing_ref = nullptr;
    if (source != cache.end()) {
        closing_owner = source->second.file.lock();
        if (closing_owner) {
            closing_ref = source->second.reference;
            BeginCloseReferenceLocked(lk, *closing_ref);
            source = cache.find(old_path);
        } else {
            CloseReferenceLocked(*source->second.reference);
        }
    }
    SCOPE_EXIT {
        if (closing_ref != nullptr) {
            EndCloseReferenceLocked(*closing_ref);
        }
    };
#else
    (void)lk;
#endif
    if (!FS::RenameFile(old_path, new_path)) {
        return false;
    }

    if (auto destination = cache.find(new_path); destination != cache.end()) {
        DetachReferenceLocked(destination);
    }
    if (source != cache.end()) {
#ifndef _WIN32
        CloseReferenceLocked(*source->second.reference);
#endif
        auto entry = cache.extract(source);
        entry.key() = new_path;
        entry.mapped().reference->path = new_path;
        cache.insert(std::move(entry));
    }
    return true;
}

bool RealVfsFilesystem::DeleteFile(std::string_view path_) {
    return DeleteFileWithResult(path_).IsSuccess();
}

Result RealVfsFilesystem::DeleteFileWithResult(std::string_view path_) {
    const auto path = FS::SanitizePath(path_, FS::DirectorySeparator::PlatformDefault);
#ifdef _WIN32
    std::shared_ptr<VfsFile> closing_owner;
#endif
    std::unique_lock lk{list_lock};
    auto entry = cache.find(path);
#ifdef _WIN32
    FileReference* closing_ref = nullptr;
    if (entry != cache.end()) {
        closing_owner = entry->second.file.lock();
        if (closing_owner) {
            closing_ref = entry->second.reference;
            BeginCloseReferenceLocked(lk, *closing_ref);
            entry = cache.find(path);
        } else {
            CloseReferenceLocked(*entry->second.reference);
        }
    }
    SCOPE_EXIT {
        if (closing_ref != nullptr) {
            EndCloseReferenceLocked(*closing_ref);
        }
    };
#endif
    std::error_code ec;
    if (!FS::RemoveFile(std::filesystem::path{FS::ToU8String(path)}, ec)) {
#ifdef _WIN32
        if (ec == std::error_code{ERROR_SHARING_VIOLATION, std::system_category()} ||
            ec == std::error_code{ERROR_LOCK_VIOLATION, std::system_category()}) {
            return ResultTargetLocked;
        }
#endif
        if (ec == std::errc::permission_denied || ec == std::errc::read_only_file_system) {
            return ResultPermissionDenied;
        }
        if (ec == std::errc::invalid_argument || ec == std::errc::is_a_directory) {
            return ResultInvalidPath;
        }
        return ResultUnknown;
    }
    if (entry != cache.end()) {
        DetachReferenceLocked(entry);
    }
    return ResultSuccess;
}

VirtualDir RealVfsFilesystem::OpenDirectory(std::string_view path_, OpenMode perms) {
    const auto path = FS::SanitizePath(path_, FS::DirectorySeparator::PlatformDefault);
    return std::make_shared<RealVfsDirectory>(*this, path, perms);
}

VirtualDir RealVfsFilesystem::CreateDirectory(std::string_view path_, OpenMode perms) {
    const auto path = FS::SanitizePath(path_, FS::DirectorySeparator::PlatformDefault);
    if (!FS::CreateDirs(path)) {
        return nullptr;
    }
    return std::make_shared<RealVfsDirectory>(*this, path, perms);
}

VirtualDir RealVfsFilesystem::CopyDirectory(std::string_view old_path_,
                                            std::string_view new_path_) {
    // Unused
    return nullptr;
}

VirtualDir RealVfsFilesystem::MoveDirectory(std::string_view old_path_,
                                            std::string_view new_path_) {
    const auto old_sanitized = FS::SanitizePath(old_path_, FS::DirectorySeparator::PlatformDefault);
    const auto new_sanitized = FS::SanitizePath(new_path_, FS::DirectorySeparator::PlatformDefault);
    const std::string old_path{FS::RemoveTrailingSlash(old_sanitized)};
    const std::string new_path{FS::RemoveTrailingSlash(new_sanitized)};
#ifdef _WIN32
    std::vector<std::shared_ptr<VfsFile>> closing_owners;
    std::unique_lock lk{list_lock};
    if (!RenameDirectoryLocked(lk, old_path, new_path, closing_owners)) {
        return nullptr;
    }
#else
    std::unique_lock lk{list_lock};
    if (!RenameDirectoryLocked(lk, old_path, new_path)) {
        return nullptr;
    }
#endif
    return OpenDirectory(new_path, OpenMode::ReadWrite);
}

#ifdef _WIN32
bool RealVfsFilesystem::RenameDirectoryLocked(
    std::unique_lock<std::mutex>& lk, const std::string& old_path, const std::string& new_path,
    std::vector<std::shared_ptr<VfsFile>>& closing_owners) {
#else
bool RealVfsFilesystem::RenameDirectoryLocked(std::unique_lock<std::mutex>& lk,
                                              const std::string& old_path,
                                              const std::string& new_path) {
#endif
    if (old_path.empty() || new_path.empty() || old_path == new_path) {
        return false;
    }
#ifdef _WIN32
    std::vector<FileReference*> closing_refs;
    for (auto it = cache.begin(); it != cache.end(); ++it) {
        if (IsWithinRoot(old_path, it->first)) {
            if (auto owner = it->second.file.lock()) {
                closing_owners.push_back(std::move(owner));
                closing_refs.push_back(it->second.reference);
            } else {
                CloseReferenceLocked(*it->second.reference);
            }
        }
    }
    size_t closed_count = 0;
    SCOPE_EXIT {
        for (size_t i = 0; i < closed_count; ++i) {
            EndCloseReferenceLocked(*closing_refs[i]);
        }
    };
    for (FileReference* ref : closing_refs) {
        BeginCloseReferenceLocked(lk, *ref);
        ++closed_count;
    }
#else
    (void)lk;
#endif
    if (!FS::RenameDir(old_path, new_path)) {
        return false;
    }

    std::vector<FileCache::iterator> affected;
    for (auto it = cache.begin(); it != cache.end();) {
        if (IsWithinRoot(old_path, it->first)) {
            affected.push_back(it);
            ++it;
        } else if (IsWithinRoot(new_path, it->first)) {
            const auto next = std::next(it);
            DetachReferenceLocked(it);
            it = next;
        } else {
            ++it;
        }
    }
    for (const auto it : affected) {
#ifndef _WIN32
        CloseReferenceLocked(*it->second.reference);
#endif
        const std::string updated_path = new_path + it->first.substr(old_path.size());
        auto entry = cache.extract(it);
        entry.key() = updated_path;
        entry.mapped().reference->path = updated_path;
        cache.insert(std::move(entry));
    }
    return true;
}

bool RealVfsFilesystem::DeleteDirectory(std::string_view path_) {
    const auto path = FS::SanitizePath(path_, FS::DirectorySeparator::PlatformDefault);
    return FS::RemoveDirRecursively(path);
}

void RealVfsFilesystem::RefreshReferenceLocked(FileReference& reference, OpenMode perms) {
    // Temporarily remove from list.
    this->RemoveReferenceFromListLocked(reference);

    // Restore file if needed.
    if (!reference.file && !reference.detached) {
        this->EvictSingleReferenceLocked();

        reference.file = FS::FileOpen(reference.path, ModeFlagsToFileAccessMode(perms),
                                      FS::FileType::BinaryFile);
        if (reference.file) {
            num_open_files++;
        }
    }

    // Reinsert into list.
    this->InsertReferenceIntoListLocked(reference);
}

void RealVfsFilesystem::DropReference(FileReference& reference) {
    if (in_dtor)
        return;

    std::scoped_lock lk{list_lock};

    // Remove from list.
    this->RemoveReferenceFromListLocked(reference);
    if (const auto it = cache.find(reference.path);
        it != cache.end() && it->second.reference == &reference) {
        cache.erase(it);
    }

    // Close the file.
    if (reference.file) {
        reference.file.reset();
        num_open_files--;
    }
}

void RealVfsFilesystem::DetachReferenceLocked(FileCache::iterator entry) {
    entry->second.reference->detached = true;
    cache.erase(entry);
}

std::shared_ptr<Common::FS::IOFile> RealVfsFilesystem::AcquireReferenceFile(
    FileReference& reference, OpenMode perms, bool invalidate_size) {
    std::unique_lock lk{list_lock};
#ifdef _WIN32
    io_cv.wait(lk, [&reference] { return !reference.closing; });
#endif
    if (invalidate_size) {
        reference.size.reset();
    }
    RefreshReferenceLocked(reference, perms);
    auto file = reference.file;
    if (file) {
        ++reference.active_io;
    }
    return file;
}

void RealVfsFilesystem::FinishReferenceIo(FileReference& reference) {
    std::scoped_lock lk{list_lock};
    --reference.active_io;
#ifdef _WIN32
    if (reference.active_io == 0 && reference.closing) {
        io_cv.notify_all();
    }
#endif
}

void RealVfsFilesystem::CloseReferenceLocked(FileReference& reference) {
    if (!reference.file) {
        return;
    }
    RemoveReferenceFromListLocked(reference);
    reference.file.reset();
    num_open_files--;
    InsertReferenceIntoListLocked(reference);
}

#ifdef _WIN32
void RealVfsFilesystem::BeginCloseReferenceLocked(std::unique_lock<std::mutex>& lk,
                                                  FileReference& reference) {
    io_cv.wait(lk, [&reference] { return !reference.closing; });
    reference.closing = true;
    CloseReferenceLocked(reference);
    io_cv.wait(lk, [&reference] { return reference.active_io == 0; });
}

void RealVfsFilesystem::EndCloseReferenceLocked(FileReference& reference) {
    reference.closing = false;
    io_cv.notify_all();
}
#endif

void RealVfsFilesystem::EvictSingleReferenceLocked() {
    if (num_open_files < MaxOpenFiles() || open_references.empty()) {
        return;
    }

    for (auto it = open_references.rbegin(); it != open_references.rend(); ++it) {
        if (it->active_io == 0) {
            CloseReferenceLocked(*it);
            return;
        }
    }
}

void RealVfsFilesystem::InsertReferenceIntoListLocked(FileReference& reference) {
    if (reference.file) {
        open_references.push_front(reference);
    } else {
        closed_references.push_front(reference);
    }
}

void RealVfsFilesystem::RemoveReferenceFromListLocked(FileReference& reference) {
    if (reference.file) {
        open_references.erase(open_references.iterator_to(reference));
    } else {
        closed_references.erase(closed_references.iterator_to(reference));
    }
}

RealVfsFile::RealVfsFile(RealVfsFilesystem& base_, std::unique_ptr<FileReference> reference_,
                         OpenMode perms_)
    : base(base_), reference(std::move(reference_)), perms(perms_) {}

RealVfsFile::~RealVfsFile() {
    base.DropReference(*reference);
}

std::string RealVfsFile::GetName() const {
    std::scoped_lock lk{base.list_lock};
    const auto& path = reference->path;
#ifdef __ANDROID__
    if (path[0] != '/') {
        return FS::Android::GetFilename(path);
    }
#endif
    // The last path component.
    const auto end = path.find_last_not_of("\\/");
    if (end == std::string::npos) {
        return "";
    }
    const auto separator = path.find_last_of("\\/", end);
    const auto begin = separator == std::string::npos ? 0 : separator + 1;
    return path.substr(begin, end + 1 - begin);
}

std::size_t RealVfsFile::GetSize() const {
    {
        std::scoped_lock lk{base.list_lock};
        if (reference->size && !reference->detached) {
            return *reference->size;
        }
    }
    const auto file = base.AcquireReferenceFile(*reference, perms);
    if (!file) {
        return 0;
    }
    SCOPE_EXIT {
        base.FinishReferenceIo(*reference);
    };
#ifdef _WIN32
    std::scoped_lock io_lk{reference->io_mutex};
#endif
    return file->GetSize();
}

bool RealVfsFile::Resize(std::size_t new_size) {
    const auto file = base.AcquireReferenceFile(*reference, perms, true);
    if (!file) {
        return false;
    }
    SCOPE_EXIT {
        base.FinishReferenceIo(*reference);
    };
#ifdef _WIN32
    std::scoped_lock io_lk{reference->io_mutex};
#endif
    return file->SetSize(new_size);
}

VirtualDir RealVfsFile::GetContainingDirectory() const {
    std::string parent;
    {
        std::scoped_lock lk{base.list_lock};
        parent = FS::GetParentPath(reference->path);
    }
    return base.OpenDirectory(parent, perms);
}

bool RealVfsFile::IsWritable() const {
    return True(perms & OpenMode::Write);
}

bool RealVfsFile::IsReadable() const {
    return True(perms & OpenMode::Read);
}

std::size_t RealVfsFile::Read(u8* data, std::size_t length, std::size_t offset) const {
    if (length == 0) {
        return 0;
    }
    const auto file = base.AcquireReferenceFile(*reference, perms);
    if (!file) {
        return 0;
    }
    SCOPE_EXIT {
        base.FinishReferenceIo(*reference);
    };
#ifdef _WIN32
    std::scoped_lock io_lk{reference->io_mutex};
    if (!file->Seek(static_cast<s64>(offset))) {
        return 0;
    }
    return file->ReadSpan(std::span{data, length});
#else
    return file->ReadAt(offset, std::span{data, length});
#endif
}

std::size_t RealVfsFile::Write(const u8* data, std::size_t length, std::size_t offset) {
    const auto file = base.AcquireReferenceFile(*reference, perms, true);
    if (!file) {
        return 0;
    }
    SCOPE_EXIT {
        base.FinishReferenceIo(*reference);
    };
    if (length == 0) {
        return 0;
    }
#ifdef _WIN32
    std::scoped_lock io_lk{reference->io_mutex};
    if (!file->Seek(static_cast<s64>(offset))) {
        return 0;
    }
    return file->WriteSpan(std::span{data, length});
#else
    return file->WriteAt(offset, std::span{data, length});
#endif
}

bool RealVfsFile::Rename(std::string_view name) {
#ifdef _WIN32
    std::shared_ptr<VfsFile> closing_owner;
#endif
    std::unique_lock lk{base.list_lock};
    if (reference->detached) {
        return false;
    }
    const std::string path = reference->path;
    const auto new_path = FS::SanitizePath(FS::GetParentPath(path) + '/' + std::string(name),
                                           FS::DirectorySeparator::PlatformDefault);
#ifdef _WIN32
    return base.RenameFileLocked(lk, path, new_path, closing_owner);
#else
    return base.RenameFileLocked(lk, path, new_path);
#endif
}

// TODO(DarkLordZach): MSVC would not let me combine the following two functions using 'if
// constexpr' because there is a compile error in the branch not used.

template <>
std::vector<VirtualFile> RealVfsDirectory::IterateEntries<RealVfsFile, VfsFile>() const {
    if (perms == OpenMode::AllowAppend) {
        return {};
    }

    std::vector<VirtualFile> out;

    const FS::DirEntryCallable callback = [this,
                                           &out](const std::filesystem::directory_entry& entry) {
        const auto full_path_string = FS::PathToUTF8String(entry.path());

        out.emplace_back(base.OpenFileFromEntry(full_path_string, entry.file_size(), perms));

        return true;
    };

    FS::IterateDirEntries(path, callback, FS::DirEntryFilter::File);

    return out;
}

template <>
std::vector<VirtualDir> RealVfsDirectory::IterateEntries<RealVfsDirectory, VfsDirectory>() const {
    if (perms == OpenMode::AllowAppend) {
        return {};
    }

    std::vector<VirtualDir> out;

    const FS::DirEntryCallable callback = [this,
                                           &out](const std::filesystem::directory_entry& entry) {
        const auto full_path_string = FS::PathToUTF8String(entry.path());

        out.emplace_back(base.OpenDirectory(full_path_string, perms));

        return true;
    };

    FS::IterateDirEntries(path, callback, FS::DirEntryFilter::Directory);

    return out;
}

RealVfsDirectory::RealVfsDirectory(RealVfsFilesystem& base_, const std::string& path_,
                                   OpenMode perms_)
    : base(base_), path(FS::RemoveTrailingSlash(path_)), parent_path(FS::GetParentPath(path)),
      path_components(FS::SplitPathComponentsCopy(path)), perms(perms_) {
    if (!FS::Exists(path) && True(perms & OpenMode::Write)) {
        void(FS::CreateDirs(path));
    }
}

RealVfsDirectory::~RealVfsDirectory() = default;

VirtualFile RealVfsDirectory::GetFileRelative(std::string_view relative_path) const {
    const auto full_path = FS::SanitizePath(path + '/' + std::string(relative_path));
    if (!FS::Exists(full_path) || FS::IsDir(full_path)
        || !IsWithinRoot(FS::SanitizePath(path), full_path)) {
        return nullptr;
    }
    return base.OpenFile(full_path, perms);
}

VirtualDir RealVfsDirectory::GetDirectoryRelative(std::string_view relative_path) const {
    const auto full_path = FS::SanitizePath(path + '/' + std::string(relative_path));
    if (!FS::Exists(full_path) || !FS::IsDir(full_path)
        || !IsWithinRoot(FS::SanitizePath(path), full_path)) {
        return nullptr;
    }
    return base.OpenDirectory(full_path, perms);
}

VirtualFile RealVfsDirectory::GetFile(std::string_view name) const {
    return GetFileRelative(name);
}

VirtualDir RealVfsDirectory::GetSubdirectory(std::string_view name) const {
    return GetDirectoryRelative(name);
}

VirtualFile RealVfsDirectory::CreateFileRelative(std::string_view relative_path) {
    const auto full_path = FS::SanitizePath(path + '/' + std::string(relative_path));
    if (!FS::CreateParentDirs(full_path) || !IsWithinRoot(FS::SanitizePath(path), full_path)) {
        return nullptr;
    }
    return base.CreateFile(full_path, perms);
}

VirtualDir RealVfsDirectory::CreateDirectoryRelative(std::string_view relative_path) {
    const auto full_path = FS::SanitizePath(path + '/' + std::string(relative_path));
    return base.CreateDirectory(full_path, perms);
}

bool RealVfsDirectory::DeleteSubdirectoryRecursive(std::string_view name) {
    const auto full_path = FS::SanitizePath(this->path + '/' + std::string(name));
    return FS::RemoveDirRecursively(full_path);
}

std::vector<VirtualFile> RealVfsDirectory::GetFiles() const {
    return IterateEntries<RealVfsFile, VfsFile>();
}

FileTimeStampRaw RealVfsDirectory::GetFileTimeStamp(std::string_view path_) const {
    const auto full_path = FS::SanitizePath(path + '/' + std::string(path_));
    const auto fs_path = std::filesystem::path{FS::ToU8String(full_path)};

#ifdef _WIN32
    struct _stat64 file_status;
    const auto stat_result = _wstat64(fs_path.c_str(), &file_status);
#else
    struct stat file_status;
    const auto stat_result = stat(fs_path.c_str(), &file_status);
#endif

    if (stat_result != 0) {
        return {};
    }

    return {
        .created{static_cast<u64>(file_status.st_ctime)},
        .accessed{static_cast<u64>(file_status.st_atime)},
        .modified{static_cast<u64>(file_status.st_mtime)},
    };
}

std::vector<VirtualDir> RealVfsDirectory::GetSubdirectories() const {
    return IterateEntries<RealVfsDirectory, VfsDirectory>();
}

bool RealVfsDirectory::IsWritable() const {
    return True(perms & OpenMode::Write);
}

bool RealVfsDirectory::IsReadable() const {
    return True(perms & OpenMode::Read);
}

std::string RealVfsDirectory::GetName() const {
    return path_components.empty() ? "" : std::string(path_components.back());
}

VirtualDir RealVfsDirectory::GetParentDirectory() const {
    if (path_components.size() <= 1) {
        return nullptr;
    }

    return base.OpenDirectory(parent_path, perms);
}

VirtualDir RealVfsDirectory::CreateSubdirectory(std::string_view name) {
    const std::string subdir_path = (path + '/').append(name);
    return base.CreateDirectory(subdir_path, perms);
}

VirtualFile RealVfsDirectory::CreateFile(std::string_view name) {
    const std::string file_path = (path + '/').append(name);
    return base.CreateFile(file_path, perms);
}

bool RealVfsDirectory::DeleteSubdirectory(std::string_view name) {
    const std::string subdir_path = (path + '/').append(name);
    return FS::RemoveDir(subdir_path);
}

bool RealVfsDirectory::DeleteFile(std::string_view name) {
    return DeleteFileWithResult(name).IsSuccess();
}

Result RealVfsDirectory::DeleteFileWithResult(std::string_view name) {
    const std::string file_path = (path + '/').append(name);
    return base.DeleteFileWithResult(file_path);
}

bool RealVfsDirectory::Rename(std::string_view name) {
    if (name.empty() || name.find('/') != std::string_view::npos ||
        name.find('\\') != std::string_view::npos) {
        return false;
    }
    const auto sanitized =
        FS::SanitizePath((parent_path + '/').append(name), FS::DirectorySeparator::PlatformDefault);
    const std::string new_path{FS::RemoveTrailingSlash(sanitized)};
    {
#ifdef _WIN32
        std::vector<std::shared_ptr<VfsFile>> closing_owners;
        std::unique_lock lk{base.list_lock};
        if (!base.RenameDirectoryLocked(lk, path, new_path, closing_owners)) {
            return false;
        }
#else
        std::unique_lock lk{base.list_lock};
        if (!base.RenameDirectoryLocked(lk, path, new_path)) {
            return false;
        }
#endif
    }
    path = new_path;
    parent_path = FS::GetParentPath(path);
    path_components = FS::SplitPathComponentsCopy(path);
    return true;
}

std::string RealVfsDirectory::GetFullPath() const {
    auto out = path;
    std::replace(out.begin(), out.end(), '\\', '/');
    return out;
}

std::map<std::string, VfsEntryType, std::less<>> RealVfsDirectory::GetEntries() const {
    if (perms == OpenMode::AllowAppend) {
        return {};
    }

    std::map<std::string, VfsEntryType, std::less<>> out;

    const FS::DirEntryCallable callback = [&out](const std::filesystem::directory_entry& entry) {
        const auto filename = FS::PathToUTF8String(entry.path().filename());
        out.insert_or_assign(filename,
                             entry.is_directory() ? VfsEntryType::Directory : VfsEntryType::File);
        return true;
    };

    FS::IterateDirEntries(path, callback);

    return out;
}

} // namespace FileSys
