// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <SimpleIni.h>

#include "common/fs/file.h"
#include "common/fs/path_util.h"
#include "lichii/startup_checks.h"

bool ReadPerformVulkanCheckSetting(const std::filesystem::path& config_path) {
    Common::FS::IOFile file{config_path, Common::FS::FileAccessMode::Read,
                            Common::FS::FileType::TextFile};
    if (!file.IsOpen()) {
        return true;
    }
    const std::string contents = file.ReadString(file.GetSize());
    CSimpleIniA ini;
    ini.SetUnicode();
    if (ini.LoadData(contents.data(), contents.size()) < 0) {
        return true;
    }
    const bool is_default = ini.GetBoolValue("Debugging", "perform_vulkan_check\\default", true);
    if (is_default) {
        return true;
    }
    return ini.GetBoolValue("Debugging", "perform_vulkan_check", true);
}

bool ReadPerformVulkanCheckSetting() {
    return ReadPerformVulkanCheckSetting(
        Common::FS::GetEdenPath(Common::FS::EdenPath::ConfigDir) / "qt-config.ini");
}
