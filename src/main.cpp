#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <filesystem>
#include "GameList.h"
#include "Controller.h"
#include "Settings.h"
#include "HardwareOptimizer.h"

// ============================================================
// Path Resolution (Works in Dev Build & Live OS /opt/malik-game-os)
// ============================================================

static std::string getBaseDirectory()
{
    // Check standard installation path first (Live OS / VM)
    if (std::filesystem::exists("/opt/malik-game-os/config/settings.ini"))
        return "/opt/malik-game-os";

    // Running from project root
    if (std::filesystem::exists("config/settings.ini"))
        return ".";

    // Running from build/ directory
    if (std::filesystem::exists("../config/settings.ini"))
        return "..";

    const char* home = std::getenv("HOME");
    if (home && std::filesystem::exists(std::string(home) + "/debianos/config/settings.ini"))
        return std::string(home) + "/debianos";

    return ".";
}

static std::string resolveBinary(const std::string& name, const std::string& baseDir)
{
    // Check baseDir/bin/
    std::string candidate = baseDir + "/bin/" + name;
    if (std::filesystem::exists(candidate))
        return candidate;

    // Check system-wide paths
    if (std::filesystem::exists("/usr/local/bin/" + name))
        return "/usr/local/bin/" + name;

    if (std::filesystem::exists("/usr/bin/" + name))
        return "/usr/bin/" + name;

    const char* home = std::getenv("HOME");
    if (home)
    {
        candidate = std::string(home) + "/debianos/bin/" + name;
        if (std::filesystem::exists(candidate))
            return candidate;
    }

    return name;
}

// ============================================================
// DuckStation configuration
// (applied silently at launch time – not exposed in UI)
// ============================================================

struct DuckStationConfig
{
    std::string renderer    = "Automatic";
    std::string adapter     = "";
    int         resolutionScale = 1;
    bool        vsync       = true;
    bool        pgxpGeometry = false;
    bool        pgxpTexture  = true;
    bool        pgxpDepth    = false;
    std::string aspectRatio = "Auto (Game Native)";
};

// ------------------------------------------------------------
// Trim
// ------------------------------------------------------------

static std::string trim(const std::string& value)
{
    size_t start = 0;
    size_t end   = value.size();

    while (start < end &&
           (value[start] == ' '  ||
            value[start] == '\t' ||
            value[start] == '\r'))
    {
        start++;
    }

    while (end > start &&
           (value[end - 1] == ' '  ||
            value[end - 1] == '\t' ||
            value[end - 1] == '\r'))
    {
        end--;
    }

    return value.substr(start, end - start);
}

// ------------------------------------------------------------
// Boolean conversion
// ------------------------------------------------------------

static bool parseBool(const std::string& value, bool defaultValue)
{
    std::string v = trim(value);

    std::transform(
        v.begin(), v.end(), v.begin(),
        [](unsigned char c)
        {
            return static_cast<char>(std::tolower(c));
        });

    if (v == "true"  || v == "1" || v == "yes") return true;
    if (v == "false" || v == "0" || v == "no")  return false;

    return defaultValue;
}

// ============================================================
// DuckStation INI helpers
// ============================================================

static bool readDuckStationConfig(
    const std::string& path,
    DuckStationConfig& config)
{
    std::ifstream file(path);

    if (!file.is_open())
        return false;

    std::string line;
    std::string section;

    while (std::getline(file, line))
    {
        line = trim(line);

        if (line.empty())
            continue;

        if (line[0] == '#' || line[0] == ';')
            continue;

        if (line.front() == '[' && line.back() == ']')
        {
            section = trim(line.substr(1, line.size() - 2));
            continue;
        }

        size_t equal = line.find('=');
        if (equal == std::string::npos)
            continue;

        std::string key   = trim(line.substr(0, equal));
        std::string value = trim(line.substr(equal + 1));

        // ----------------------------------------------------
        // GPU
        // ----------------------------------------------------

        if (section == "GPU")
        {
            if (key == "Renderer")
            {
                config.renderer = value;
            }
            else if (key == "Adapter")
            {
                config.adapter = value;
            }
            else if (key == "ResolutionScale")
            {
                try   { config.resolutionScale = std::stoi(value); }
                catch (...) { config.resolutionScale = 1; }

                if (config.resolutionScale < 1)  config.resolutionScale = 1;
                if (config.resolutionScale > 16) config.resolutionScale = 16;
            }
            else if (key == "PGXPEnable")
            {
                config.pgxpGeometry =
                    parseBool(value, config.pgxpGeometry);
            }
            else if (key == "PGXPTextureCorrection")
            {
                config.pgxpTexture =
                    parseBool(value, config.pgxpTexture);
            }
            else if (key == "PGXPDepthBuffer")
            {
                config.pgxpDepth =
                    parseBool(value, config.pgxpDepth);
            }
        }

        // ----------------------------------------------------
        // Display
        // ----------------------------------------------------

        else if (section == "Display")
        {
            if (key == "VSync")
            {
                config.vsync =
                    parseBool(value, config.vsync);
            }
            else if (key == "AspectRatio")
            {
                config.aspectRatio = value;
            }
        }
    }

    return true;
}

