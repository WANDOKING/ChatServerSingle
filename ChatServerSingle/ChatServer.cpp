#include <process.h>

#include <cpp_redis/cpp_redis>

#include "ChatServer.h"
#include "Protocol.h"
#include "NetLibrary/Logger/Logger.h"
#include "NetLibrary/Profiler/Profiler.h"

#pragma comment (lib, "cpp_redis.lib")
#pragma comment (lib, "tacopie.lib")

void ChatServer::OnAccept(const uint64_t sessionID)
{
    Work newWork;
    newWork.SessionID = sessionID;
    newWork.WorkType = EWorkType::Accept;
    newWork.Packet = 0;

    mWorkQueue.Enqueue(newWork);
    ::SetEvent(mWorkQueueEvent);
}

void ChatServer::OnRelease(const uint64_t sessionID)
{
    Work newWork;
    newWork.SessionID = sessionID;
    newWork.WorkType = EWorkType::Release;
    newWork.Packet = 0;

    mWorkQueue.Enqueue(newWork);
    ::SetEvent(mWorkQueueEvent);
}

void ChatServer::OnReceive(const uint64_t sessionID, Serializer* packet)
{
    Work newWork;
    newWork.SessionID = sessionID;
    newWork.WorkType = EWorkType::Receive;
    newWork.Packet = packet;

    mWorkQueue.Enqueue(newWork);
    ::SetEvent(mWorkQueueEvent);
}

