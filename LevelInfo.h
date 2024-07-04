#pragma once

#include "Usings.h"

// gets information about the state of the order book
struct LevelInfo
{
    Price price_;
    Quantity quantity_;
};

using LevelInfos = std::vector<LevelInfo>;