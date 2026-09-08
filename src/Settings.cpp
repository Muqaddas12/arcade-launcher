#include "Settings.h"

#include <fstream>
#include <string>

Settings::Settings()
{
}

bool Settings::load(const std::string& path)
{
    std::ifstream file(path);

    if (!file.is_open())
        return false;

    std::string line;
    std::string section;

    while (std::getline(file, line))
    {
        if (line.empty())
            continue;

        if (line[0] == '#')
            continue;

        if (line[0] == '[' && line.back() == ']')
        {
            section = line.substr(1, line.size() - 2);
            continue;
        }

        const size_t equals = line.find('=');

        if (equals == std::string::npos)
            continue;

        std::string key = line.substr(0, equals);
        std::string value = line.substr(equals + 1);

        // Remove leading spaces
        while (!key.empty() && key.front() == ' ')
            key.erase(key.begin());

        // Remove trailing spaces
        while (!key.empty() && key.back() == ' ')
            key.pop_back();

        while (!value.empty() && value.front() == ' ')
            value.erase(value.begin());

        if (section == "display")
        {
            if (key == "fullscreen")
                fullscreen = (value == "true");

            else if (key == "width")
                screenWidth = std::stoi(value);

            else if (key == "height")
                screenHeight = std::stoi(value);

            else if (key == "ui_scale")
                uiScale = std::stoi(value);
        }

        else if (section == "audio")
        {
            if (key == "volume")
                volume = std::stoi(value);

            else if (key == "mute")
                mute = (value == "true");
        }

        else if (section == "duckstation")
        {
            if (key == "renderer")
                ps1Renderer = value;

            else if (key == "resolution")
                ps1Resolution = std::stoi(value);

            else if (key == "vsync")
                ps1Vsync = (value == "true");

            else if (key == "pgxp_geometry")
                pgxpGeometry = (value == "true");

            else if (key == "pgxp_texture")
                pgxpTexture = (value == "true");

            else if (key == "pgxp_depth")
                pgxpDepth = (value == "true");

            else if (key == "aspect_ratio")
                aspectRatio = value;
        }
    }

    return true;
}

bool Settings::save(const std::string& path) const
{
    std::ofstream file(path);

    if (!file.is_open())
        return false;

    file << "[display]\n";
    file << "fullscreen=" << (fullscreen ? "true" : "false") << '\n';
    file << "width=" << screenWidth << '\n';
    file << "height=" << screenHeight << '\n';
    file << "ui_scale=" << uiScale << '\n';

    file << "\n[audio]\n";
    file << "volume=" << volume << '\n';
    file << "mute=" << (mute ? "true" : "false") << '\n';

    file << "\n[duckstation]\n";
    file << "renderer=" << ps1Renderer << '\n';
    file << "resolution=" << ps1Resolution << '\n';
    file << "vsync=" << (ps1Vsync ? "true" : "false") << '\n';
    file << "pgxp_geometry=" << (pgxpGeometry ? "true" : "false") << '\n';
    file << "pgxp_texture=" << (pgxpTexture ? "true" : "false") << '\n';
    file << "pgxp_depth=" << (pgxpDepth ? "true" : "false") << '\n';
    file << "aspect_ratio=" << aspectRatio << '\n';

    return true;
}