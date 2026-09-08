#pragma once

#include <SDL2/SDL.h>

class Controller
{
public:
    Controller();
    ~Controller();

    bool initialize();

    void handleEvent(const SDL_Event& event);

    bool upPressed() const;
    bool downPressed() const;
    bool startPressed() const;
    bool backPressed() const;

private:
    SDL_GameController* controller = nullptr;

    bool up = false;
    bool down = false;
    bool start = false;
    bool back = false;
};