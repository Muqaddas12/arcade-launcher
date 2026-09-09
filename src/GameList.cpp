
#include "GameList.h"

#include <filesystem>
#include <iostream>
#include <algorithm>
#include <cctype>
namespace fs = std::filesystem;

static bool hasExtension(
    const fs::path& path,
    const std::vector<std::string>& extensions)
{
    std::string ext = path.extension().string();

    std::transform(
        ext.begin(),
        ext.end(),
        ext.begin(),
        [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        }
    );

    for (const auto& allowed : extensions)
    {
        if (ext == allowed)
            return true;
    }

    return false;
}

void GameList::clear()
{
    games.clear();
    selectedIndex = 0;
}

void GameList::addGame(const Game& game)
{
    games.push_back(game);
}

void GameList::scanGames(const std::string& basePath)
{
    clear();

    const fs::path base(basePath);

    if (!fs::exists(base))
    {
        std::cerr << "Games directory not found: "
                  << basePath << '\n';
        return;
    }

    // =========================
    // PS1
    // =========================

    const fs::path ps1Path = base / "PS1";

    if (fs::exists(ps1Path))
    {
        for (const auto& gameFolder : fs::directory_iterator(ps1Path))
        {
            if (!gameFolder.is_directory())
                continue;

            const fs::path folder = gameFolder.path();

            for (const auto& file : fs::directory_iterator(folder))
            {
                if (!file.is_regular_file())
                    continue;

                // Ignore 0-byte dummy files
                try {
                    if (file.file_size() < 20)
                        continue;
                } catch (...) {
                    continue;
                }

                if (hasExtension(
                        file.path(),
                        {".cue", ".chd", ".iso", ".img"}))
                {
                    addGame({
                        folder.filename().string(),
                        "PS1",
                        file.path().string()
                    });

                    break;
                }
            }
        }
    }

    // =========================
    // PS2
    // =========================

    const fs::path ps2Path = base / "PS2";

    if (fs::exists(ps2Path))
    {
        for (const auto& gameFolder : fs::directory_iterator(ps2Path))
        {
            if (!gameFolder.is_directory())
                continue;

            const fs::path folder = gameFolder.path();

            for (const auto& file : fs::directory_iterator(folder))
            {
                if (!file.is_regular_file())
                    continue;

                // Ignore 0-byte dummy files (ISOs/BINs must be at least 1MB)
                try {
                    if (file.file_size() < 1024 * 1024)
                        continue;
                } catch (...) {
                    continue;
                }

                if (hasExtension(
                        file.path(),
                        {".iso", ".chd", ".cso",".bin",}))
                {
                    addGame({
                        folder.filename().string(),
                        "PS2",
                        file.path().string()
                    });

                    break;
                }
            }
        }
    }

    // Sort alphabetically
    std::sort(
        games.begin(),
        games.end(),
        [](const Game& a, const Game& b)
        {
            return a.name < b.name;
        }
    );

    std::cout << "Games found: "
              << games.size()
              << '\n';

    for (const auto& game : games)
    {
        std::cout << "  "
                  << game.system
                  << " | "
                  << game.name
                  << " | "
                  << game.path
                  << '\n';
    }
}

const std::vector<Game>& GameList::getGames() const
{
    return games;
}

void GameList::selectNext()
{
    if (games.empty())
        return;

    selectedIndex++;

    if (selectedIndex >= static_cast<int>(games.size()))
        selectedIndex = 0;
}

void GameList::selectPrevious()
{
    if (games.empty())
        return;

    selectedIndex--;

    if (selectedIndex < 0)
        selectedIndex =
            static_cast<int>(games.size()) - 1;
}

const Game* GameList::getSelectedGame() const
{
    if (games.empty())
        return nullptr;

    return &games[selectedIndex];
}