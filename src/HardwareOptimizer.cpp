#include "HardwareOptimizer.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <sys/sysinfo.h>
#include <thread>
#include <vector>

static void copyBiosFiles(const std::string& sourceDir, const std::string& destDir)
{
    try
    {
        if (std::filesystem::exists(sourceDir))
        {
            std::filesystem::create_directories(destDir);
            for (const auto& entry : std::filesystem::directory_iterator(sourceDir))
            {
                if (entry.is_regular_file() && entry.file_size() > 0)
                {
                    std::string destFile = destDir + "/" + entry.path().filename().string();
                    if (!std::filesystem::exists(destFile))
                    {
                        std::filesystem::copy_file(entry.path(), destFile, std::filesystem::copy_options::overwrite_existing);
                    }
                }
            }
        }
    }
    catch (...) {}
}

static std::string trim(const std::string& str)
{
    size_t start = 0;
    size_t end = str.size();

    while (start < end && (str[start] == ' ' || str[start] == '\t' || str[start] == '\r'))
    {
        start++;
    }

    while (end > start && (str[end - 1] == ' ' || str[end - 1] == '\t' || str[end - 1] == '\r'))
    {
        end--;
    }

    return str.substr(start, end - start);
}

static bool setIniValue(
    std::vector<std::string>& lines,
    const std::string& sectionName,
    const std::string& key,
    const std::string& value)
{
    int sectionStart = -1;
    int sectionEnd = static_cast<int>(lines.size());

    for (size_t i = 0; i < lines.size(); ++i)
    {
        std::string line = trim(lines[i]);

        if (line.size() >= 2 && line.front() == '[' && line.back() == ']')
        {
            std::string section = trim(line.substr(1, line.size() - 2));

            if (section == sectionName)
            {
                sectionStart = static_cast<int>(i);

                for (size_t j = i + 1; j < lines.size(); ++j)
                {
                    std::string next = trim(lines[j]);
                    if (next.size() >= 2 && next.front() == '[' && next.back() == ']')
                    {
                        sectionEnd = static_cast<int>(j);
                        break;
                    }
                }
                break;
            }
        }
    }

    // Section doesn't exist yet - append at end
    if (sectionStart == -1)
    {
        if (!lines.empty() && !trim(lines.back()).empty())
            lines.push_back("");

        lines.push_back("[" + sectionName + "]");
        lines.push_back(key + " = " + value);
        return true;
    }

    // Search for existing key in the section
    for (int i = sectionStart + 1; i < sectionEnd; ++i)
    {
        std::string line = trim(lines[i]);
        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;

        size_t equal = line.find('=');
        if (equal == std::string::npos)
            continue;

        std::string existingKey = trim(line.substr(0, equal));
        if (existingKey == key)
        {
            lines[i] = key + " = " + value;
            return true;
        }
    }

    // Key not found - insert before next section
    lines.insert(lines.begin() + sectionEnd, key + " = " + value);
    return true;
}

