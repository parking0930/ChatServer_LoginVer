#pragma once
#include <unordered_map>
#include <list>
#include "CNetServer.h"
#define dfSECTOR_MAX_X	50
#define dfSECTOR_MAX_Y	50
#define	dfLOGIN_PACKET_RECV_TIMEOUT		5000
#define dfNETWORK_PACKET_RECV_TIMEOUT	40000

using namespace std;

struct REQ_LOGIN
{
	INT64	AccountNo;
	WCHAR	ID[20];				// null 포함
	WCHAR	Nickname[20];		// null 포함
	char	SessionKey[64];		// 인증토큰
};
#pragma pack(1)
struct RES_LOGIN
{
	WORD	Type;
	BYTE	Status;				// 0:실패	1:성공
	INT64	AccountNo;
};

struct SECTOR_MOVE
{
	INT64	AccountNo;
	WORD	SectorX;
	WORD	SectorY;
};
#pragma pack()

struct SECTOR_POS
{
	int iX;
	int iY;
};

struct SECTOR_AROUND
{
	int iCount;
	SECTOR_POS around[9];
};

class ChatServer :public CNetServer
{
private:
	class Character;
	class ContentsJob
	{
	public:
		enum CONTENTS_JOB_TYPE
		{
			en_Default, en_Request, en_Join, en_Leave, en_Recv, en_DeleteWaitCharacter
		};
		enum CONTENTS_REQ_TYPE
		{
			en_Redis
		};
		ContentsJob();
		ContentsJob(const ContentsJob& job) = delete;
		ContentsJob(CONTENTS_JOB_TYPE type, UINT64 sessionID, PVOID pMyData);
		static ContentsJob* Alloc(CONTENTS_JOB_TYPE type, unsigned __int64 sessionID, PVOID pMyData);
		static void Free(ContentsJob* pJob);
	public:
		static MemoryPoolTLS<ContentsJob>	contentsJobPool;
		CONTENTS_JOB_TYPE	_type;
		PVOID				_pMyData;
		UINT64				_sessionID;
	};
	class RedisJob
	{
	public:
		RedisJob();
		RedisJob(const RedisJob& job) = delete;
		RedisJob(Character* pCharacter, PVOID pMyData);
		static RedisJob* Alloc(Character* pCharacter, PVOID pMyData);
		static void Free(RedisJob* pJob);
	public:
		static MemoryPoolTLS<RedisJob>	redisJobPool;
		Character*	_pCharacter;
		PVOID		_pMyData;
	};
	class Character
	{
	public:
		Character();
		static Character* Alloc();
		static void	Free(Character* pCharacter);
	public:
		unsigned __int64		_sessionID;
		struct ACCOUNT_INFO
		{
			INT64				_accountNo;
			WCHAR				_id[20];
			WCHAR				_nickname[20];
			char				_sessionKey[65];
		}_info;
		WORD					_SectorX;
		WORD					_SectorY;
		ULONGLONG				_lastRecvTime;
	private:
		static MemoryPoolTLS<Character> poolManager;
	};
	struct LOGIN_WAIT_CHARACTER
	{
	public:
		unsigned __int64	_sessionID;
		ULONGLONG			_lastRecvTime;
		static LOGIN_WAIT_CHARACTER* Alloc();
		static void Free(LOGIN_WAIT_CHARACTER* pJob);
	private:
		static MemoryPoolTLS<LOGIN_WAIT_CHARACTER> _poolManager;
	};
public:
	ChatServer();
	~ChatServer();
	bool OnConnectionRequest(wchar_t* ip, unsigned short port);
	void OnClientJoin(unsigned __int64 sessionID);
	void OnClientLeave(unsigned __int64 sessionID);
	void OnRecv(unsigned __int64 sessionID, CNetPacket* pPacket);
	void OnError(int errorcode, wchar_t* str);
	void OnStart();
	void OnStop();
	void SendAround(Character* pCharacter, CNetPacket* clpPacket);
	int GetWaitCharactorCount();
	int GetLoginCharacterCount();
	int GetAcceptTotal();
private:
	Character* FindCharacter(unsigned __int64 sessionID);
	LOGIN_WAIT_CHARACTER* FindWaitCharacter(unsigned __int64 sessionID);
	void Join(unsigned __int64 sessionID, LOGIN_WAIT_CHARACTER* pWaitChar);
	void Leave(unsigned __int64 sessionID);
	void Login(unsigned __int64 sessionID, CNetPacket* pPacket);
	void SectorMove(unsigned __int64 sessionID, CNetPacket* pPacket);
	void DeleteLoginWaitCharacter(unsigned __int64 sessionID);
	void DeleteCharacter(unsigned __int64 sessionID);
	void MessageCS(unsigned __int64 sessionID, CNetPacket* pPacket);
	void Heartbeat(unsigned __int64 sessionID, CNetPacket* pPacket);
	///////////////////////////////////////////////////////////////////
	void RedisResultHandling(unsigned __int64 sessionID, CNetPacket* pPacket);
private:
	static unsigned WINAPI stContentsThread(LPVOID lpParam);
	void ContentsThread();
	static unsigned WINAPI stRedisThread(LPVOID lpParam);
	void RedisThread();
	void CheckHeartbeat();
	void ContentsHandler(ContentsJob* pJob);
	void RequestHandler(unsigned __int64 sessionID, CNetPacket* pPacket);
	void RecvHandler(unsigned __int64 sessionID, CNetPacket* pPacket);
	void GetSectorAround(int iSectorX, int iSectorY, SECTOR_AROUND* pSectorAround);
private:
	INT _acceptTotal;
	unordered_map<UINT64, Character*> _characterMap;
	unordered_map<UINT64, LOGIN_WAIT_CHARACTER*> _waitCharMap;
	CLockFreeQueue<ContentsJob*> _contentsQueue;
	CLockFreeQueue<RedisJob*> _redisQueue;
	volatile bool _isServerOff;
	list<Character*> sector[dfSECTOR_MAX_Y][dfSECTOR_MAX_X];
	HANDLE _hUpdateThread;
	HANDLE _hContentsEvent;
	HANDLE _hRedisThread;
	HANDLE _hRedisEvent;
	//DWORD	_tlsIdxRedis;
};