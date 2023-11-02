#pragma once

#include <stdint.h>

enum class EWorkType
{
    Accept,
    Release,
    Receive,
};

struct Work
{
    uint64_t SessionID;
    EWorkType WorkType;
    Serializer* Packet; // Accept, Release - nullptr
};