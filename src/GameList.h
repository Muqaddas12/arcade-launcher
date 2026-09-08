#pragma once

#include <string>
#include <vector>

struct Game
{
    std::string name;
    std::string system;
    std::string path;
};

class GameList
{
public:
    void clear();

    void addGame(const Game& game);

    void scanGames(const std::string& basePath);

    const std::vector<Game>& getGames() const;

    void selectNext();
    void selectPrevious();

    const Game* getSelectedGame() const;

private:
    std::vector<Game> games;
    int selectedIndex = 0;
};