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
#pragma warning(disable: 26495) // ���� �ʱ�ȭ ��� ����
	ChatServer() = default;
#pragma warning(pop)

	virtual ~ChatServer() = default;

public: // ���� ���� �� ���� �Լ���

	// Ÿ�� �ƿ� üũ ����
	inline void		SetTimeoutCheckInterval(const uint32_t interval) { mTimeoutCheckInterval = interval; }
	
	// �α����� ������ �ִ� Ÿ�Ӿƿ� �ð�
	inline void		SetTimeoutLoggedIn(const uint32_t timeout) { mTimeoutLoggedIn = timeout; }

	// �α������� ���� ������ �ִ� Ÿ�Ӿƿ� �ð�
	inline void		SetTimeoutNotLoggedIn(const uint32_t timeout) { mTimeoutNotLoggedIn = timeout; }

	// ���� ��� ����
	inline void		UseRedis(void) { mbRedisUsed = true; }

public:

	// ���� ����
	virtual void Start(
		const uint16_t port, 
		const uint32_t maxSessionCount, 
		const uint32_t iocpConcurrentThreadCount, 
		const uint32_t iocpWorkerThreadCount) override;

	// ���� ���� (��� �����尡 ����� �� ���� �����)
	virtual void Shutdown(void) override;

	// NetServer��(��) ���� ��ӵ�
	virtual void OnAccept(const uint64_t sessionID) override;
	virtual void OnRelease(const uint64_t sessionID) override;
	virtual void OnReceive(const uint64_t sessionID, Serializer* packet) override;

public: // ����, ����

	struct SectorMonitorInfo
	{
		uint32_t SectorX;
		uint32_t SectorY;
		uint32_t Count;
	};

	inline uint32_t	GetPlayerPoolSize(void) const { return mPlayerPool.GetTotalCreatedObjectCount(); }
	inline uint32_t	GetRealPlayerCount(void) const { return mRealPlayerCount; }

	inline uint32_t GetTotalMaxWorkQueueSize(void) const { return mTotalMaxWorkQueueSize; }

	// ������Ʈ ������ ť �ʴ� �ִ� ������ ��ȯ �� 0���� �ʱ�ȭ (����͸���)
	inline uint32_t	GetMaxWorkQueueSizePerSecond(void) { return InterlockedExchange(&mMaxWorkQueueSizePerSecond, 0); }

	// ������Ʈ ������ ť �ʴ� �ּ� ������ ��ȯ �� UINT32_MAX�� �ʱ�ȭ (����͸���)
	inline uint32_t	GetMinWorkQueueSizePerSecond(void) { return InterlockedExchange(&mMinWorkQueueSizePerSecond, UINT32_MAX); }
	
	// ������Ʈ ������ ť �޼��� ó�� Ƚ�� ��ȯ �� 0���� �ʱ�ȭ ����͸���)
	inline uint32_t	GetProcessedMessageCountPerSecond(void) { return InterlockedExchange(&mProcessedMessageCountPerSecond, 0); }

	// �� ���Ϳ� �����ϴ� �÷��̾� ���� ��ȯ
	// ��Ȯ�� �� ������ ������ �ƴϸ�, �뷫���� �������� �ľ�
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

private: // �޼��� ����

	// �α��� ��û
	void process_CS_CHAT_REQ_LOGIN(const uint64_t sessionID, const int64_t accountNo, const WCHAR id[], const WCHAR nickName[], const char sessionKey[]);
	
	// ���� �̵� ��û
	void process_CS_CHAT_REQ_SECTOR_MOVE(const uint64_t sessionID, const int64_t accountNo, const WORD sectorX, const WORD sectorY);
	
	// ä��
	void process_CS_CHAT_REQ_MESSAGE(const uint64_t sessionID, const int64_t accountNo, const WORD messageLen, const WCHAR message[]);
	
	// ��Ʈ��Ʈ
	void process_CS_CHAT_REQ_HEARTBEAT(const uint64_t sessionID);
	
	// ���� connect
	void process_SessionAccept(const uint64_t sessionID);
	
	// ���� disconnect
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

	// �̱� ������Ʈ ������
	static unsigned int updateThread(void* chatServer);

	// ���� ID�� ���� �÷��̾ ��´�
	Player* findPlayerOrNull(const uint64_t sessionID);

	// mPlayerMap�� ��ȸ�ϸ鼭 Ÿ�Ӿƿ� üũ
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
	uint32_t								mRealPlayerCount = 0; // ���� ������ �÷��̾� ��

	std::list<uint64_t>						mSector[SECTOR_WIDTH_AND_HEIGHT][SECTOR_WIDTH_AND_HEIGHT];

	HANDLE									mTimeOutCheckEvent;
	uint32_t								mTimeoutCheckInterval;
	uint32_t								mTimeoutLoggedIn;
	uint32_t								mTimeoutNotLoggedIn;

	bool									mbRedisUsed;
};