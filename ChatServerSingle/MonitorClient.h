#pragma once

#include "NetLibrary/NetServer/NetClient.h"
#include "NetLibrary/NetServer/Serializer.h"

#include "Protocol.h"

class MonitorClient : public NetClient
{
public:
    MonitorClient(const std::wstring serverIP, const uint16_t serverPortNumber)
        : NetClient(serverIP, serverPortNumber)
    {}

    // NetClient을(를) 통해 상속됨
    virtual void OnReceive(Serializer* packet) override
    {
    }

    virtual void OnDisconnect(int errorCode) override
    {
    }

    void SendLoginPacket(void)
    {
        Serializer* packet = Serializer::Alloc();

        *packet << (WORD)en_PACKET_SS_MONITOR_LOGIN << LOGIN_SERVER_NO;

        SendPacket(packet);

        packet->DecrementRefCount();
    }

    void Send_LOGIN_SERVER_RUN(void)
    {
        Serializer* packet = Serializer::Alloc();

        int dataValue = 1;

        *packet << (WORD)en_PACKET_CS_MONITOR_TOOL_DATA_UPDATE
            << (BYTE)LOGIN_SERVER_NO
            << (BYTE)dfMONITOR_DATA_TYPE_LOGIN_SERVER_RUN
            << dataValue
            << (int)time(nullptr);

        SendPacket(packet);

        packet->DecrementRefCount();
    }

    void Send_MONITOR_DATA_UPDATE(const BYTE dataType, const int32_t dataValue, const int32_t timeStamp)
    {
        Serializer* packet = Serializer::Alloc();

        *packet << (WORD)en_PACKET_SS_MONITOR_DATA_UPDATE << dataType << dataValue << timeStamp;

        SendPacket(packet);

        packet->DecrementRefCount();
    }

private:
    enum
    {
        LOGIN_SERVER_NO = 1
    };
};