// ============================================================
// Replace / insert an INI value
// ============================================================

static bool setIniValue(
    std::vector<std::string>& lines,
    const std::string&        sectionName,
    const std::string&        key,
    const std::string&        value)
{
    int sectionStart = -1;
    int sectionEnd   = static_cast<int>(lines.size());

    for (size_t i = 0; i < lines.size(); ++i)
    {
        std::string line = trim(lines[i]);

        if (line.size() >= 2 &&
            line.front() == '[' &&
            line.back()  == ']')
        {
            std::string section =
                trim(line.substr(1, line.size() - 2));

            if (section == sectionName)
            {
                sectionStart = static_cast<int>(i);

                for (size_t j = i + 1; j < lines.size(); ++j)
                {
                    std::string next = trim(lines[j]);

                    if (next.size() >= 2 &&
                        next.front() == '[' &&
                        next.back()  == ']')
                    {
                        sectionEnd = static_cast<int>(j);
                        break;
                    }
                }

                break;
            }
        }
    }

    // --------------------------------------------------------
    // Section does not exist – append
    // --------------------------------------------------------

    if (sectionStart == -1)
    {
        if (!lines.empty() && !trim(lines.back()).empty())
            lines.push_back("");

        lines.push_back("[" + sectionName + "]");
        lines.push_back(key + " = " + value);
        return true;
    }

    // --------------------------------------------------------
    // Search key inside section
    // --------------------------------------------------------

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

    // Key not found – insert at end of section
    lines.insert(lines.begin() + sectionEnd, key + " = " + value);
    return true;
}

// ============================================================
// Write DuckStation configuration
// ============================================================

static bool writeDuckStationConfig(
    const std::string&       path,
    const DuckStationConfig& config)
{
    std::vector<std::string> lines;

    {
        std::ifstream file(path);

        if (file.is_open())
        {
            std::string line;

            while (std::getline(file, line))
                lines.push_back(line);
        }
    }

    if (lines.empty())
    {
        lines.push_back("[Main]");
        lines.push_back("SettingsVersion = 3");
        lines.push_back("StartFullscreen = true");
        lines.push_back("ApplyGameSettings = true");
        lines.push_back("");
        lines.push_back("[GPU]");
        lines.push_back("");
        lines.push_back("[Display]");
        lines.push_back("");
    }

    setIniValue(lines, "GPU", "Renderer",
                config.renderer);

    setIniValue(lines, "GPU", "Adapter",
                config.adapter);

    setIniValue(lines, "GPU", "ResolutionScale",
                std::to_string(config.resolutionScale));

    setIniValue(lines, "GPU", "PGXPEnable",
                config.pgxpGeometry ? "true" : "false");

    setIniValue(lines, "GPU", "PGXPTextureCorrection",
                config.pgxpTexture ? "true" : "false");

    setIniValue(lines, "GPU", "PGXPDepthBuffer",
                config.pgxpDepth ? "true" : "false");

    setIniValue(lines, "Display", "VSync",
                config.vsync ? "true" : "false");

    setIniValue(lines, "Display", "AspectRatio",
                config.aspectRatio);

    std::ofstream file(path);

    if (!file.is_open())
    {
        std::cerr
            << "Could not write DuckStation settings: "
            << path << '\n';
        return false;
    }

    for (const std::string& line : lines)
        file << line << '\n';

    return true;
}

// ============================================================
// DuckStation settings path
// ============================================================

static std::string getDuckStationSettingsPath()
{
    const char* home = std::getenv("HOME");
    if (!home) return "";
    return std::string(home) +
           "/.local/share/duckstation/settings.ini";
}

// ============================================================
// Save Malik settings
// ============================================================

static void saveMalikSettings(
    Settings&          settings,
    const std::string& path)
{
    if (!settings.save(path))
        std::cerr << "Failed to save Malik settings.\n";
}

