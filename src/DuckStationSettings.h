#pragma once

#include "Settings.h"

#include <string>

class DuckStationSettings
{
public:
    static bool write(const Settings& settings,
                      const std::string& path);
};

