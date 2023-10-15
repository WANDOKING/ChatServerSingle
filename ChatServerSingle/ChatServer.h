#pragma once

#include "NetLibrary/NetServer/NetServer.h"
#include "NetLibrary/DataStructure/LockFreeQueue.h"
#include "Work.h"
#include "Protocol.h"
#include "Player.h"
#include "NetLibrary/Memory/ObjectPool.h"

#include <vector>
#include <map>
#include <unordered_map>
#include <list>

class ChatServer : public NetServer
{
public:
#pragma warning(push)
#pragma warning(disable: 26495) // 변수 초기화 경고 제거
	ChatServer() = default;
#pragma warning(pop)

	virtual ~ChatServer() = default;

public: // 서버 시작 전 세팅 함수들

	// 타임 아웃 체크 간격
	inline void		SetTimeoutCheckInterval(const uint32_t interval) { mTimeoutCheckInterval = interval; }
	
	// 로그인한 유저의 최대 타임아웃 시간
	inline void		SetTimeoutLoggedIn(const uint32_t timeout) { mTimeoutLoggedIn = timeout; }

	// 로그인하지 않은 유저의 최대 타임아웃 시간
	inline void		SetTimeoutNotLoggedIn(const uint32_t timeout) { mTimeoutNotLoggedIn = timeout; }

	// 레디스 사용 여부
	inline void		UseRedis(void) { mbRedisUsed = true; }

public:

	// 서버 시작
	virtual void Start(
		const uint16_t port, 
		const uint32_t maxSessionCount, 
		const uint32_t iocpConcurrentThreadCount, 
		const uint32_t iocpWorkerThreadCount) override;

	// 서버 종료 (모든 스레드가 종료될 때 까지 블락됨)
	virtual void Shutdown(void) override;

	// NetServer을(를) 통해 상속됨
	virtual void OnAccept(const uint64_t sessionID) override;
	virtual void OnRelease(const uint64_t sessionID) override;
	virtual void OnReceive(const uint64_t sessionID, Serializer* packet) override;

public: // 게터, 세터

	struct SectorMonitorInfo
	{
		uint32_t SectorX;
		uint32_t SectorY;
		uint32_t Count;
	};

	inline uint32_t	GetPlayerPoolSize(void) const { return mPlayerPool.GetTotalCreatedObjectCount(); }
	inline uint32_t	GetRealPlayerCount(void) const { return mRealPlayerCount; }

	inline uint32_t GetTotalMaxWorkQueueSize(void) const { return mTotalMaxWorkQueueSize; }

	// 업데이트 스레드 큐 초당 최대 사이즈 반환 후 0으로 초기화 (모니터링용)
	inline uint32_t	GetMaxWorkQueueSizePerSecond(void) { return InterlockedExchange(&mMaxWorkQueueSizePerSecond, 0); }

	// 업데이트 스레드 큐 초당 최소 사이즈 반환 후 UINT32_MAX로 초기화 (모니터링용)
	inline uint32_t	GetMinWorkQueueSizePerSecond(void) { return InterlockedExchange(&mMinWorkQueueSizePerSecond, UINT32_MAX); }
	
	// 업데이트 스레드 큐 메세지 처리 횟수 반환 후 0으로 초기화 모니터링용)
	inline uint32_t	GetProcessedMessageCountPerSecond(void) { return InterlockedExchange(&mProcessedMessageCountPerSecond, 0); }

	// 각 섹터에 존재하는 플레이어 수를 반환
	// 정확한 한 시점의 정보는 아니며, 대략적인 분포도를 파악
	inline void		GetSectorNonitorInfos(std::vector<SectorMonitorInfo>* outDatas)
	{
		constexpr size_t TOTAL_SECTOR_COUNT = static_cast<size_t>(SECTOR_WIDTH_AND_HEIGHT) * SECTOR_WIDTH_AND_HEIGHT;

		outDatas->clear();
		outDatas->reserve(TOTAL_SECTOR_COUNT);

		for (int i = 0; i < SECTOR_WIDTH_AND_HEIGHT; ++i)
		{
			for (int j = 0; j < SECTOR_WIDTH_AND_HEIGHT; ++j)
			{
				SectorMonitorInfo info;
				info.SectorX = j;
				info.SectorY = i;
				info.Count = static_cast<uint32_t>(mSector[i][j].size());

				outDatas->push_back(info);
			}
		}
	}

private: // 메세지 관련