// ============================================================
// Launch Game
// Non-blocking – forks the emulator and returns the child PID.
// Returns -1 on failure.
// ============================================================

static pid_t launchGame(
    const Game&            game,
    const Settings&        settings,
    const HardwareProfile& hwProfile,
    const std::string&     duckStationSettingsPath,
    const std::string&     pcsx2SettingsPath)
{
    // ----------------------------------------------------------
    // Resolve wrapper script paths dynamically
    // ----------------------------------------------------------

    std::string baseDir = getBaseDirectory();
    std::string duckstationBin = resolveBinary("duckstation", baseDir);
    std::string pcsx2Bin       = resolveBinary("pcsx2", baseDir);

    // ----------------------------------------------------------
    // Build argument list  (run via bash so the script executes)
    // ----------------------------------------------------------

    std::vector<std::string> args;
    args.push_back("/bin/bash");

    if (game.system == "PS1")
    {
        // Re-optimize DuckStation configuration for detected hardware + always fullscreen
        HardwareOptimizer::optimizeDuckStation(duckStationSettingsPath, hwProfile);

        args.push_back(duckstationBin);
        args.push_back("-fullscreen"); // Always Fullscreen
        args.push_back("-batch");      // Clean arcade exit
        args.push_back("-fastboot");   // Fast boot into game at 60 FPS
        args.push_back(game.path);
    }
    else if (game.system == "PS2")
    {
        // Re-optimize PCSX2 configuration for detected hardware + always fullscreen
        HardwareOptimizer::optimizePCSX2(pcsx2SettingsPath, hwProfile);

        args.push_back(pcsx2Bin);
        args.push_back("-bigpicture"); // Always Fullscreen TV/Arcade interface
        args.push_back(game.path);
    }
    else
    {
        std::cerr << "Unknown system: " << game.system << '\n';
        return -1;
    }

    // ----------------------------------------------------------
    // Build null-terminated argv
    // ----------------------------------------------------------

    std::vector<char*> argv;

    for (auto& s : args)
        argv.push_back(const_cast<char*>(s.c_str()));

    argv.push_back(nullptr);

    std::cout << "Launching: " << game.name   << '\n';
    std::cout << "System: "   << game.system  << '\n';
    std::cout << "Path: "     << game.path    << '\n';

    // ----------------------------------------------------------
    // Fork
    // ----------------------------------------------------------

    pid_t pid = fork();

    if (pid == 0)
    {
        // Child process – replace image with emulator
        execvp(argv[0], argv.data());

        // execvp only returns on failure
        std::cerr << "exec failed: " << argv[0] << '\n';
        _exit(1);
    }

    if (pid < 0)
    {
        std::cerr << "fork() failed.\n";
        return -1;
    }

    std::cout << "Game PID: " << pid << '\n';
    return pid;
}

// ============================================================
// Draw Text
// ============================================================

static void drawText(
    SDL_Renderer*      renderer,
    TTF_Font*          font,
    const std::string& text,
    int                x,
    int                y,
    SDL_Color          color)
{
    SDL_Surface* surface =
        TTF_RenderUTF8_Blended(font, text.c_str(), color);

    if (!surface)
        return;

    SDL_Texture* texture =
        SDL_CreateTextureFromSurface(renderer, surface);

    if (!texture)
    {
        SDL_FreeSurface(surface);
        return;
    }

    SDL_Rect rect{ x, y, surface->w, surface->h };

    SDL_RenderCopy(renderer, texture, nullptr, &rect);

    SDL_FreeSurface(surface);
    SDL_DestroyTexture(texture);
}

// ============================================================
// Main
// ============================================================

