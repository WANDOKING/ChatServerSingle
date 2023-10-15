#pragma once

#include <cstdint>

typedef wchar_t WCHAR;

struct Player
{
public:
#pragma warning(push)
#pragma warning(disable: 26495)
    Player() = default;
#pragma warning(pop)

    void Init(const uint64_t sessionID)
    {
        mSessionID = sessionID;
        mbLoggedIn = false;
        mbSectorIn = false;
        mLastRecvTick = ::timeGetTime();
    }

    inline bool         IsLoggedIn(void) const { return mbLoggedIn; }
    inline bool         IsSectorIn(void) const { return mbSectorIn; }

    inline uint16_t     GetSectorX(void) const { return mSectorX; }
    inline uint16_t     GetSectorY(void) const { return mSectorY; }
    inline int64_t      GetAccountNo(void) const { return mAccountNo; }
    inline const WCHAR* GetID(void) const { return mID; }
    inline const WCHAR* GetNickName(void) const { return mNickName; }
    inline const char*  GetSessionKey(void) const { return mSessionKey; }

    inline uint64_t     GetSessionID(void) const { return mSessionID; }
    inline uint32_t     GetLastRecvTick(void) const { return mLastRecvTick; }

    inline void         UpdateLastRecvTick(void) { mLastRecvTick = ::timeGetTime(); }

    void LogIn(const int64_t accountNo, const WCHAR id[], const WCHAR nickName[], const char sessionKey[])
    {
        mbLoggedIn = true;
        mAccountNo = accountNo;
        memcpy(&mID, id, sizeof(WCHAR) * 20);
        memcpy(&mNickName, nickName, sizeof(WCHAR) * 20);
        memcpy(&mSessionKey, sessionKey, sizeof(char) * 64);
    }

    void MoveSector(const uint16_t sectorX, const uint16_t sectorY)
    {
        mbSectorIn = true;
        mSectorX = sectorX;
        mSectorY = sectorY;
    }

private:
    uint64_t    mSessionID;
    bool        mbLoggedIn;
    bool        mbSectorIn;
    uint32_t    mLastRecvTick;
    uint16_t    mSectorX;
    uint16_t    mSectorY;
    int64_t     mAccountNo;
    WCHAR       mID[20];
    WCHAR       mNickName[20];
    char        mSessionKey[64];
};