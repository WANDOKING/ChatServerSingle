#include <conio.h>
#include <Windows.h>
#include <process.h>
#include <Psapi.h>
#include <Pdh.h>

#include "NetLibrary/Tool/ConfigReader.h"
#include "NetLibrary/Logger/Logger.h"
#include "NetLibrary/Profiler/Profiler.h"
#include "MonitorClient.h"
#include "ChatServer.h"

#pragma comment(lib,"Pdh.lib")

ChatServer myChatServer;

int main(void)
{
#ifdef PROFILE_ON
    LOGF(ELogLevel::System, L"Profiler: PROFILE_ON");
#endif

#pragma region config 파일 읽기
    const WCHAR* CONFIG_FILE_NAME = L"ChatServer.config";

    /*************************************** Config - Logger ***************************************/

    WCHAR inputLogLevel[10];

    ASSERT_LIVE(ConfigReader::GetString(CONFIG_FILE_NAME, L"LOG_LEVEL", inputLogLevel, sizeof(inputLogLevel)), L"ERROR: config file read failed (LOG_LEVEL)");

    if (wcscmp(inputLogLevel, L"DEBUG") == 0)
    {
        Logger::SetLogLevel(ELogLevel::System);
    }
    else if (wcscmp(inputLogLevel, L"ERROR") == 0)
    {
        Logger::SetLogLevel(ELogLevel::Error);
    }
    else if (wcscmp(inputLogLevel, L"SYSTEM") == 0)
    {
        Logger::SetLogLevel(ELogLevel::System);
    }
    else
    {
        ASSERT_LIVE(false, L"ERROR: invalid LOG_LEVEL");
    }

    LOGF(ELogLevel::System, L"Logger Log Level = %s", inputLogLevel);

    /*************************************** Config - NetServer ***************************************/

    uint32_t inputPortNumber;
    uint32_t inputMaxSessionCount;
    uint32_t inputConcurrentThreadCount;
    uint32_t inputWorkerThreadCount;
    uint32_t inputSetTcpNodelay;
    uint32_t inputSetSendBufZero;

    ASSERT_LIVE(ConfigReader::GetInt(CONFIG_FILE_NAME, L"PORT", &inputPortNumber), L"ERROR: config file read failed (PORT)");
    ASSERT_LIVE(ConfigReader::GetInt(CONFIG_FILE_NAME, L"MAX_SESSION_COUNT", &inputMaxSessionCount), L"ERROR: config file read failed (MAX_SESSION_COUNT)");
    ASSERT_LIVE(ConfigReader::GetInt(CONFIG_FILE_NAME, L"CONCURRENT_THREAD_COUNT", &inputConcurrentThreadCount), L"ERROR: config file read failed (CONCURRENT_THREAD_COUNT)");
    ASSERT_LIVE(ConfigReader::GetInt(CONFIG_FILE_NAME, L"WORKER_THREAD_COUNT", &inputWorkerThreadCount), L"ERROR: config file read failed (WORKER_THREAD_COUNT)");
    ASSERT_LIVE(ConfigReader::GetInt(CONFIG_FILE_NAME, L"TCP_NODELAY", &inputSetTcpNodelay), L"ERROR: config file read failed (TCP_NODELAY)");
    ASSERT_LIVE(ConfigReader::GetInt(CONFIG_FILE_NAME, L"SND_BUF_ZERO", &inputSetSendBufZero), L"ERROR: config file read failed (SND_BUF_ZERO)");

    LOGF(ELogLevel::System, L"CONCURRENT_THREAD_COUNT = %u", inputConcurrentThreadCount);
    LOGF(ELogLevel::System, L"WORKER_THREAD_COUNT = %u", inputWorkerThreadCount);

    if (inputSetTcpNodelay != 0)
    {
        myChatServer.SetTcpNodelay(true);
        LOGF(ELogLevel::System, L"ChatServer - SetTcpNodelay(true)");
    }

    if (inputSetSendBufZero != 0)
    {
        myChatServer.SetSendBufferSizeToZero(true);
        LOGF(ELogLevel::System, L"ChatServer - SetSendBufferSizeToZero(true)");
    }

    /*************************************** Config - ChatServer ***************************************/

    uint32_t inputTimeoutCheckInterval;
    uint32_t inputTimeoutLoggedIn;
    uint32_t inputTimeoutNotLoggedIn;
    uint32_t inputUseRedis; // 로그인 서버 연동 레디스 사용하는지 여부 (테스트용)

    ASSERT_LIVE(ConfigReader::GetInt(CONFIG_FILE_NAME, L"TIMEOUT_CHECK_INTERVAL", &inputTimeoutCheckInterval), L"ERROR: config file read failed (TIMEOUT_CHECK_INTERVAL)");
    ASSERT_LIVE(ConfigReader::GetInt(CONFIG_FILE_NAME, L"TIMEOUT_LOGGED_IN", &inputTimeoutLoggedIn), L"ERROR: config file read failed (TIMEOUT_LOGGED_IN)");
    ASSERT_LIVE(ConfigReader::GetInt(CONFIG_FILE_NAME, L"TIMEOUT_NOT_LOGGED_IN", &inputTimeoutNotLoggedIn), L"ERROR: config file read failed (TIMEOUT_NOT_LOGGED_IN)");
    ASSERT_LIVE(ConfigReader::GetInt(CONFIG_FILE_NAME, L"USE_REDIS", &inputUseRedis), L"ERROR: config file read failed (USE_REDIS)");

    myChatServer.SetTimeoutCheckInterval(inputTimeoutCheckInterval);
    myChatServer.SetTimeoutLoggedIn(inputTimeoutLoggedIn);
    myChatServer.SetTimeoutNotLoggedIn(inputTimeoutNotLoggedIn);

    LOGF(ELogLevel::System, L"TIMEOUT_CHECK_INTERVAL = %u", inputTimeoutCheckInterval);
    LOGF(ELogLevel::System, L"TIMEOUT_LOGGED_IN = %u", inputTimeoutLoggedIn);
    LOGF(ELogLevel::System, L"TIMEOUT_NOT_LOGGED_IN = %u", inputTimeoutNotLoggedIn);

    if (inputUseRedis != 0)
    {
        myChatServer.UseRedis();
        LOGF(ELogLevel::System, L"USE REDIS");
    }

    // MonitoringServer config input

    WCHAR inputMonitoringServerIP[16];
    uint32_t inputMonitoringServerPort;

    ASSERT_LIVE(ConfigReader::GetString(CONFIG_FILE_NAME, L"MONITORING_SERVER_IP", inputMonitoringServerIP, 16), L"ERROR: config file read failed (MONITORING_SERVER_IP)");
    ASSERT_LIVE(ConfigReader::GetInt(CONFIG_FILE_NAME, L"MONITORING_SERVER_PORT", &inputMonitoringServerPort), L"ERROR: config file read failed (MONITORING_SERVER_PORT)");

    MonitorClient monitorClient(std::wstring(inputMonitoringServerIP, 16), inputMonitoringServerPort);

    if (false == monitorClient.Connect())
    {
        LOGF(ELogLevel::Error, L"ERROR: MonitoringServer Connect Failed");
    }
    else
    {
        monitorClient.SendLoginPacket();
    }

#pragma endregion

#pragma region 모니터링 서버 정보 얻기 PDH 사전 작업

    WCHAR processName[MAX_PATH];
    WCHAR query[MAX_PATH];
    PDH_HQUERY queryHandle;
    PDH_HCOUNTER privateBytes;

    // 프로세스 이름 얻기
    ASSERT_LIVE(::GetProcessImageFileName(::GetCurrentProcess(), processName, MAX_PATH) != 0, L"GetProcessImageFileName() Failed");
    _wsplitpath_s(processName, NULL, 0, NULL, 0, processName, MAX_PATH, NULL, 0);

    // PDH 쿼리 핸들 생성
    PdhOpenQuery(NULL, NULL, &queryHandle);

    // PDH 리소스 카운터 생성
    wsprintf(query, L"\\Process(%s)\\Private Bytes", processName);
    ASSERT_LIVE(PdhAddCounter(queryHandle, query, NULL, &privateBytes) == ERROR_SUCCESS, L"PdhAddCounter Error");

    // 첫 갱신
    PdhCollectQueryData(queryHandle);

#pragma endregion

    // 최대 페이로드 길이 지정
    myChatServer.SetMaxPayloadLength(1'000);

    // 서버 시작
    myChatServer.Start(static_cast<uint16_t>(inputPortNumber), inputMaxSessionCount, inputConcurrentThreadCount, inputWorkerThreadCount);

    MonitoringVariables monitoringInfo;                             // NetServer의 모니터링 정보
    std::vector<ChatServer::SectorMonitorInfo> sectorMonitorInfos;  // 채팅 서버 섹터 모니터링 정보

    HANDLE mainThreadTimer = ::CreateWaitableTimer(NULL, FALSE, NULL);
    ASSERT_LIVE(mainThreadTimer != INVALID_HANDLE_VALUE, L"mainThreadTimer create failed");

    LARGE_INTEGER timerTime{};
    timerTime.QuadPart = -1 * (10'000 * static_cast<LONGLONG>(1'000));
    ::SetWaitableTimer(mainThreadTimer, &timerTime, 1'000, nullptr, nullptr, FALSE);

    for (;;)
    {
        ::WaitForSingleObject(mainThreadTimer, INFINITE);

        if (_kbhit())
        {
            int input = _getch();
            if (input == 'Q' || input == 'q')
            {
                myChatServer.Shutdown();
                break;
            }
#ifdef PROFILE_ON
            else if (input == 'S' || input == 's')
            {
                PROFILE_SAVE(L"Profile_ChatServer.txt");
                LOGF(ELogLevel::System, L"Profile_ChatServer.txt saved");
            }
#endif
        }

        // NetServer Monitoring
        monitoringInfo = myChatServer.GetMonitoringInfo();

        uint32_t processedMessageCountPerSecond = myChatServer.GetProcessedMessageCountPerSecond();
        uint32_t maxWorkQueueSizePerSecond = myChatServer.GetMaxWorkQueueSizePerSecond();
        uint32_t minWorkQueueSizePerSecond = myChatServer.GetMinWorkQueueSizePerSecond();
        if (minWorkQueueSizePerSecond == UINT32_MAX)
        {
            minWorkQueueSizePerSecond = 0;
        }

        // Pdh - private bytes
        PdhCollectQueryData(queryHandle);
        PDH_FMT_COUNTERVALUE privateBytesValue;
        PdhGetFormattedCounterValue(privateBytes, PDH_FMT_LARGE, NULL, &privateBytesValue);

        // 모니터링 서버에 모니터링 정보 전송
        if (monitorClient.IsConnected())
        {
            int32_t timeStamp = static_cast<int32_t>(time(nullptr));
            monitorClient.Send_MONITOR_DATA_UPDATE(en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_CHAT_SERVER_RUN, 1, timeStamp);
            monitorClient.Send_MONITOR_DATA_UPDATE(en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_CHAT_SERVER_CPU, static_cast<int32_t>(monitoringInfo.ProcessTimeTotal), timeStamp);
            monitorClient.Send_MONITOR_DATA_UPDATE(en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_CHAT_SERVER_MEM, static_cast<int32_t>(privateBytesValue.largeValue / 1'000'000), timeStamp);
            monitorClient.Send_MONITOR_DATA_UPDATE(en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_CHAT_SESSION, myChatServer.GetSessionCount(), timeStamp);
            monitorClient.Send_MONITOR_DATA_UPDATE(en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_CHAT_PLAYER, static_cast<int32_t>(myChatServer.GetRealPlayerCount()), timeStamp);
            monitorClient.Send_MONITOR_DATA_UPDATE(en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_CHAT_UPDATE_TPS, static_cast<int32_t>(processedMessageCountPerSecond), timeStamp);
            monitorClient.Send_MONITOR_DATA_UPDATE(en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_CHAT_PACKET_POOL, Serializer::GetTotalPacketCount(), timeStamp);
            monitorClient.Send_MONITOR_DATA_UPDATE(en_PACKET_SS_MONITOR_DATA_UPDATE::dfMONITOR_DATA_TYPE_CHAT_UPDATEMSG_POOL, static_cast<int32_t>(minWorkQueueSizePerSecond), timeStamp);
        }

        // ChatServer Monitoring
        myChatServer.GetSectorNonitorInfos(&sectorMonitorInfos);

        std::sort(sectorMonitorInfos.begin(), sectorMonitorInfos.end(), [](const ChatServer::SectorMonitorInfo& a, const ChatServer::SectorMonitorInfo& b) { return a.Count > b.Count; });

        uint32_t sectorMinPlayerCount = UINT32_MAX;
        uint32_t sectorMaxPlayerCount = 0;

        for (const ChatServer::SectorMonitorInfo& info : sectorMonitorInfos)
        {
            if (info.Count > sectorMaxPlayerCount)
            {
                sectorMaxPlayerCount = info.Count;
            }

            if (info.Count < sectorMinPlayerCount)
            {
                sectorMinPlayerCount = info.Count;
            }
        }

        wprintf(L"\n\n");
        //LOG_CURRENT_TIME();
        wprintf(L"[ ChatServer Running (S: profile save) (Q: quit)]\n");
        wprintf(L"=================================================\n");
        wprintf(L"Session Count        = %u / %u\n", myChatServer.GetSessionCount(), myChatServer.GetMaxSessionCount());
        wprintf(L"Accept Total         = %llu\n", myChatServer.GetTotalAcceptCount());
        wprintf(L"Disconnected Total   = %llu\n", myChatServer.GetTotalDisconnectCount());
        wprintf(L"Packet Pool Size     = %u\n", Serializer::GetTotalPacketCount());
        wprintf(L"---------------------- TPS ----------------------\n");
        wprintf(L"Accept TPS           = %9u (Avg: %9u)\n", monitoringInfo.AcceptTPS, monitoringInfo.AverageAcceptTPS);
        wprintf(L"Send Message TPS     = %9u (Avg: %9u)\n", monitoringInfo.SendMessageTPS, monitoringInfo.AverageSendMessageTPS);
        wprintf(L"Recv Message TPS     = %9u (Avg: %9u)\n", monitoringInfo.RecvMessageTPS, monitoringInfo.AverageRecvMessageTPS);
        wprintf(L"Send Pending TPS     = %9u (Avg: %9u)\n", monitoringInfo.SendPendingTPS, monitoringInfo.AverageSendPendingTPS);
        wprintf(L"Recv Pending TPS     = %9u (Avg: %9u)\n", monitoringInfo.RecvPendingTPS, monitoringInfo.AverageRecvPendingTPS);
        wprintf(L"----------------------- CPU ---------------------\n");
        wprintf(L"Total  = Processor: %6.3f / Process: %6.3f\n", monitoringInfo.ProcessorTimeTotal, monitoringInfo.ProcessTimeTotal);
        wprintf(L"User   = Processor: %6.3f / Process: %6.3f\n", monitoringInfo.ProcessorTimeUser, monitoringInfo.ProcessTimeUser);
        wprintf(L"Kernel = Processor: %6.3f / Process: %6.3f\n", monitoringInfo.ProcessorTimeKernel, monitoringInfo.ProcessTimeKernel);
        wprintf(L"=================================================\n");
        wprintf(L"WorkQueue Size Max = %5u (Total Max: %5u)\n", maxWorkQueueSizePerSecond, myChatServer.GetTotalMaxWorkQueueSize());
        wprintf(L"WorkQueue Size Min = %5u\n", minWorkQueueSizePerSecond);
        wprintf(L"Processed Message  = %5u\n\n", processedMessageCountPerSecond);

        wprintf(L"[Player & Sector]\n");
        wprintf(L"Player Count     = %5u / %5u\n", myChatServer.GetRealPlayerCount(), myChatServer.GetPlayerPoolSize());
        wprintf(L"Sector MAX Count = %5u\n", sectorMaxPlayerCount);
        wprintf(L"Sector MIN Count = %5u\n", sectorMinPlayerCount);
        for (int i = 0; i < 5; ++i)
        {
            wprintf(L"TOP %d (%2u, %2u) : %2u  |  MIN %d (%2u, %2u) : %2u\n", i + 1, sectorMonitorInfos[i].SectorX, sectorMonitorInfos[i].SectorY, sectorMonitorInfos[i].Count, i + 1, sectorMonitorInfos[sectorMonitorInfos.size() - i - 1].SectorX, sectorMonitorInfos[sectorMonitorInfos.size() - i - 1].SectorY, sectorMonitorInfos[sectorMonitorInfos.size() - i - 1].Count);
        }
    }

    ::CloseHandle(mainThreadTimer);

    return 0;
}