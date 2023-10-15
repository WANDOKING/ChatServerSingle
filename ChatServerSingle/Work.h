#pragma once

#include <stdint.h>

enum EWorkType
{
    Accept,
    Release,
    Receive,
};

struct Work
{
    uint64_t SessionID;
    EWorkType WorkType;
    Serializer* Packet; // 0ÀÌ¸י Release
};