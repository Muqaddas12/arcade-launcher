#pragma once

#include <string>

class Settings
{
public:
    Settings();

    bool load(const std::string& path);
    bool save(const std::string& path) const;

    // -------------------------
    // Display
    // -------------------------
    bool fullscreen = true;
    int screenWidth = 1280;
    int screenHeight = 720;
    int uiScale = 100;

    // -------------------------
    // Audio
    // -------------------------
    int volume = 100;
    bool mute = false;

    // -------------------------
    // DuckStation GPU
    // -------------------------
    std::string ps1Renderer = "Automatic";
    int ps1Resolution = 1;

    bool ps1Vsync = true;

    bool pgxpGeometry = false;
    bool pgxpTexture = true;
    bool pgxpDepth = false;

    std::string aspectRatio = "Auto (Game Native)";
};