void ChatServer::Start(const uint16_t port, const uint32_t maxSessionCount, const uint32_t iocpConcurrentThreadCount, const uint32_t iocpWorkerThreadCount)
{
    NetServer::Start(port, maxSessionCount, iocpConcurrentThreadCount, iocpWorkerThreadCount);

    mWorkQueueEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
    ASSERT_LIVE(mWorkQueueEvent != INVALID_HANDLE_VALUE, L"mWorkQueueEvent create failed");

    mTimeOutCheckEvent = ::CreateWaitableTimer(NULL, FALSE, NULL);
    ASSERT_LIVE(mTimeOutCheckEvent != INVALID_HANDLE_VALUE, L"mTimeOutCheckEvent create failed");

    LARGE_INTEGER timerTime{};
    timerTime.QuadPart = -1 * (10'000 * static_cast<LONGLONG>(mTimeoutCheckInterval));
    ::SetWaitableTimer(mTimeOutCheckEvent, &timerTime, mTimeoutCheckInterval, nullptr, nullptr, FALSE);

    mbUpdateThreadRunning = true;

    mUpdateThread = (HANDLE)::_beginthreadex(nullptr, 0, updateThread, this, 0, nullptr);
}

void ChatServer::Shutdown(void)
{
    mbUpdateThreadRunning = false;

    ::WaitForSingleObject(mUpdateThread, INFINITE);

    for (auto it = mPlayerMap.begin(); it != mPlayerMap.end(); ++it)
    {
        mPlayerPool.Free(it->second);
    }

    ::CloseHandle(mWorkQueueEvent);
    ::CloseHandle(mTimeOutCheckEvent);

    NetServer::Shutdown();
}

void ChatServer::timeoutCheck(void)
{
    uint32_t currentTick = ::timeGetTime();

    for (auto it = mPlayerMap.begin(); it != mPlayerMap.end(); ++it)
    {
        Player* player = it->second;
        uint32_t maxTimeout;

        if (player->IsLoggedIn())
        {
            maxTimeout = mTimeoutLoggedIn;
        }
        else
        {
            maxTimeout = mTimeoutNotLoggedIn;
        }

        if (maxTimeout == 0)
        {
            continue;
        }

        if (currentTick - player->GetLastRecvTick() > maxTimeout)
        {
            LOGF(ELogLevel::System, L"SessionID %llu timeouted (maxTimeout = %u) (currentTick = %u, player = %u)", player->GetSessionID(), maxTimeout, currentTick, player->GetLastRecvTick());
            Disconnect(player->GetSessionID());
        }
    }
}

void ChatServer::process_SessionAccept(const uint64_t sessionID)
{
    Player* player = mPlayerPool.Alloc();

    player->Init(sessionID);

    mPlayerMap.insert(std::make_pair(sessionID, player));
}

void ChatServer::process_SessionReleased(const uint64_t sessionID)
{
    Player* player = findPlayerOrNull(sessionID);
    ASSERT_LIVE(player != nullptr, L"SessionReleased player is nullptr");

    if (player->IsSectorIn())
    {
        mSector[player->GetSectorY()][player->GetSectorX()].remove(player->GetSessionID());
    }

    if (player->IsLoggedIn())
    {
        mRealPlayerCount--;
    }

    mPlayerMap.erase(player->GetSessionID());

    mPlayerPool.Free(player);
}

void ChatServer::process_CS_CHAT_REQ_LOGIN(const uint64_t sessionID, const int64_t accountNo, const WCHAR id[], const WCHAR nickName[], const char sessionKey[])
{
    Player* player = findPlayerOrNull(sessionID);
    if (player == nullptr)
    {
        return;
    }

    if (player->IsLoggedIn())
    {
        LOGF(ELogLevel::System, L"Disconnect(%llu): CS_CHAT_REQ_LOGIN already logged in", sessionID);
        Disconnect(sessionID);
        return;
    }

    if (true == mbRedisUsed)
    {
        static cpp_redis::client redisClient;

        if (false == redisClient.is_connected())
        {
            redisClient.connect();
        }

        bool bIsValidSessionKey = false;

        redisClient.get(std::to_string(accountNo), [&](cpp_redis::reply& reply) {

            if (reply.is_null())
            {
                return;
            }

            if (strncmp(reply.as_string().c_str(), sessionKey, 64) == 0)
            {
                bIsValidSessionKey = true;
            }
            else
            {
                bIsValidSessionKey = false;
            }

            });

        redisClient.sync_commit();

        std::vector<std::string> toDeleteKeys;
        toDeleteKeys.push_back(std::to_string(accountNo));
        redisClient.del(toDeleteKeys);
        redisClient.sync_commit();
    }

    player->UpdateLastRecvTick();

    player->LogIn(accountNo, id, nickName, sessionKey);
    mRealPlayerCount++;

    Serializer* packet = createMessage_CS_CHAT_RES_LOGIN(1, accountNo);

    SendPacket(sessionID, packet);

    Serializer::Free(packet);
}

void ChatServer::process_CS_CHAT_REQ_SECTOR_MOVE(const uint64_t sessionID, const int64_t accountNo, const WORD sectorX, const WORD sectorY)
{
    if (sectorX >= 50 || sectorY >= 50)
    {
        LOGF(ELogLevel::System, L"Disconnect(%llu): CS_CHAT_REQ_SECTOR_MOVE invalid sector X/Y", sessionID);
        Disconnect(sessionID);
        return;
    }

    Player* player = findPlayerOrNull(sessionID);
    if (player == nullptr)
    {
        return;
    }

    if (false == player->IsLoggedIn())
    {
        LOGF(ELogLevel::System, L"Disconnect(%llu): CS_CHAT_REQ_SECTOR_MOVE not logged in player", sessionID);
        Disconnect(sessionID);
        return;
    }

    if (player->GetAccountNo() != accountNo)
    {
        LOGF(ELogLevel::System, L"Disconnect(%llu): CS_CHAT_REQ_SECTOR_MOVE different accountNo", sessionID);
        Disconnect(sessionID);
        return;
    }

    player->UpdateLastRecvTick();

    if (player->IsSectorIn())
    {
        mSector[player->GetSectorY()][player->GetSectorX()].remove(player->GetSessionID());
    }

    player->MoveSector(sectorX, sectorY);

    mSector[player->GetSectorY()][player->GetSectorX()].push_back(player->GetSessionID());

    Serializer* packet = createMessage_CS_CHAT_RES_SECTOR_MOVE(player->GetAccountNo(), player->GetSectorX(), player->GetSectorY());

    SendPacket(player->GetSessionID(), packet);

    Serializer::Free(packet);
}

void ChatServer::process_CS_CHAT_REQ_MESSAGE(const uint64_t sessionID, const int64_t accountNo, const WORD messageLen, const WCHAR message[])
{
    Player* player = findPlayerOrNull(sessionID);
    if (player == nullptr)
    {
        return;
    }

    if (false == player->IsLoggedIn())
    {
        LOGF(ELogLevel::System, L"Disconnect(%llu): CS_CHAT_REQ_MESSAGE not logged in player", sessionID);
        Disconnect(sessionID);
        return;
    }

    if (player->GetAccountNo() != accountNo)
    {
        LOGF(ELogLevel::System, L"Disconnect(%llu): CS_CHAT_REQ_MESSAGE different accountNo", sessionID);
        Disconnect(sessionID);
        return;
    }

    player->UpdateLastRecvTick();

    ASSERT_LIVE(player->IsSectorIn(), L"CS_CHAT_REQ_MESSAGE player is not in any sector");

    Serializer* packet = createMessage_CS_CHAT_RES_MESSAGE(player->GetAccountNo(), player->GetID(), player->GetNickName(), messageLen, message);

    if (player->GetSectorY() > 0)
    {
        if (player->GetSectorX() > 0)
        {
            for (uint64_t otherSession : mSector[player->GetSectorY() - 1][player->GetSectorX() - 1])
            {
                SendPacket(otherSession, packet);
            }
        }
        {
            for (uint64_t otherSession : mSector[player->GetSectorY() - 1][player->GetSectorX()])
            {
                SendPacket(otherSession, packet);
            }
        }
        if (player->GetSectorX() < SECTOR_WIDTH_AND_HEIGHT - 1)
        {
            for (uint64_t otherSession : mSector[player->GetSectorY() - 1][player->GetSectorX() + 1])
            {
                SendPacket(otherSession, packet);
            }
        }
    }
    {
        if (player->GetSectorX() > 0)
        {
            for (uint64_t otherSession : mSector[player->GetSectorY()][player->GetSectorX() - 1])
            {
                SendPacket(otherSession, packet);
            }
        }
        {
            for (uint64_t otherSession : mSector[player->GetSectorY()][player->GetSectorX()])
            {
                SendPacket(otherSession, packet);
            }
        }
        if (player->GetSectorX() < SECTOR_WIDTH_AND_HEIGHT - 1)
        {
            for (uint64_t otherSession : mSector[player->GetSectorY()][player->GetSectorX() + 1])
            {
                SendPacket(otherSession, packet);
            }
        }
    }
    if (player->GetSectorY() < SECTOR_WIDTH_AND_HEIGHT - 1)
    {
        if (player->GetSectorX() > 0)
        {
            for (uint64_t otherSession : mSector[player->GetSectorY() + 1][player->GetSectorX() - 1])
            {
                SendPacket(otherSession, packet);
            }
        }
        {
            for (uint64_t otherSession : mSector[player->GetSectorY() + 1][player->GetSectorX()])
            {
                SendPacket(otherSession, packet);
            }
        }
        if (player->GetSectorX() < SECTOR_WIDTH_AND_HEIGHT - 1)
        {
            for (uint64_t otherSession : mSector[player->GetSectorY() + 1][player->GetSectorX() + 1])
            {
                SendPacket(otherSession, packet);
            }
        }
    }

    Serializer::Free(packet);
}

void ChatServer::process_CS_CHAT_REQ_HEARTBEAT(const uint64_t sessionID)
{
    Player* player = findPlayerOrNull(sessionID);
    if (player == nullptr)
    {
        return;
    }

    player->UpdateLastRecvTick();
}

unsigned int ChatServer::updateThread(void* chatServer)
{
    LOGF(ELogLevel::System, L"ChatServer UpdateThread Start (ID : %d)", ::GetCurrentThreadId());

    ChatServer* server = reinterpret_cast<ChatServer*>(chatServer);

    DWORD lastTimeoutCheckTick = ::timeGetTime();

    HANDLE events[2]{};
    events[0] = server->mWorkQueueEvent;
    events[1] = server->mTimeOutCheckEvent;

    Work work;

    while (server->mbUpdateThreadRunning)
    {
        ::WaitForMultipleObjects(2, events, FALSE, INFINITE);

        // timeout check
        if (::timeGetTime() - lastTimeoutCheckTick >= server->mTimeoutCheckInterval)
        {
            server->timeoutCheck();
            lastTimeoutCheckTick = ::timeGetTime();
        }

        // work loop
        while (server->mWorkQueue.TryDequeue(work))
        {
            PROFILE_BEGIN(L"Update Loop");

            uint32_t workQueueSize = server->mWorkQueue.GetCount();

            if (workQueueSize < server->mMinWorkQueueSizePerSecond)
            {
                server->mMinWorkQueueSizePerSecond = workQueueSize;
            }

            if (workQueueSize > server->mMaxWorkQueueSizePerSecond)
            {
                server->mMaxWorkQueueSizePerSecond = workQueueSize;
            }

            if (workQueueSize > server->mTotalMaxWorkQueueSize)
            {
                server->mTotalMaxWorkQueueSize = workQueueSize;
            }

            switch (work.WorkType)
            {
            case EWorkType::Accept:
            {
                PROFILE_BEGIN(L"process_SessionAccept");
                server->process_SessionAccept(work.SessionID);
                PROFILE_END(L"process_SessionAccept");
            }
            break;
            case EWorkType::Release:
            {
                PROFILE_BEGIN(L"process_SessionRelease");
                server->process_SessionReleased(work.SessionID);
                PROFILE_END(L"process_SessionRelease");
            }
            break;
            case EWorkType::Receive:
            {
                Serializer* packet = work.Packet;

                WORD messageType;

                *packet >> messageType;

                switch (messageType)
                {
                case en_PACKET_TYPE::en_PACKET_CS_CHAT_REQ_LOGIN:
                {
                    int64_t accountNo;
                    WCHAR   id[20];
                    WCHAR   nickName[20];
                    char    sessionKey[64];

                    constexpr uint32_t PACKET_SIZE = sizeof(messageType) + sizeof(accountNo) + sizeof(id) + sizeof(nickName) + sizeof(sessionKey);
                    if (packet->GetUseSize() != PACKET_SIZE)
                    {
                        server->Disconnect(work.SessionID);
                        break;
                    }

                    *packet >> accountNo;
                    packet->GetByte((char*)id, sizeof(id));
                    packet->GetByte((char*)nickName, sizeof(nickName));
                    packet->GetByte((char*)sessionKey, sizeof(sessionKey));

                    PROFILE_BEGIN(L"process_CS_CHAT_REQ_LOGIN");
                    server->process_CS_CHAT_REQ_LOGIN(work.SessionID, accountNo, id, nickName, sessionKey);
                    PROFILE_END(L"process_CS_CHAT_REQ_LOGIN");
                }
                break;
                case en_PACKET_TYPE::en_PACKET_CS_CHAT_REQ_SECTOR_MOVE:
                {
                    int64_t accountNo;
                    WORD    sectorX;
                    WORD    sectorY;

                    constexpr uint32_t PACKET_SIZE = sizeof(messageType) + sizeof(accountNo) + sizeof(sectorX) + sizeof(sectorY);
                    if (packet->GetUseSize() != PACKET_SIZE)
                    {
                        server->Disconnect(work.SessionID);
                        break;
                    }

                    *packet >> accountNo >> sectorX >> sectorY;

                    PROFILE_BEGIN(L"process_CS_CHAT_REQ_SECTOR_MOVE");
                    server->process_CS_CHAT_REQ_SECTOR_MOVE(work.SessionID, accountNo, sectorX, sectorY);
                    PROFILE_END(L"process_CS_CHAT_REQ_SECTOR_MOVE");
                }
                break;
                case en_PACKET_TYPE::en_PACKET_CS_CHAT_REQ_MESSAGE:
                {
                    int64_t         accountNo;
                    WORD            messageLen;
                    static WCHAR    message[UINT16_MAX / 2];

                    constexpr uint32_t PACKET_MIN_SIZE = sizeof(messageType) + sizeof(accountNo) + sizeof(messageLen);
                    if (packet->GetUseSize() < PACKET_MIN_SIZE)
                    {
                        server->Disconnect(work.SessionID);
                        break;
                    }

                    *packet >> accountNo >> messageLen;

                    if (packet->GetUseSize() != PACKET_MIN_SIZE + messageLen)
                    {
                        server->Disconnect(work.SessionID);
                        break;
                    }

                    packet->GetByte((char*)message, messageLen);

                    PROFILE_BEGIN(L"process_CS_CHAT_REQ_MESSAGE");
                    server->process_CS_CHAT_REQ_MESSAGE(work.SessionID, accountNo, messageLen, message);
                    PROFILE_END(L"process_CS_CHAT_REQ_MESSAGE");
                }
                break;
                case en_PACKET_TYPE::en_PACKET_CS_CHAT_REQ_HEARTBEAT:
                {
                    constexpr uint32_t PACKET_SIZE = sizeof(messageType);
                    if (packet->GetUseSize() != PACKET_SIZE)
                    {
                        server->Disconnect(work.SessionID);
                        break;
                    }

                    PROFILE_BEGIN(L"process_CS_CHAT_REQ_HEARTBEAT");
                    server->process_CS_CHAT_REQ_HEARTBEAT(work.SessionID);
                    PROFILE_END(L"process_CS_CHAT_REQ_HEARTBEAT");
                }
                break;
                default:
                    server->Disconnect(work.SessionID);
                }

                Serializer::Free(packet);
            }
            break;
            default:
                ASSERT_LIVE(false, L"Invalid EWorkType dequeued");
            }

            InterlockedIncrement(&server->mProcessedMessageCountPerSecond);
            

            PROFILE_END(L"Update Loop");
        }
    }

    LOGF(ELogLevel::System, L"ChatServer UpdateThread End (ID : %d)", ::GetCurrentThreadId());

    return 0;
}

Player* ChatServer::findPlayerOrNull(const uint64_t sessionID)
{
    Player* player = nullptr;

    auto found = mPlayerMap.find(sessionID);
    if (found != mPlayerMap.end())
    {
        player = found->second;
    }

    return player;
}