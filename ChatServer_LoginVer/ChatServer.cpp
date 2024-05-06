#include <cpp_redis/cpp_redis>
#pragma comment (lib, "cpp_redis.lib")
#pragma comment (lib, "tacopie.lib")
#pragma comment (lib, "ws2_32.lib")
#include "ChatServer.h"
#include "CommonProtocol.h"

MemoryPoolTLS<ChatServer::ContentsJob> ChatServer::ContentsJob::contentsJobPool(0, POOL_MAX_ALLOC, true);

ChatServer::ContentsJob::ContentsJob() :_type(en_Default), _sessionID(0), _pMyData(nullptr) {}
ChatServer::ContentsJob::ContentsJob(CONTENTS_JOB_TYPE type, UINT64 sessionID, PVOID pMyData) :_type(type), _sessionID(sessionID), _pMyData(pMyData) {}

ChatServer::ContentsJob* ChatServer::ContentsJob::Alloc(CONTENTS_JOB_TYPE type, unsigned __int64 sessionID, PVOID pMyData)
{
	return contentsJobPool.Alloc(type, sessionID, pMyData);
}

void ChatServer::ContentsJob::Free(ChatServer::ContentsJob* pJob)
{
	contentsJobPool.Free(pJob);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
MemoryPoolTLS<ChatServer::RedisJob> ChatServer::RedisJob::redisJobPool(0, POOL_MAX_ALLOC, true);

ChatServer::RedisJob::RedisJob() :_pCharacter(nullptr), _pMyData(nullptr) {}
ChatServer::RedisJob::RedisJob(Character* pCharacter, PVOID pMyData) : _pCharacter(pCharacter), _pMyData(pMyData) {}

ChatServer::RedisJob* ChatServer::RedisJob::Alloc(Character* pCharacter, PVOID pMyData)
{
	return redisJobPool.Alloc(pCharacter, pMyData);
}

void ChatServer::RedisJob::Free(ChatServer::RedisJob* pJob)
{
	redisJobPool.Free(pJob);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

MemoryPoolTLS<ChatServer::Character> ChatServer::Character::poolManager(0, POOL_MAX_ALLOC, true);
ChatServer::Character::Character() :_sessionID(0), _info({ 0, }), _SectorX(0), _SectorY(0), _lastRecvTime(0) {}

ChatServer::Character* ChatServer::Character::Alloc()
{
	return poolManager.Alloc();
}

void ChatServer::Character::Free(Character* pCharacter)
{
	poolManager.Free(pCharacter);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
MemoryPoolTLS<ChatServer::LOGIN_WAIT_CHARACTER> ChatServer::LOGIN_WAIT_CHARACTER::_poolManager(0, POOL_MAX_ALLOC, true);

ChatServer::LOGIN_WAIT_CHARACTER* ChatServer::LOGIN_WAIT_CHARACTER::Alloc()
{
	return _poolManager.Alloc();
}

void ChatServer::LOGIN_WAIT_CHARACTER::Free(LOGIN_WAIT_CHARACTER* pJob)
{
	_poolManager.Free(pJob);
}
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

CNetPacket& operator>>(CNetPacket& packet, SECTOR_MOVE& sMove)
{
	packet >> sMove.AccountNo;
	packet >> sMove.SectorX;
	packet >> sMove.SectorY;
	return packet;
}

ChatServer::ChatServer() :_acceptTotal(0), _hContentsEvent(NULL), _hUpdateThread(NULL),
							_hRedisThread(NULL), _hRedisEvent(NULL), _isServerOff(false) {}

ChatServer::~ChatServer()
{
	_isServerOff = true;
	if (_hUpdateThread != NULL)
	{
		if (WaitForSingleObject(_hUpdateThread, 5000) == WAIT_TIMEOUT)
			TerminateThread(_hUpdateThread, 1);
		CloseHandle(_hUpdateThread);

	}
	if (_hRedisThread != NULL)
	{
		if (WaitForSingleObject(_hRedisThread, 5000) == WAIT_TIMEOUT)
			TerminateThread(_hRedisThread, 1);
		CloseHandle(_hRedisThread);
	}
	if (_hContentsEvent != NULL)
	{
		CloseHandle(_hContentsEvent);
	}
	if (_hRedisEvent != NULL)
	{
		CloseHandle(_hRedisEvent);
	}
}

bool ChatServer::OnConnectionRequest(wchar_t* ip, unsigned short port) { return true; }
void ChatServer::OnError(int errorcode, wchar_t* str) {}

unsigned WINAPI ChatServer::stContentsThread(LPVOID lpParam)
{
	ChatServer* pChatServer = (ChatServer*)lpParam;
	pChatServer->ContentsThread();
	return 0;
}

unsigned WINAPI ChatServer::stRedisThread(LPVOID lpParam)
{
	ChatServer* pChatServer = (ChatServer*)lpParam;
	pChatServer->RedisThread();
	return 0;
}

void ChatServer::OnClientJoin(unsigned __int64 sessionID)
{
	++_acceptTotal;
	LOGIN_WAIT_CHARACTER* newWait = LOGIN_WAIT_CHARACTER::Alloc();
	newWait->_sessionID = sessionID;
	newWait->_lastRecvTime = GetTickCount64();
	// ContentsJob EnQ
	ContentsJob* pJob = ContentsJob::Alloc(ContentsJob::en_Join, sessionID, (PVOID)newWait);
	_contentsQueue.Enqueue(pJob);
	SetEvent(_hContentsEvent);
}

void ChatServer::OnClientLeave(unsigned __int64 sessionID)
{
	ContentsJob* pJob = ContentsJob::Alloc(ContentsJob::en_Leave, sessionID, nullptr);
	_contentsQueue.Enqueue(pJob);
	SetEvent(_hContentsEvent);
}

void ChatServer::OnRecv(unsigned __int64 sessionID, CNetPacket* pPacket)
{
	ContentsJob* pJob = ContentsJob::Alloc(ContentsJob::en_Recv, sessionID, pPacket);
	pPacket->AddRefCount();
	_contentsQueue.Enqueue(pJob);
	SetEvent(_hContentsEvent);
}

void ChatServer::OnStart()
{
	_acceptTotal = 0;
	_hContentsEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	_hRedisEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
	_hUpdateThread = (HANDLE)_beginthreadex(NULL, 0, stContentsThread, (LPVOID)this, NULL, NULL);
	_hRedisThread = (HANDLE)_beginthreadex(NULL, 0, stRedisThread, (LPVOID)this, NULL, NULL);
}

void ChatServer::OnStop()
{
	_isServerOff = true;
	if (_hUpdateThread != NULL)
	{
		if (WaitForSingleObject(_hUpdateThread, 5000) == WAIT_TIMEOUT)
			TerminateThread(_hUpdateThread, 1);
		CloseHandle(_hUpdateThread);
		_hUpdateThread = NULL;
	}
	if (_hRedisThread != NULL)
	{
		if (WaitForSingleObject(_hRedisThread, 5000) == WAIT_TIMEOUT)
			TerminateThread(_hRedisThread, 1);
		CloseHandle(_hRedisThread);
		_hRedisThread = NULL;
	}
	if (_hContentsEvent != NULL)
	{
		CloseHandle(_hContentsEvent);
		_hContentsEvent = NULL;
	}
	if (_hRedisEvent != NULL)
	{
		CloseHandle(_hRedisEvent);
		_hRedisEvent = NULL;
	}
}

void ChatServer::SendAround(Character* pCharacter, CNetPacket* clpPacket)
{
	SECTOR_AROUND stAround;
	GetSectorAround(pCharacter->_SectorX, pCharacter->_SectorY, &stAround);

	list<Character*>* pSector;
	list<Character*>::iterator it;
	for (int i = 0; i < stAround.iCount; i++)
	{
		pSector = &sector[stAround.around[i].iY][stAround.around[i].iX];
		it = pSector->begin();
		while (it != pSector->end())
		{
			SendPacket((*it)->_sessionID, clpPacket);
			//printf("SendAround:%lld\n", pCharacter->_info._accountNo);
			++it;
		}
	}
}

void ChatServer::ContentsThread()
{
	ContentsJob* pJob;
	do
	{
		WaitForSingleObject(_hContentsEvent, INFINITE);
		while (_contentsQueue.Dequeue(&pJob))
			ContentsHandler(pJob);
		//CheckHeartbeat();
	} while (!_isServerOff);
}

void ChatServer::RedisThread()
{
	cpp_redis::client client;
	client.connect();
	RedisJob* pJob;
	do
	{
		WaitForSingleObject(_hRedisEvent, INFINITE);
		while (_redisQueue.Dequeue(&pJob))
		{
			std::future<cpp_redis::reply> get_reply = client.get(to_string(reinterpret_cast<INT64>(pJob->_pMyData)));
			client.sync_commit();

			CNetPacket* pDataPacket = CNetPacket::Alloc();
			*pDataPacket << ContentsJob::en_Redis;

			string result = get_reply.get().as_string();
			if (result.compare(pJob->_pCharacter->_info._sessionKey) == 0)
			{
				*pDataPacket << (BYTE)1;
			}
			else
			{
				*pDataPacket << (BYTE)0;
			}
			ContentsJob* pContentsJob = ContentsJob::Alloc(ContentsJob::en_Request, (ULONGLONG)pJob->_pCharacter->_sessionID, pDataPacket);
			_contentsQueue.Enqueue(pContentsJob);
			SetEvent(_hContentsEvent);
		}
	} while (!_isServerOff);
}

void ChatServer::ContentsHandler(ContentsJob* pJob)
{
	switch (pJob->_type)
	{
	case ContentsJob::en_Request:
		RequestHandler(pJob->_sessionID, reinterpret_cast<CNetPacket*>(pJob->_pMyData));
		break;
	case ContentsJob::en_Join:
		Join(pJob->_sessionID, reinterpret_cast<LOGIN_WAIT_CHARACTER*>(pJob->_pMyData));
		break;
	case ContentsJob::en_Leave:
		Leave(pJob->_sessionID);
		break;
	case ContentsJob::en_Recv:
		RecvHandler(pJob->_sessionID, reinterpret_cast<CNetPacket*>(pJob->_pMyData));
		break;
	case ContentsJob::en_DeleteWaitCharacter:
		DeleteLoginWaitCharacter(pJob->_sessionID);
		break;
	default:
		break;
	}
	ContentsJob::Free(pJob);
}

void ChatServer::RequestHandler(unsigned __int64 sessionID, CNetPacket* pPacket)
{
	UINT type;
	*pPacket >> type;
	switch (type)
	{
	case ContentsJob::en_Redis:
		RedisResultHandling(sessionID, pPacket);
		break;
	default:
		break;
	}
	CNetPacket::Free(pPacket);
}

void ChatServer::RecvHandler(unsigned __int64 sessionID, CNetPacket* pPacket)
{
	WORD type;
	*pPacket >> type;
	switch (type)
	{
	case en_PACKET_CS_CHAT_REQ_LOGIN:
		Login(sessionID, pPacket);
		break;
	case en_PACKET_CS_CHAT_REQ_SECTOR_MOVE:
		SectorMove(sessionID, pPacket);
		break;
	case en_PACKET_CS_CHAT_REQ_MESSAGE:
		MessageCS(sessionID, pPacket);
		break;
	case en_PACKET_CS_CHAT_REQ_HEARTBEAT:
		Heartbeat(sessionID, pPacket);
		break;
	default:
		break;
	}
	CNetPacket::Free(pPacket);
}

ChatServer::Character* ChatServer::FindCharacter(unsigned __int64 sessionID)
{
	unordered_map<UINT64, Character*>::iterator it = _characterMap.find(sessionID);
	if (it == _characterMap.end())
		return nullptr;
	else
		return it->second;
}
ChatServer::LOGIN_WAIT_CHARACTER* ChatServer::FindWaitCharacter(unsigned __int64 sessionID)
{
	unordered_map<UINT64, LOGIN_WAIT_CHARACTER*>::iterator it = _waitCharMap.find(sessionID);
	if (it == _waitCharMap.end())
		return nullptr;
	else
		return it->second;
}

void ChatServer::Join(unsigned __int64 sessionID, LOGIN_WAIT_CHARACTER* pWaitChar)
{
	_waitCharMap.insert(pair<UINT64, LOGIN_WAIT_CHARACTER*>(sessionID, pWaitChar));
}

void ChatServer::Leave(unsigned __int64 sessionID)
{
	DeleteLoginWaitCharacter(sessionID);
	DeleteCharacter(sessionID);
}

void ChatServer::Login(unsigned __int64 sessionID, CNetPacket* pPacket)
{
	LOGIN_WAIT_CHARACTER* pWaitChar = FindWaitCharacter(sessionID);
	if (pWaitChar == nullptr)
	{
		Disconnect(sessionID);
		return;
	}

	DeleteLoginWaitCharacter(sessionID);

	ContentsJob* pJob = ContentsJob::Alloc(ContentsJob::en_DeleteWaitCharacter, sessionID, nullptr);
	_contentsQueue.Enqueue(pJob);
	SetEvent(_hContentsEvent);

	Character* pCharacter = Character::Alloc();
	pCharacter->_sessionID = sessionID;
	pCharacter->_SectorX = -1;
	pCharacter->_SectorY = -1;
	pCharacter->_lastRecvTime = GetTickCount64();
	pPacket->GetData(reinterpret_cast<char*>(&pCharacter->_info), sizeof(REQ_LOGIN));
	pCharacter->_info._sessionKey[64] = '\0';
	_characterMap.insert(pair<UINT64, Character*>(sessionID, pCharacter));

	// Redis Check
	RedisJob* pRdsJob = RedisJob::Alloc(pCharacter, reinterpret_cast<PVOID>(pCharacter->_info._accountNo));
	_redisQueue.Enqueue(pRdsJob);
	SetEvent(_hRedisEvent);
}

void ChatServer::SectorMove(unsigned __int64 sessionID, CNetPacket* pPacket)
{
	Character* pCharacter = FindCharacter(sessionID);
	if (pCharacter == nullptr)
	{
		Disconnect(sessionID);
		return;
	}
	pCharacter->_lastRecvTime = GetTickCount64();

	SECTOR_MOVE sMove;
	*pPacket >> sMove;

	if (pCharacter->_info._accountNo != sMove.AccountNo)
	{
		Disconnect(sessionID);
		return;
	}

	if (sMove.SectorX >= dfSECTOR_MAX_X || sMove.SectorY >= dfSECTOR_MAX_Y)
	{
		Disconnect(sessionID);
		return;
	}

	if (pCharacter->_SectorX != 65535 && pCharacter->_SectorY != 65535)
		sector[pCharacter->_SectorY][pCharacter->_SectorX].remove(pCharacter);

	pCharacter->_SectorX = sMove.SectorX;
	pCharacter->_SectorY = sMove.SectorY;
	sector[pCharacter->_SectorY][pCharacter->_SectorX].push_back(pCharacter);

	CNetPacket* pSndPkt = CNetPacket::Alloc();
	*pSndPkt << (WORD)en_PACKET_CS_CHAT_RES_SECTOR_MOVE;
	pSndPkt->SetData((char*)&sMove, sizeof(SECTOR_MOVE));
	SendPacket(sessionID, pSndPkt);
	CNetPacket::Free(pSndPkt);
	//printf("SectorMove:%lld\n", pCharacter->_info._accountNo);
}

void ChatServer::DeleteLoginWaitCharacter(unsigned __int64 sessionID)
{
	LOGIN_WAIT_CHARACTER* pWaitCharacter = FindWaitCharacter(sessionID);
	if (pWaitCharacter == nullptr)
		return;
	_waitCharMap.erase(sessionID);
	LOGIN_WAIT_CHARACTER::Free(pWaitCharacter);
}

void ChatServer::DeleteCharacter(unsigned __int64 sessionID)
{
	Character* pCharacter = FindCharacter(sessionID);
	if (pCharacter == nullptr)
		return;
	_characterMap.erase(sessionID);
	if (pCharacter->_SectorX != 65535 && pCharacter->_SectorY != 65535)
		sector[pCharacter->_SectorY][pCharacter->_SectorX].remove(pCharacter);
	Character::Free(pCharacter);
}

void ChatServer::MessageCS(unsigned __int64 sessionID, CNetPacket* pPacket)
{
	Character* pCharacter = FindCharacter(sessionID);
	if (pCharacter == nullptr)
	{
		Disconnect(sessionID);
		return;
	}
	pCharacter->_lastRecvTime = GetTickCount64();

	INT64 accountNo;
	WORD msgLen;
	pPacket->GetData((char*)&accountNo, sizeof(INT64));
	pPacket->GetData((char*)&msgLen, sizeof(WORD));

	WCHAR* pMessage = new WCHAR[msgLen];
	pPacket->GetData((char*)pMessage, msgLen);

	WORD type = en_PACKET_CS_CHAT_RES_MESSAGE;
	CNetPacket* pSndPkt = CNetPacket::Alloc();
	pSndPkt->SetData((char*)&type, sizeof(WORD));
	pSndPkt->SetData((char*)&pCharacter->_info._accountNo, sizeof(pCharacter->_info._accountNo));
	pSndPkt->SetData((char*)&pCharacter->_info._id, sizeof(pCharacter->_info._id));
	pSndPkt->SetData((char*)&pCharacter->_info._nickname, sizeof(pCharacter->_info._nickname));
	pSndPkt->SetData((char*)&msgLen, sizeof(WORD));
	pSndPkt->SetData((char*)pMessage, msgLen);
	SendAround(pCharacter, pSndPkt);
	CNetPacket::Free(pSndPkt);
	delete[] pMessage;
	//printf("Msg:%lld\n", pCharacter->_info._accountNo);
}

void ChatServer::Heartbeat(unsigned __int64 sessionID, CNetPacket* pPacket)
{
	Character* pCharacter = FindCharacter(sessionID);
	if (pCharacter == nullptr)
	{
		Disconnect(sessionID);
		return;
	}
	pCharacter->_lastRecvTime = GetTickCount64();
}

//////////////////////////////////////////////////////////////////////////////

void ChatServer::RedisResultHandling(unsigned __int64 sessionID, CNetPacket* pPacket)
{
	Character* pCharacter = FindCharacter(sessionID);
	if (pCharacter == nullptr)
	{
		Disconnect(sessionID);
		return;
	}
	
	// Response »ý¼º
	RES_LOGIN res;
	res.Type = en_PACKET_CS_CHAT_RES_LOGIN;
	*pPacket >> res.Status;
	res.AccountNo = pCharacter->_info._accountNo;

	CNetPacket* pSendPkt = CNetPacket::Alloc();
	pSendPkt->SetData(reinterpret_cast<char*>(&res), sizeof(RES_LOGIN));
	SendPacket(sessionID, pSendPkt);
	CNetPacket::Free(pSendPkt);

	if (res.Status == 0)
	{
		DeleteCharacter(sessionID);
	}
}

//////////////////////////////////////////////////////////////////////////////
void ChatServer::CheckHeartbeat()
{
	Character* pCharacter;
	LOGIN_WAIT_CHARACTER* pWaitCharacter;
	unordered_map<UINT64, Character*>::iterator charIt = _characterMap.begin();
	unordered_map<UINT64, LOGIN_WAIT_CHARACTER*>::iterator waitIt = _waitCharMap.begin();
	ULONGLONG ulCurTick = GetTickCount64();
	while (waitIt != _waitCharMap.end())
	{
		pWaitCharacter = waitIt->second;
		++waitIt;
		if (ulCurTick - pWaitCharacter->_lastRecvTime > dfLOGIN_PACKET_RECV_TIMEOUT)
			Disconnect(pWaitCharacter->_sessionID);
	}
	while (charIt != _characterMap.end())
	{
		pCharacter = charIt->second;
		++charIt;
		if (ulCurTick - pCharacter->_lastRecvTime > dfNETWORK_PACKET_RECV_TIMEOUT)
			Disconnect(pCharacter->_sessionID);
	}
}

void ChatServer::GetSectorAround(int iSectorX, int iSectorY, SECTOR_AROUND* pSectorAround)
{
	int iCntX, iCntY;
	--iSectorX;
	--iSectorY;
	pSectorAround->iCount = 0;

	for (iCntY = 0; iCntY < 3; iCntY++)
	{
		if (iSectorY + iCntY < 0 || iSectorY + iCntY >= dfSECTOR_MAX_Y)
			continue;

		for (iCntX = 0; iCntX < 3; iCntX++)
		{
			if (iSectorX + iCntX < 0 || iSectorX + iCntX >= dfSECTOR_MAX_X)
				continue;

			pSectorAround->around[pSectorAround->iCount].iX = iSectorX + iCntX;
			pSectorAround->around[pSectorAround->iCount].iY = iSectorY + iCntY;
			pSectorAround->iCount++;
		}
	}
}

int ChatServer::GetWaitCharactorCount()
{
	return _waitCharMap.size();
}

int ChatServer::GetLoginCharacterCount()
{
	return _characterMap.size();
}

int ChatServer::GetAcceptTotal()
{
	return _acceptTotal;
}