// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "lichii/startup_checks.h"

namespace {

std::filesystem::path UniqueTempIni(const char* name) {
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
           (std::string("lichii_") + name + "_" + std::to_string(stamp) + ".ini");
}

void WriteIni(const std::filesystem::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
}

} // namespace

TEST_CASE("ReadPerformVulkanCheckSetting respects qt-config.ini default and override",
          "[startup][config]") {
    const auto missing = UniqueTempIni("missing_vk_check");
    std::error_code ec;
    std::filesystem::remove(missing, ec);
    REQUIRE(ReadPerformVulkanCheckSetting(missing));

    const auto ini_path = UniqueTempIni("vk_check");
    {
        WriteIni(ini_path, "[Debugging]\nperform_vulkan_check\\default=true\n"
                           "perform_vulkan_check=false\n");
        REQUIRE(ReadPerformVulkanCheckSetting(ini_path));
    }
    {
        WriteIni(ini_path, "[Debugging]\nperform_vulkan_check\\default=false\n"
                           "perform_vulkan_check=false\n");
        REQUIRE_FALSE(ReadPerformVulkanCheckSetting(ini_path));
    }
    {
        WriteIni(ini_path, "[Debugging]\nperform_vulkan_check\\default=false\n"
                           "perform_vulkan_check=true\n");
        REQUIRE(ReadPerformVulkanCheckSetting(ini_path));
    }
    std::filesystem::remove(ini_path, ec);
}
