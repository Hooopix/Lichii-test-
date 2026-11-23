// SPDX-FileCopyrightText: Copyright 2026 Lichii Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <atomic>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include "common/fs/file.h"
#include "common/fs/path_util.h"
#include "core/file_sys/errors.h"
#include "core/file_sys/vfs/vfs_real.h"
#include "tests/common/temp_dir.h"

TEST_CASE("Real VFS can replace a file while its old reference is live", "[core][vfs]") {
    const Tests::TempDir temp{"lichii-vfs"};
    const auto path = Common::FS::PathToUTF8String(temp / "trial.ghost");
    FileSys::RealVfsFilesystem fs;
    const auto ghost = fs.CreateFile(path);
    REQUIRE(ghost);
    REQUIRE(ghost->WriteByte(0x42));
    REQUIRE(ghost->ReadByte(0) == 0x42);

    REQUIRE(fs.DeleteFile(path));
    REQUIRE_FALSE(fs.OpenFile(path));
    const auto replacement = fs.CreateFile(path);
    REQUIRE(replacement);
    REQUIRE(replacement->WriteByte(0x24));
    CHECK(replacement->ReadByte(0) == 0x24);
#ifdef _WIN32
    CHECK_FALSE(ghost->ReadByte(0));
    CHECK_FALSE(ghost->WriteByte(0x55));
#else
    CHECK(ghost->ReadByte(0) == 0x42);
    REQUIRE(ghost->WriteByte(0x55));
#endif
    CHECK(replacement->ReadByte(0) == 0x24);
}

TEST_CASE("Real VFS can rename a file while its reference is live", "[core][vfs]") {
    const Tests::TempDir temp{"lichii-vfs"};
    const auto old_path = Common::FS::PathToUTF8String(temp / "old.ghost");
    const auto new_path = Common::FS::PathToUTF8String(temp / "new.ghost");
    FileSys::RealVfsFilesystem fs;
    const auto ghost = fs.CreateFile(old_path);
    REQUIRE(ghost);
    REQUIRE(ghost->WriteByte(0x42));

    {
        const auto other = fs.CreateFile(new_path);
        REQUIRE(other);
        REQUIRE(other->WriteByte(0x24));
        CHECK_FALSE(ghost->Rename("new.ghost"));
        CHECK(ghost->ReadByte(0) == 0x42);
        CHECK(other->ReadByte(0) == 0x24);
        REQUIRE(fs.DeleteFile(new_path));
    }
    REQUIRE(ghost->Rename("new.ghost"));
    REQUIRE_FALSE(fs.OpenFile(old_path));
    const auto renamed = fs.OpenFile(new_path);
    REQUIRE(renamed);
    CHECK(renamed == ghost);
    CHECK(renamed->GetSize() == 1);
    CHECK(renamed->ReadAllBytes() == std::vector<u8>{0x42});
    const auto replacement = fs.CreateFile(old_path);
    REQUIRE(replacement);
    REQUIRE(replacement->Resize(2));
    CHECK(renamed->GetSize() == 1);
    CHECK(renamed->ReadByte(0) == 0x42);
}

TEST_CASE("Real VFS can rename a directory", "[core][vfs]") {
    const Tests::TempDir temp{"lichii-vfs"};
    const auto old_dir_path = Common::FS::PathToUTF8String(temp / "old_dir");
    const auto new_dir_path = Common::FS::PathToUTF8String(temp / "new_dir");
    const auto sibling_path = Common::FS::PathToUTF8String(temp / "existing_dir");
    FileSys::RealVfsFilesystem fs;

    const auto dir = fs.CreateDirectory(old_dir_path);
    REQUIRE(dir);
    const auto child = dir->CreateFile("inside.bin");
    REQUIRE(child);
    REQUIRE(child->WriteByte(0x7A));
    REQUIRE(child->ReadByte(0) == 0x7A);

    const auto existing = fs.CreateDirectory(sibling_path);
    REQUIRE(existing);
    CHECK_FALSE(dir->Rename("existing_dir"));
    CHECK_FALSE(dir->Rename(""));
    CHECK_FALSE(dir->Rename("bad/name"));
    CHECK(dir->GetName() == "old_dir");

    REQUIRE(dir->Rename("new_dir"));
    CHECK(dir->GetName() == "new_dir");
    CHECK_FALSE(std::filesystem::exists(temp / "old_dir"));
    CHECK(std::filesystem::is_directory(temp / "new_dir"));
    CHECK(child->ReadByte(0) == 0x7A);

    const auto reopened = fs.OpenFile(Common::FS::PathToUTF8String(temp / "new_dir" / "inside.bin"));
    REQUIRE(reopened);
    CHECK(reopened == child);
    CHECK(reopened->ReadByte(0) == 0x7A);
    const auto via_dir = dir->GetFile("inside.bin");
    REQUIRE(via_dir);
    CHECK(via_dir->ReadByte(0) == 0x7A);
}

