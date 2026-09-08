#include "DuckStationSettings.h"

#include <fstream>

static std::string boolValue(bool value)
{
    return value ? "true" : "false";
}

bool DuckStationSettings::write(const Settings& settings,
                                const std::string& path)
{
    std::ofstream file(path);

    if (!file.is_open())
        return false;

    file << "[Main]\n";
    file << "SettingsVersion = 3\n";
    file << "StartFullscreen = "
         << boolValue(settings.fullscreen) << "\n";
    file << "ApplyGameSettings = true\n";

    file << "\n[GPU]\n";

    file << "Renderer = "
         << settings.ps1Renderer << "\n";

    file << "Adapter = \n";

    file << "ResolutionScale = "
         << settings.ps1Resolution << "\n";

    file << "PGXPEnable = "
         << boolValue(settings.pgxpGeometry) << "\n";

    file << "PGXPTextureCorrection = "
         << boolValue(settings.pgxpTexture) << "\n";

    file << "PGXPDepthBuffer = "
         << boolValue(settings.pgxpDepth) << "\n";

    file << "PGXPCulling = true\n";
    file << "PGXPVertexCache = false\n";
    file << "PGXPCPU = false\n";
    file << "PGXPPreserveProjFP = false\n";
    file << "PGXPTolerance = -1.000000\n";

    file << "\n[Display]\n";

    file << "AspectRatio = "
         << settings.aspectRatio << "\n";

    file << "Fullscreen = "
         << boolValue(settings.fullscreen) << "\n";

    file << "VSync = "
         << boolValue(settings.ps1Vsync) << "\n";

    file << "Stretch = false\n";
    file << "IntegerScaling = false\n";

    file << "\n[Audio]\n";

    file << "OutputVolume = "
         << settings.volume << "\n";

    file << "OutputMuted = "
         << (settings.mute ? 1 : 0) << "\n";

    return true;
}
