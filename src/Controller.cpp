#include "Controller.h"

Controller::Controller()
{
}

Controller::~Controller()
{
    if (controller)
        SDL_GameControllerClose(controller);
}

bool Controller::initialize()
{
    for (int i = 0; i < SDL_NumJoysticks(); i++)
    {
        if (SDL_IsGameController(i))
        {
            controller = SDL_GameControllerOpen(i);

            if (controller)
                return true;
        }
    }

    return false;
}

void Controller::handleEvent(const SDL_Event& event)
{
    up = false;
    down = false;
    start = false;
    back = false;

    if (event.type == SDL_CONTROLLERBUTTONDOWN)
    {
        switch (event.cbutton.button)
        {
            case SDL_CONTROLLER_BUTTON_DPAD_UP:
                up = true;
                break;

            case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
                down = true;
                break;

            case SDL_CONTROLLER_BUTTON_A:
                start = true;
                break;

            case SDL_CONTROLLER_BUTTON_B:
                back = true;
                break;
        }
    }
}

bool Controller::upPressed() const
{
    return up;
}

bool Controller::downPressed() const
{
    return down;
}

bool Controller::startPressed() const
{
    return start;
}

bool Controller::backPressed() const
{
    return back;
}