#ifndef _WIN32
TEST_CASE("IOFile ReadAt and WriteAt perform positional I/O", "[core][vfs]") {
    const Tests::TempDir temp{"lichii-vfs"};
    const auto path = temp / "positional.bin";
    {
        Common::FS::IOFile creator{path, Common::FS::FileAccessMode::Write,
                                   Common::FS::FileType::BinaryFile};
        REQUIRE(creator.IsOpen());
    }
    Common::FS::IOFile file{path, Common::FS::FileAccessMode::ReadWrite,
                            Common::FS::FileType::BinaryFile};
    REQUIRE(file.IsOpen());

    const std::vector<u8> second{0x10, 0x20, 0x30, 0x40};
    const std::vector<u8> first{0xAA, 0xBB, 0xCC, 0xDD};
    REQUIRE(file.WriteAt(4, second) == second.size());
    REQUIRE(file.WriteAt(0, first) == first.size());

    std::vector<u8> buf(8, 0);
    REQUIRE(file.ReadAt(0, buf) == buf.size());
    CHECK(buf == std::vector<u8>{0xAA, 0xBB, 0xCC, 0xDD, 0x10, 0x20, 0x30, 0x40});

    std::vector<u8> tail(4, 0);
    REQUIRE(file.ReadAt(6, tail) == 2);
    CHECK(tail[0] == 0x30);
    CHECK(tail[1] == 0x40);
}
#endif

TEST_CASE("RealVfsFile supports concurrent reads across multiple threads", "[core][vfs]") {
    const Tests::TempDir temp{"lichii-vfs"};
    const auto shared_path = Common::FS::PathToUTF8String(temp / "shared.bin");
    const auto sibling_path = Common::FS::PathToUTF8String(temp / "sibling.bin");
    FileSys::RealVfsFilesystem fs;

    constexpr size_t NumBlocks = 4;
    constexpr size_t BlockSize = 4096;
    {
        const auto writer = fs.CreateFile(shared_path);
        REQUIRE(writer);
        std::vector<u8> payload(NumBlocks * BlockSize);
        for (size_t b = 0; b < NumBlocks; ++b) {
            for (size_t i = 0; i < BlockSize; ++i) {
                payload[b * BlockSize + i] = static_cast<u8>((b * 37 + i) & 0xff);
            }
        }
        REQUIRE(writer->Write(payload.data(), payload.size(), 0) == payload.size());
        const auto sibling = fs.CreateFile(sibling_path);
        REQUIRE(sibling);
        REQUIRE(sibling->WriteByte(0x5A));
    }

    const auto shared = fs.OpenFile(shared_path);
    REQUIRE(shared);
    REQUIRE(shared->GetSize() == NumBlocks * BlockSize);

    std::atomic<bool> ok{true};
    std::vector<std::thread> readers;
    for (size_t b = 0; b < NumBlocks; ++b) {
        readers.emplace_back([&, b] {
            std::vector<u8> chunk(BlockSize);
            for (int iter = 0; iter < 64; ++iter) {
                if (shared->Read(chunk.data(), chunk.size(), b * BlockSize) != BlockSize) {
                    ok = false;
                    return;
                }
                for (size_t i = 0; i < BlockSize; ++i) {
                    if (chunk[i] != static_cast<u8>((b * 37 + i) & 0xff)) {
                        ok = false;
                        return;
                    }
                }
            }
        });
    }
    readers.emplace_back([&] {
        for (int iter = 0; iter < 64; ++iter) {
            const auto sibling = fs.OpenFile(sibling_path);
            if (!sibling || sibling->ReadByte(0) != 0x5A) {
                ok = false;
                return;
            }
        }
    });
    for (auto& t : readers) {
        t.join();
    }
    REQUIRE(ok.load());
}

#ifdef _WIN32
TEST_CASE("Real VFS reports a genuinely locked delete", "[core][vfs]") {
    const Tests::TempDir temp{"lichii-vfs"};
    const auto path = temp / "trial.ghost";
    FileSys::RealVfsFilesystem fs;
    const auto dir = fs.OpenDirectory(Common::FS::PathToUTF8String(temp.Path()));
    {
        const auto seed = fs.CreateFile(Common::FS::PathToUTF8String(path));
        REQUIRE(seed);
        REQUIRE(seed->WriteByte('a'));
    }
    const auto ghost = dir->GetFile("trial.ghost");
    REQUIRE(ghost);
    REQUIRE(ghost->ReadByte(0) == 'a');
    Common::FS::IOFile external{path, Common::FS::FileAccessMode::Read};
    REQUIRE(external.IsOpen());

    CHECK(dir->DeleteFileWithResult("trial.ghost") == FileSys::ResultTargetLocked);
    CHECK(std::filesystem::exists(path));
    CHECK(dir->GetFile("trial.ghost") == ghost);
    CHECK(ghost->ReadByte(0) == 'a');
    external.Close();
    CHECK(dir->DeleteFileWithResult("trial.ghost") == ResultSuccess);
    CHECK_FALSE(std::filesystem::exists(path));
}
#endif