int main()
{
    // ========================================================
    // SDL
    // ========================================================

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0)
    {
        std::cerr
            << "SDL initialization failed: "
            << SDL_GetError() << '\n';
        return 1;
    }

    // ========================================================
    // SDL_ttf
    // ========================================================

    if (TTF_Init() != 0)
    {
        std::cerr
            << "TTF initialization failed: "
            << TTF_GetError() << '\n';
        SDL_Quit();
        return 1;
    }

    // ========================================================
    // Base directory & Malik settings
    // ========================================================

    std::string baseDir = getBaseDirectory();
    std::cout << "Project Base Directory: " << baseDir << '\n';

    Settings settings;
    const std::string settingsPath = baseDir + "/config/settings.ini";
    settings.load(settingsPath);

    // ========================================================
    // Hardware Auto-Detection & 60 FPS Optimization
    // ========================================================

    HardwareProfile hwProfile = HardwareOptimizer::detectHardware();

    std::cout << "========================================\n";
    std::cout << "Auto-Detected Hardware Profile:\n";
    std::cout << "  CPU: " << hwProfile.cpuModel << '\n';
    std::cout << "  Threads: " << hwProfile.cpuThreads
              << " | Total RAM: " << static_cast<int>(hwProfile.totalRamGB * 10.0) / 10.0 << " GB\n";
    std::cout << "  Profile: " << hwProfile.tierName << '\n';
    std::cout << "  PS1 Scale: " << hwProfile.ps1ResolutionScale
              << "x | PS2 Scale: " << hwProfile.ps2UpscaleMultiplier << "x\n";
    std::cout << "  Target: Always Fullscreen @ 60 FPS\n";
    std::cout << "========================================\n";

    const std::string duckStationSettingsPath =
        HardwareOptimizer::getDuckStationSettingsPath();
    const std::string pcsx2SettingsPath =
        HardwareOptimizer::getPCSX2SettingsPath();

    // Pre-apply 60 FPS and Fullscreen optimizations to both emulators
    HardwareOptimizer::optimizeDuckStation(duckStationSettingsPath, hwProfile);
    HardwareOptimizer::optimizePCSX2(pcsx2SettingsPath, hwProfile);

    // ========================================================
    // Window
    // ========================================================

    Uint32 windowFlags = SDL_WINDOW_SHOWN;

    if (settings.fullscreen)
        windowFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

    SDL_Window* window = SDL_CreateWindow(
        "Malik Game OS",
        SDL_WINDOWPOS_CENTERED,
        SDL_WINDOWPOS_CENTERED,
        settings.screenWidth,
        settings.screenHeight,
        windowFlags);

    if (!window)
    {
        std::cerr
            << "Window creation failed: "
            << SDL_GetError() << '\n';
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    // ========================================================
    // Renderer
    // ========================================================

    SDL_Renderer* renderer = SDL_CreateRenderer(
        window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);

    if (!renderer)
    {
        std::cerr
            << "Renderer creation failed: "
            << SDL_GetError() << '\n';
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    // ========================================================
    // Game list
    // ========================================================

    GameList gameList;
    gameList.scanGames(baseDir + "/games");

    // ========================================================
    // Controller
    // ========================================================

    Controller controller;

    if (controller.initialize())
        std::cout << "Controller detected.\n";
    else
        std::cout << "No controller detected.\n";

    // ========================================================
    // Fonts
    // ========================================================

    const char* fontPath =
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";

    TTF_Font* font      = TTF_OpenFont(fontPath, 28);
    TTF_Font* titleFont = TTF_OpenFont(fontPath, 44);

    if (!font || !titleFont)
    {
        std::cerr << "Font error: " << TTF_GetError() << '\n';

        if (font)      TTF_CloseFont(font);
        if (titleFont) TTF_CloseFont(titleFont);

        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        TTF_Quit();
        SDL_Quit();
        return 1;
    }

    // ========================================================
    // Screen states
    // ========================================================

    enum class Screen
    {
        MainMenu,
        Games,
        Settings,
        DisplaySettings,
        AudioSettings,
        SystemSettings
    };

    Screen screen = Screen::MainMenu;

    int mainSelection     = 0;
    int settingsSelection = 0;
    int displaySelection  = 0;
    int audioSelection    = 0;

    // ========================================================
    // Game-running state
    // ========================================================

    pid_t gamePid     = -1;
    bool  gameRunning = false;

    bool      running = true;
    SDL_Event event;

    // ========================================================
    // Main loop
    // ========================================================

    while (running)
    {
        // ----------------------------------------------------
        // Check if game process has exited
        // ----------------------------------------------------

        if (gameRunning && gamePid > 0)
        {
            int status = 0;
            pid_t result = waitpid(gamePid, &status, WNOHANG);

            if (result == gamePid)
            {
                // Game exited – restore the launcher
                gameRunning = false;
                gamePid     = -1;

                SDL_ShowWindow(window);
                if (settings.fullscreen)
                {
                    SDL_SetWindowFullscreen(window, SDL_WINDOW_FULLSCREEN_DESKTOP);
                }
                SDL_RaiseWindow(window);

                std::cout
                    << "Game exited. "
                    << "Returning to Malik Game OS.\n";
            }
            else
            {
                // Game is still running!
                // Drain events without processing game input
                while (SDL_PollEvent(&event))
                {
                    if (event.type == SDL_QUIT)
                    {
                        running = false;
                    }
                }

                // Sleep to consume 0% CPU while game is playing
                SDL_Delay(50);
                continue; // Do not render anything while game is active!
            }
        }

        // ----------------------------------------------------
        // Event loop
        // ----------------------------------------------------

        while (SDL_PollEvent(&event))
        {
            controller.handleEvent(event);

            // ------------------------------------------------
            // Window close
            // ------------------------------------------------

            if (event.type == SDL_QUIT)
            {
                running = false;
            }

            // ------------------------------------------------
            // Windows / Super key
            // Always handled, even while a game is running.
            // Brings the launcher window to the front.
            // ------------------------------------------------

            if (event.type == SDL_KEYDOWN &&
                (event.key.keysym.sym == SDLK_LGUI ||
                 event.key.keysym.sym == SDLK_RGUI))
            {
                SDL_RestoreWindow(window);
                SDL_RaiseWindow(window);
            }

            // ------------------------------------------------
            // While game is running: block all other input
            // ------------------------------------------------

            if (gameRunning)
                continue;

            // =================================================
            // Keyboard input  (launcher only)
            // =================================================

            if (event.type == SDL_KEYDOWN && !event.key.repeat)
            {
                SDL_Keycode key = event.key.keysym.sym;

                bool up     = (key == SDLK_UP);
                bool down   = (key == SDLK_DOWN);
                bool select = (key == SDLK_RETURN ||
                               key == SDLK_SPACE  ||
                               key == SDLK_a);
                bool back   = (key == SDLK_ESCAPE ||
                               key == SDLK_b);
                bool quit   = (key == SDLK_q);

                if (quit)
                {
                    running = false;
                    continue;
                }

                // -----------------------------------------
                // MAIN MENU
                // -----------------------------------------

                if (screen == Screen::MainMenu)
                {
                    if (up)
                    {
                        mainSelection--;
                        if (mainSelection < 0) mainSelection = 2;
                    }

                    if (down)
                    {
                        mainSelection++;
                        if (mainSelection > 2) mainSelection = 0;
                    }

                    if (select)
                    {
                        if (mainSelection == 0)
                            screen = Screen::Games;
                        else if (mainSelection == 1)
                            screen = Screen::Settings;
                        else
                            running = false;
                    }
                }

                // -----------------------------------------
                // GAMES
                // -----------------------------------------

                else if (screen == Screen::Games)
                {
                    if (up)   gameList.selectPrevious();
                    if (down) gameList.selectNext();

                    if (select)
                    {
                        if (auto game = gameList.getSelectedGame())
                        {
                            pid_t pid = launchGame(
                                *game, settings,
                                hwProfile,
                                duckStationSettingsPath,
                                pcsx2SettingsPath);

                            if (pid > 0)
                            {
                                gamePid     = pid;
                                gameRunning = true;
                                SDL_HideWindow(window);
                            }
                        }
                    }

                    if (back)
                        screen = Screen::MainMenu;
                }

                // -----------------------------------------
                // SETTINGS
                // -----------------------------------------

                else if (screen == Screen::Settings)
                {
                    if (up)
                    {
                        settingsSelection--;
                        if (settingsSelection < 0)
                            settingsSelection = 2;
                    }

                    if (down)
                    {
                        settingsSelection++;
                        if (settingsSelection > 2)
                            settingsSelection = 0;
                    }

                    if (select)
                    {
                        if (settingsSelection == 0)
                            screen = Screen::DisplaySettings;
                        else if (settingsSelection == 1)
                            screen = Screen::AudioSettings;
                        else
                            screen = Screen::SystemSettings;
                    }

                    if (back)
                        screen = Screen::MainMenu;
                }

                // -----------------------------------------
                // DISPLAY SETTINGS
                // -----------------------------------------

                else if (screen == Screen::DisplaySettings)
                {
                    if (up)
                    {
                        displaySelection--;
                        if (displaySelection < 0)
                            displaySelection = 2;
                    }

                    if (down)
                    {
                        displaySelection++;
                        if (displaySelection > 2)
                            displaySelection = 0;
                    }

                    if (select)
                    {
                        if (displaySelection == 0)
                        {
                            settings.fullscreen =
                                !settings.fullscreen;

                            saveMalikSettings(
                                settings, settingsPath);

                            SDL_SetWindowFullscreen(
                                window,
                                settings.fullscreen
                                    ? SDL_WINDOW_FULLSCREEN_DESKTOP
                                    : 0);
                        }
                        else if (displaySelection == 1)
                        {
                            settings.screenWidth =
                                (settings.screenWidth == 1280)
                                    ? 1920
                                    : 1280;

                            saveMalikSettings(
                                settings, settingsPath);
                        }
                        else
                        {
                            settings.screenHeight =
                                (settings.screenHeight == 720)
                                    ? 1080
                                    : 720;

                            saveMalikSettings(
                                settings, settingsPath);
                        }
                    }

                    if (back)
                        screen = Screen::Settings;
                }

                // -----------------------------------------
                // AUDIO SETTINGS
                // -----------------------------------------

                else if (screen == Screen::AudioSettings)
                {
                    if (up)
                    {
                        audioSelection--;
                        if (audioSelection < 0)
                            audioSelection = 1;
                    }

                    if (down)
                    {
                        audioSelection++;
                        if (audioSelection > 1)
                            audioSelection = 0;
                    }

                    if (select)
                    {
                        if (audioSelection == 0)
                        {
                            settings.volume += 10;
                            if (settings.volume > 100)
                                settings.volume = 0;
                        }
                        else
                        {
                            settings.mute = !settings.mute;
                        }

                        saveMalikSettings(settings, settingsPath);
                    }

                    if (back)
                        screen = Screen::Settings;
                }

                // -----------------------------------------
                // SYSTEM SETTINGS
                // -----------------------------------------

                else if (screen == Screen::SystemSettings)
                {
                    if (back)
                        screen = Screen::Settings;
                }
            }

            // =================================================
            // Controller navigation  (launcher only)
            // =================================================

            if (controller.upPressed())
            {
                if (screen == Screen::MainMenu)
                {
                    mainSelection--;
                    if (mainSelection < 0) mainSelection = 2;
                }
                else if (screen == Screen::Games)
                {
                    gameList.selectPrevious();
                }
                else if (screen == Screen::Settings)
                {
                    settingsSelection--;
                    if (settingsSelection < 0) settingsSelection = 2;
                }
                else if (screen == Screen::DisplaySettings)
                {
                    displaySelection--;
                    if (displaySelection < 0) displaySelection = 2;
                }
                else if (screen == Screen::AudioSettings)
                {
                    audioSelection--;
                    if (audioSelection < 0) audioSelection = 1;
                }
            }

            if (controller.downPressed())
            {
                if (screen == Screen::MainMenu)
                {
                    mainSelection++;
                    if (mainSelection > 2) mainSelection = 0;
                }
                else if (screen == Screen::Games)
                {
                    gameList.selectNext();
                }
                else if (screen == Screen::Settings)
                {
                    settingsSelection++;
                    if (settingsSelection > 2) settingsSelection = 0;
                }
                else if (screen == Screen::DisplaySettings)
                {
                    displaySelection++;
                    if (displaySelection > 2) displaySelection = 0;
                }
                else if (screen == Screen::AudioSettings)
                {
                    audioSelection++;
                    if (audioSelection > 1) audioSelection = 0;
                }
            }

            // Controller A / Start
            if (controller.startPressed())
            {
                if (screen == Screen::MainMenu)
                {
                    if (mainSelection == 0)
                        screen = Screen::Games;
                    else if (mainSelection == 1)
                        screen = Screen::Settings;
                    else
                        running = false;
                }
                else if (screen == Screen::Games)
                {
                    if (auto game = gameList.getSelectedGame())
                    {
                        pid_t pid = launchGame(
                            *game, settings,
                            hwProfile,
                            duckStationSettingsPath,
                            pcsx2SettingsPath);

                        if (pid > 0)
                        {
                            gamePid     = pid;
                            gameRunning = true;
                            SDL_HideWindow(window);
                        }
                    }
                }
                else if (screen == Screen::Settings)
                {
                    if (settingsSelection == 0)
                        screen = Screen::DisplaySettings;
                    else if (settingsSelection == 1)
                        screen = Screen::AudioSettings;
                    else
                        screen = Screen::SystemSettings;
                }
                else if (screen == Screen::DisplaySettings)
                {
                    if (displaySelection == 0)
                    {
                        settings.fullscreen = !settings.fullscreen;
                        saveMalikSettings(settings, settingsPath);
                        SDL_SetWindowFullscreen(
                            window,
                            settings.fullscreen
                                ? SDL_WINDOW_FULLSCREEN_DESKTOP
                                : 0);
                    }
                    else if (displaySelection == 1)
                    {
                        settings.screenWidth =
                            (settings.screenWidth == 1280)
                                ? 1920 : 1280;
                        saveMalikSettings(settings, settingsPath);
                    }
                    else
                    {
                        settings.screenHeight =
                            (settings.screenHeight == 720)
                                ? 1080 : 720;
                        saveMalikSettings(settings, settingsPath);
                    }
                }
                else if (screen == Screen::AudioSettings)
                {
                    if (audioSelection == 0)
                    {
                        settings.volume += 10;
                        if (settings.volume > 100)
                            settings.volume = 0;
                    }
                    else
                    {
                        settings.mute = !settings.mute;
                    }
                    saveMalikSettings(settings, settingsPath);
                }
            }

            // Controller B / Back
            if (controller.backPressed())
            {
                if (screen == Screen::Games ||
                    screen == Screen::Settings)
                {
                    screen = Screen::MainMenu;
                }
                else if (screen == Screen::DisplaySettings ||
                         screen == Screen::AudioSettings   ||
                         screen == Screen::SystemSettings)
                {
                    screen = Screen::Settings;
                }
            }
        }

        // ========================================================
        // Render
        // ========================================================

        SDL_SetRenderDrawColor(renderer, 10, 12, 18, 255);
        SDL_RenderClear(renderer);

        SDL_Color white{ 255, 255, 255, 255 };
        SDL_Color gray { 160, 165, 175, 255 };

        // --------------------------------------------------------
        // MAIN MENU
        // --------------------------------------------------------

        if (screen == Screen::MainMenu)
        {
            drawText(renderer, titleFont,
                     "MALIK GAME OS", 70, 45, white);

            drawText(renderer, font,
                     "MAIN MENU", 90, 140, white);

            const char* menu[] = { "GAMES", "SETTINGS", "EXIT" };

            for (int i = 0; i < 3; ++i)
            {
                int y = 220 + i * 70;

                if (i == mainSelection)
                {
                    SDL_SetRenderDrawColor(renderer, 45, 55, 75, 255);
                    SDL_Rect box{ 70, y - 8, 500, 55 };
                    SDL_RenderFillRect(renderer, &box);
                }

                std::string text =
                    (i == mainSelection ? "▶  " : "    ") +
                    std::string(menu[i]);

                drawText(renderer, font, text, 90, y,
                         i == mainSelection ? white : gray);
            }

            drawText(renderer, font,
                     "A = SELECT       B = BACK",
                     90, 620, gray);
        }

        // --------------------------------------------------------
        // GAMES
        // --------------------------------------------------------

        else if (screen == Screen::Games)
        {
            drawText(renderer, titleFont,
                     "GAME LIST", 70, 45, white);

            const auto& games = gameList.getGames();

            for (size_t i = 0; i < games.size(); ++i)
            {
                int y = 140 + static_cast<int>(i) * 70;

                const Game& game = games[i];
                bool selected = (gameList.getSelectedGame() == &game);

                if (selected)
                {
                    SDL_SetRenderDrawColor(renderer, 45, 55, 75, 255);
                    SDL_Rect box{ 70, y - 8, 700, 55 };
                    SDL_RenderFillRect(renderer, &box);
                }

                std::string text =
                    (selected ? "▶  " : "    ") +
                    game.name + " [" + game.system + "]";

                drawText(renderer, font, text, 90, y,
                         selected ? white : gray);
            }

            drawText(renderer, font,
                     "A = START       B = BACK",
                     90, 620, gray);
        }

        // --------------------------------------------------------
        // SETTINGS
        // --------------------------------------------------------

        else if (screen == Screen::Settings)
        {
            drawText(renderer, titleFont,
                     "SETTINGS", 70, 45, white);

            const char* items[] =
            {
                "DISPLAY",
                "AUDIO",
                "SYSTEM"
            };

            for (int i = 0; i < 3; ++i)
            {
                int y = 150 + i * 70;

                if (i == settingsSelection)
                {
                    SDL_SetRenderDrawColor(renderer, 45, 55, 75, 255);
                    SDL_Rect box{ 70, y - 8, 600, 55 };
                    SDL_RenderFillRect(renderer, &box);
                }

                std::string text =
                    (i == settingsSelection ? "▶  " : "    ") +
                    std::string(items[i]);

                drawText(renderer, font, text, 90, y,
                         i == settingsSelection ? white : gray);
            }

            drawText(renderer, font,
                     "A = SELECT       B = BACK",
                     90, 620, gray);
        }

        // --------------------------------------------------------
        // DISPLAY SETTINGS
        // --------------------------------------------------------

        else if (screen == Screen::DisplaySettings)
        {
            drawText(renderer, titleFont,
                     "DISPLAY", 70, 45, white);

            std::string items[] =
            {
                "Fullscreen: " +
                    std::string(settings.fullscreen ? "ON" : "OFF"),

                "Width: " +
                    std::to_string(settings.screenWidth),

                "Height: " +
                    std::to_string(settings.screenHeight)
            };

            for (int i = 0; i < 3; ++i)
            {
                int y = 150 + i * 70;

                if (i == displaySelection)
                {
                    SDL_SetRenderDrawColor(renderer, 45, 55, 75, 255);
                    SDL_Rect box{ 70, y - 8, 650, 55 };
                    SDL_RenderFillRect(renderer, &box);
                }

                drawText(renderer, font,
                         (i == displaySelection ? "▶  " : "    ") +
                             items[i],
                         90, y,
                         i == displaySelection ? white : gray);
            }

            drawText(renderer, font,
                     "A = CHANGE       B = BACK",
                     90, 620, gray);
        }

        // --------------------------------------------------------
        // AUDIO SETTINGS
        // --------------------------------------------------------

        else if (screen == Screen::AudioSettings)
        {
            drawText(renderer, titleFont,
                     "AUDIO", 70, 45, white);

            std::string items[] =
            {
                "Volume: " + std::to_string(settings.volume),
                "Mute: "   + std::string(settings.mute ? "ON" : "OFF")
            };

            for (int i = 0; i < 2; ++i)
            {
                int y = 150 + i * 70;

                if (i == audioSelection)
                {
                    SDL_SetRenderDrawColor(renderer, 45, 55, 75, 255);
                    SDL_Rect box{ 70, y - 8, 650, 55 };
                    SDL_RenderFillRect(renderer, &box);
                }

                drawText(renderer, font,
                         (i == audioSelection ? "▶  " : "    ") +
                             items[i],
                         90, y,
                         i == audioSelection ? white : gray);
            }

            drawText(renderer, font,
                     "A = CHANGE       B = BACK",
                     90, 620, gray);
        }

        // --------------------------------------------------------
        // SYSTEM
        // --------------------------------------------------------

        else if (screen == Screen::SystemSettings)
        {
            drawText(renderer, titleFont,
                     "SYSTEM & HARDWARE", 70, 45, white);

            drawText(renderer, font,
                     "Malik Game OS (Arcade Edition)", 90, 130, white);

            std::string cpuShort = hwProfile.cpuModel;
            if (cpuShort.size() > 36)
                cpuShort = cpuShort.substr(0, 36) + "...";

            drawText(renderer, font,
                     "CPU: " + cpuShort, 90, 185, gray);

            char specBuf[80];
            snprintf(specBuf, sizeof(specBuf), "Threads: %d   |   RAM: %.1f GB",
                     hwProfile.cpuThreads, hwProfile.totalRamGB);
            drawText(renderer, font, specBuf, 90, 235, gray);

            drawText(renderer, font,
                     "Profile: " + hwProfile.tierName, 90, 290, white);

            drawText(renderer, font,
                     "Target: 60 FPS (Hardware Auto-Tuned)", 90, 340, white);

            drawText(renderer, font,
                     "Display: Always Fullscreen (Forced)", 90, 395, gray);

            drawText(renderer, font,
                     "PS1: DuckStation (" + std::to_string(hwProfile.ps1ResolutionScale) + "x Native)",
                     90, 445, gray);

            drawText(renderer, font,
                     "PS2: PCSX2 (" + std::to_string(hwProfile.ps2UpscaleMultiplier) + "x Native + MTVU)",
                     90, 495, gray);

            drawText(renderer, font,
                     "B = BACK", 90, 620, gray);
        }

        // --------------------------------------------------------
        // Present
        // --------------------------------------------------------

        SDL_RenderPresent(renderer);

        // Slow down the loop while game is running
        // (launcher is minimized, no need for 120fps)
        SDL_Delay(gameRunning ? 100 : 8);
    }

    // ============================================================
    // Cleanup
    // ============================================================

    // If a game is still running, terminate it gracefully
    if (gameRunning && gamePid > 0)
    {
        kill(gamePid, SIGTERM);
        waitpid(gamePid, nullptr, 0);
    }

    TTF_CloseFont(font);
    TTF_CloseFont(titleFont);

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);

    TTF_Quit();
    SDL_Quit();

    return 0;
}