	// 로그인 요청
	void process_CS_CHAT_REQ_LOGIN(const uint64_t sessionID, const int64_t accountNo, const WCHAR id[], const WCHAR nickName[], const char sessionKey[]);
	
	// 섹터 이동 요청
	void process_CS_CHAT_REQ_SECTOR_MOVE(const uint64_t sessionID, const int64_t accountNo, const WORD sectorX, const WORD sectorY);
	
	// 채팅
	void process_CS_CHAT_REQ_MESSAGE(const uint64_t sessionID, const int64_t accountNo, const WORD messageLen, const WCHAR message[]);
	
	// 하트비트
	void process_CS_CHAT_REQ_HEARTBEAT(const uint64_t sessionID);
	
	// 세션 connect
	void process_SessionAccept(const uint64_t sessionID);
	
	// 세션 disconnect
	void process_SessionReleased(const uint64_t sessionID);

	inline static Serializer* createMessage_CS_CHAT_RES_LOGIN(const BYTE Status, const int64_t AccountNo)
	{
		Serializer* packet = Serializer::Alloc();

		*packet << (WORD)en_PACKET_CS_CHAT_RES_LOGIN << Status << AccountNo;

		return packet;
	}
	inline static Serializer* createMessage_CS_CHAT_RES_SECTOR_MOVE(const int64_t AccountNo, const WORD sectorX, const WORD sectorY)
	{
		Serializer* packet = Serializer::Alloc();

		*packet << (WORD)en_PACKET_CS_CHAT_RES_SECTOR_MOVE << AccountNo << sectorX << sectorY;

		return packet;
	}
	inline static Serializer* createMessage_CS_CHAT_RES_MESSAGE(const int64_t AccountNo, const WCHAR id[], const WCHAR nickName[], const WORD messageLen, const WCHAR message[])
	{
		Serializer* packet = Serializer::Alloc();

		*packet << (WORD)en_PACKET_CS_CHAT_RES_MESSAGE << AccountNo;
		packet->InsertByte((const char*)id, sizeof(WCHAR) * 20);
		packet->InsertByte((const char*)nickName, sizeof(WCHAR) * 20);

		*packet << messageLen;
		packet->InsertByte((const char*)message, messageLen);

		return packet;
	}

private:

	// 싱글 업데이트 스레드
	static unsigned int updateThread(void* chatServer);

	// 세션 ID에 대한 플레이어를 얻는다
	Player* findPlayerOrNull(const uint64_t sessionID);

	// mPlayerMap을 순회하면서 타임아웃 체크
	void timeoutCheck(void);

private:

	enum
	{
		SECTOR_WIDTH_AND_HEIGHT = 50,
	};

	HANDLE									mUpdateThread;
	bool									mbUpdateThreadRunning;

	LockFreeQueue<Work>						mWorkQueue;
	HANDLE									mWorkQueueEvent;
	uint32_t								mMinWorkQueueSizePerSecond;
	uint32_t								mMaxWorkQueueSizePerSecond;
	uint32_t								mTotalMaxWorkQueueSize;
	uint32_t								mProcessedMessageCountPerSecond;

	std::unordered_map<uint64_t, Player*>	mPlayerMap;
	inline static OBJECT_POOL<Player>		mPlayerPool;
	uint32_t								mRealPlayerCount = 0; // 인증 성공한 플레이어 수

	std::list<uint64_t>						mSector[SECTOR_WIDTH_AND_HEIGHT][SECTOR_WIDTH_AND_HEIGHT];

	HANDLE									mTimeOutCheckEvent;
	uint32_t								mTimeoutCheckInterval;
	uint32_t								mTimeoutLoggedIn;
	uint32_t								mTimeoutNotLoggedIn;

	bool									mbRedisUsed;
};