HardwareProfile HardwareOptimizer::detectHardware()
{
    HardwareProfile profile;

    // Detect threads
    profile.cpuThreads = std::thread::hardware_concurrency();
    if (profile.cpuThreads == 0)
        profile.cpuThreads = 4;

    // Detect CPU model
    std::ifstream cpuinfo("/proc/cpuinfo");
    if (cpuinfo.is_open())
    {
        std::string line;
        while (std::getline(cpuinfo, line))
        {
            if (line.rfind("model name", 0) == 0)
            {
                size_t colon = line.find(':');
                if (colon != std::string::npos)
                {
                    profile.cpuModel = trim(line.substr(colon + 1));
                    break;
                }
            }
        }
    }

    if (profile.cpuModel.empty())
        profile.cpuModel = "Generic x86_64 CPU";

    // Detect RAM
    struct sysinfo si;
    if (sysinfo(&si) == 0)
    {
        profile.totalRamGB = static_cast<double>(si.totalram * si.mem_unit) /
                             (1024.0 * 1024.0 * 1024.0);
    }

    // Check CPU features for low-power indicators
    std::string cpuLower = profile.cpuModel;
    std::transform(cpuLower.begin(), cpuLower.end(), cpuLower.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    bool isLowPower = false;
    const std::vector<std::string> lowPowerKeywords = {
        "celeron", "pentium", "atom", "athlon", "core(tm) m",
        "a4-", "a6-", "a8-", "e1-", "e2-", "silver", "gold"
    };

    for (const auto& kw : lowPowerKeywords)
    {
        if (cpuLower.find(kw) != std::string::npos)
        {
            isLowPower = true;
            break;
        }
    }

    // Check for Intel/AMD mobile suffix: digits followed by 'u', 'y', or 'g'
    if (!isLowPower)
    {
        for (size_t i = 0; i + 1 < cpuLower.size(); ++i)
        {
            if (std::isdigit(static_cast<unsigned char>(cpuLower[i])) &&
                (cpuLower[i + 1] == 'u' || cpuLower[i + 1] == 'y'))
            {
                if (i + 2 >= cpuLower.size() || !std::isalpha(static_cast<unsigned char>(cpuLower[i + 2])))
                {
                    isLowPower = true;
                    break;
                }
            }
        }
    }

    // Target 60 FPS profile classification
    if (profile.cpuThreads <= 4 || profile.totalRamGB <= 6.0 || isLowPower)
    {
        profile.isLowEnd = true;
        profile.tierName = "Low-End / Integrated (Solid 60 FPS Target)";
        profile.ps1ResolutionScale = 1;     // 1x Native (no slowdowns)
        profile.ps2UpscaleMultiplier = 1;   // 1x Native (smooth 60 FPS)
        profile.ps2BlendingAccuracy = 1;    // 1 = Basic (Fastest)
    }
    else if (profile.cpuThreads >= 8 && profile.totalRamGB >= 12.0)
    {
        profile.isLowEnd = false;
        profile.tierName = "High-Performance (High Resolution 60 FPS)";
        profile.ps1ResolutionScale = 3;     // 3x 1080p
        profile.ps2UpscaleMultiplier = 3;   // 3x 1080p
        profile.ps2BlendingAccuracy = 2;    // Medium
    }
    else
    {
        profile.isLowEnd = false;
        profile.tierName = "Mid-Range (Balanced 60 FPS)";
        profile.ps1ResolutionScale = 2;     // 2x 720p
        profile.ps2UpscaleMultiplier = 2;   // 2x 720p
        profile.ps2BlendingAccuracy = 1;    // Basic
    }

    return profile;
}

std::string HardwareOptimizer::getDuckStationSettingsPath()
{
    const char* home = std::getenv("HOME");
    if (!home) return "";
    return std::string(home) + "/.local/share/duckstation/settings.ini";
}

std::string HardwareOptimizer::getPCSX2SettingsPath()
{
    const char* home = std::getenv("HOME");
    if (!home) return "";
    return std::string(home) + "/.config/PCSX2/inis/PCSX2.ini";
}

bool HardwareOptimizer::optimizeDuckStation(
    const std::string& settingsPath,
    const HardwareProfile& profile)
{
    if (settingsPath.empty())
        return false;

    // Ensure parent directory exists
    try {
        std::filesystem::create_directories(std::filesystem::path(settingsPath).parent_path());
    } catch (...) {}

    std::vector<std::string> lines;
    {
        std::ifstream file(settingsPath);
        if (file.is_open())
        {
            std::string line;
            while (std::getline(file, line))
                lines.push_back(line);
        }
    }

    // Always Fullscreen and Arcade behavior
    setIniValue(lines, "Main", "StartFullscreen", "true");
    setIniValue(lines, "Main", "HideCursorInFullscreen", "true");
    setIniValue(lines, "Main", "ConfirmPowerOff", "false");
    setIniValue(lines, "Main", "ApplyGameSettings", "true");
    setIniValue(lines, "Main", "EmulationSpeed", "1");
    setIniValue(lines, "Main", "SetupWizardIncomplete", "false");
    setIniValue(lines, "UI", "SetupWizardIncomplete", "false");
    setIniValue(lines, "AutoUpdater", "CheckAtStartup", "false");

    setIniValue(lines, "Display", "ExclusiveFullscreenControl", "Automatic");
    setIniValue(lines, "Display", "VSync", "true");
    setIniValue(lines, "Display", "DisableMailboxPresentation", "false");

    // 60 FPS Hardware Optimization
    setIniValue(lines, "GPU", "Renderer", "Automatic");
    setIniValue(lines, "GPU", "Adapter", ""); // Auto-detect primary GPU (avoid llvmpipe lock)
    setIniValue(lines, "GPU", "ResolutionScale", std::to_string(profile.ps1ResolutionScale));
    setIniValue(lines, "GPU", "ThreadedPresentation", "true");
    setIniValue(lines, "GPU", "DisableShaderCache", "false");

    // Disable heavy PGXP on low-end for 60 FPS
    if (profile.isLowEnd)
    {
        setIniValue(lines, "GPU", "PGXPEnable", "false");
        setIniValue(lines, "GPU", "PGXPTextureCorrection", "false");
        setIniValue(lines, "GPU", "PGXPDepthBuffer", "false");
    }

    setIniValue(lines, "CPU", "ExecutionMode", "Recompiler");
    setIniValue(lines, "CPU", "FastmemMode", "MMap");
    setIniValue(lines, "CPU", "RecompilerBlockLinking", "true");
    setIniValue(lines, "BIOS", "PatchFastBoot", "true");

    // Locate and configure PS1 BIOS
    std::string ps1BiosDir;
    if (std::filesystem::exists("/opt/malik-game-os/games/Bios/Ps1"))
        ps1BiosDir = "/opt/malik-game-os/games/Bios/Ps1";
    else if (std::filesystem::exists("games/Bios/Ps1"))
        ps1BiosDir = "games/Bios/Ps1";
    else if (std::filesystem::exists("../games/Bios/Ps1"))
        ps1BiosDir = "../games/Bios/Ps1";
    else {
        const char* home = std::getenv("HOME");
        if (home && std::filesystem::exists(std::string(home) + "/debianos/games/Bios/Ps1"))
            ps1BiosDir = std::string(home) + "/debianos/games/Bios/Ps1";
    }

    if (!ps1BiosDir.empty())
    {
        const char* home = std::getenv("HOME");
        std::string targetDir = ps1BiosDir;
        if (home)
        {
            std::string destBios = std::string(home) + "/.local/share/duckstation/bios";
            copyBiosFiles(ps1BiosDir, destBios);
            try {
                if (std::filesystem::exists(destBios + "/SCPH1001 (1).BIN") && !std::filesystem::exists(destBios + "/scph1001.bin"))
                    std::filesystem::copy_file(destBios + "/SCPH1001 (1).BIN", destBios + "/scph1001.bin");
                if (std::filesystem::exists(destBios + "/SCPH1001 (1).BIN") && !std::filesystem::exists(destBios + "/SCPH1001.BIN"))
                    std::filesystem::copy_file(destBios + "/SCPH1001 (1).BIN", destBios + "/SCPH1001.BIN");
            } catch (...) {}
            targetDir = destBios;
        }
        setIniValue(lines, "BIOS", "SearchDirectory", targetDir);
    }

    std::ofstream out(settingsPath);
    if (!out.is_open())
        return false;

    for (const auto& l : lines)
        out << l << '\n';
    out.close();

    // Mirror settings to ~/.config/duckstation/settings.ini
    const char* home = std::getenv("HOME");
    if (home)
    {
        try {
            std::string altPath = std::string(home) + "/.config/duckstation/settings.ini";
            std::filesystem::create_directories(std::filesystem::path(altPath).parent_path());
            std::filesystem::copy_file(settingsPath, altPath, std::filesystem::copy_options::overwrite_existing);
        } catch (...) {}
    }

    return true;
}

bool HardwareOptimizer::optimizePCSX2(
    const std::string& settingsPath,
    const HardwareProfile& profile)
{
    if (settingsPath.empty())
        return false;

    // Ensure parent directory exists
    try {
        std::filesystem::create_directories(std::filesystem::path(settingsPath).parent_path());
    } catch (...) {}

    std::vector<std::string> lines;
    {
        std::ifstream file(settingsPath);
        if (file.is_open())
        {
            std::string line;
            while (std::getline(file, line))
                lines.push_back(line);
        }
    }

    // Always Fullscreen & Clean Arcade UI
    setIniValue(lines, "UI", "StartFullscreen", "true");
    setIniValue(lines, "UI", "HideMouseCursor", "true");
    setIniValue(lines, "UI", "InhibitScreensaver", "true");
    setIniValue(lines, "UI", "ConfirmShutdown", "false");
    setIniValue(lines, "UI", "PauseOnFocusLoss", "false");
    setIniValue(lines, "UI", "DoubleClickTogglesFullscreen", "true");
    setIniValue(lines, "UI", "RenderToSeparateWindow", "false");
    setIniValue(lines, "UI", "SetupWizardIncomplete", "false");
    setIniValue(lines, "AutoUpdater", "CheckAtStartup", "false");

    // Speedhacks for 60 FPS
    setIniValue(lines, "EmuCore", "EnableFastBoot", "true");
    setIniValue(lines, "EmuCore", "EnablePatches", "true");

    // MTVU (Multi-Threaded microVU1) is essential for 60 FPS on multi-core CPUs!
    setIniValue(lines, "EmuCore/Speedhacks", "vuThread", "true");
    setIniValue(lines, "EmuCore/Speedhacks", "vu1Instant", "true");
    setIniValue(lines, "EmuCore/Speedhacks", "fastCDVD", "true");
    setIniValue(lines, "EmuCore/Speedhacks", "IntcStat", "true");
    setIniValue(lines, "EmuCore/Speedhacks", "WaitLoop", "true");
    setIniValue(lines, "EmuCore/Speedhacks", "vuFlagHack", "true");
    setIniValue(lines, "EmuCore/Speedhacks", "EECycleRate", "0");
    setIniValue(lines, "EmuCore/Speedhacks", "EECycleSkip", "0");

    // Recompiler performance
    setIniValue(lines, "EmuCore/CPU/Recompiler", "EnableFastmem", "true");
    setIniValue(lines, "EmuCore/CPU/Recompiler", "EnableEE", "true");
    setIniValue(lines, "EmuCore/CPU/Recompiler", "EnableIOP", "true");
    setIniValue(lines, "EmuCore/CPU/Recompiler", "EnableVU0", "true");
    setIniValue(lines, "EmuCore/CPU/Recompiler", "EnableVU1", "true");

    // Graphics / GS Optimization for 60 FPS
    setIniValue(lines, "EmuCore/GS", "upscale_multiplier", std::to_string(profile.ps2UpscaleMultiplier));
    setIniValue(lines, "EmuCore/GS", "accurate_blending_unit", std::to_string(profile.ps2BlendingAccuracy));
    setIniValue(lines, "EmuCore/GS", "SkipDuplicateFrames", "true");
    setIniValue(lines, "EmuCore/GS", "DisableShaderCache", "false");
    setIniValue(lines, "EmuCore/GS", "pcrtc_antiblur", "true");
    setIniValue(lines, "EmuCore/GS", "ThreadedPresentation", "true");
    setIniValue(lines, "EmuCore/GS", "DisableMailboxPresentation", "false");
    setIniValue(lines, "EmuCore/GS", "VsyncEnable", "false"); // Disable double-buffer vsync dips on low-end
    setIniValue(lines, "EmuCore/GS", "Renderer", "-1");       // Auto/best renderer

    // Locate and configure PS2 BIOS
    std::string ps2BiosDir;
    if (std::filesystem::exists("/opt/malik-game-os/games/Bios/Ps2"))
        ps2BiosDir = "/opt/malik-game-os/games/Bios/Ps2";
    else if (std::filesystem::exists("games/Bios/Ps2"))
        ps2BiosDir = "games/Bios/Ps2";
    else if (std::filesystem::exists("../games/Bios/Ps2"))
        ps2BiosDir = "../games/Bios/Ps2";
    else {
        const char* home = std::getenv("HOME");
        if (home && std::filesystem::exists(std::string(home) + "/debianos/games/Bios/Ps2"))
            ps2BiosDir = std::string(home) + "/debianos/games/Bios/Ps2";
    }

    if (!ps2BiosDir.empty())
    {
        const char* home = std::getenv("HOME");
        std::string targetDir = ps2BiosDir;
        if (home)
        {
            std::string destBios = std::string(home) + "/.config/PCSX2/bios";
            copyBiosFiles(ps2BiosDir, destBios);
            targetDir = destBios;
        }
        setIniValue(lines, "Folders", "Bios", targetDir);
        setIniValue(lines, "Filenames", "BIOS", "ps2-0200a-20040614-100909.bin");
    }

    std::ofstream out(settingsPath);
    if (!out.is_open())
        return false;

    for (const auto& l : lines)
        out << l << '\n';
    out.close();

    // Mirror settings to ~/.config/PCSX2/PCSX2.ini
    const char* home = std::getenv("HOME");
    if (home)
    {
        try {
            std::string altPath = std::string(home) + "/.config/PCSX2/PCSX2.ini";
            std::filesystem::create_directories(std::filesystem::path(altPath).parent_path());
            std::filesystem::copy_file(settingsPath, altPath, std::filesystem::copy_options::overwrite_existing);
        } catch (...) {}
    }

    return true;
}
