#pragma once

#include <string>

struct HardwareProfile
{
    std::string cpuModel;
    int cpuThreads = 4;
    double totalRamGB = 4.0;
    bool isLowEnd = true;
    std::string tierName = "Low-End / Integrated (60 FPS Optimized)";

    int ps1ResolutionScale = 1;
    int ps2UpscaleMultiplier = 1;
    int ps2BlendingAccuracy = 1; // 1 = Basic (Fastest 60 FPS)
};

class HardwareOptimizer
{
public:
    static HardwareProfile detectHardware();

    static bool optimizeDuckStation(const std::string& settingsPath,
                                   const HardwareProfile& profile);

    static bool optimizePCSX2(const std::string& settingsPath,
                             const HardwareProfile& profile);

    static std::string getPCSX2SettingsPath();
    static std::string getDuckStationSettingsPath();
};
