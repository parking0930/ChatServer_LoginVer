#pragma warning(disable: 6258)
#pragma comment(lib,"ws2_32")
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <Windows.h>
#include <process.h>
#include <stdio.h>
#include "CNetServer.h"
#include "CrashDump.h"

/////////////////////////////////////////////////////////////////////////

CNetServer::CNetServer()
{
	InitSettings();
}

CNetServer::~CNetServer()
{
	Stop();
}

bool CNetServer::Start(const wchar_t* ip, unsigned short port, int workerThreadCnt, int runningThreadCnt, bool nagleOff, int maxUser)
{
	if (workerThreadCnt == 0 || runningThreadCnt == 0 || maxUser == 0 ||
		port < 0 || port > 65535 || maxUser < 0 || maxUser > 65535)
		return false;

	InitSettings();
	_workerThreadCnt = workerThreadCnt;
	_nagleOff = nagleOff;
	_maxUser = maxUser;
	_monitorSwitch = true;

	// Prepare listen socket 
	WSADATA wsa;
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
		return false;

	_listen_sock = socket(AF_INET, SOCK_STREAM, 0);
	if (_listen_sock == INVALID_SOCKET)
		return false;

	SOCKADDR_IN sockaddr;
	sockaddr.sin_family = AF_INET;
	sockaddr.sin_port = htons(port);
	if (ip == NULL)
		sockaddr.sin_addr.S_un.S_addr = htonl(INADDR_ANY);
	else
		InetPtonW(AF_INET, ip, &sockaddr.sin_addr);

	int retval;

	retval = bind(_listen_sock, (SOCKADDR*)&sockaddr, sizeof(sockaddr));
	if (retval == SOCKET_ERROR)
	{
		closesocket(_listen_sock);
		return false;
	}

	retval = listen(_listen_sock, SOMAXCONN);
	if (retval == SOCKET_ERROR)
	{
		closesocket(_listen_sock);
		return false;
	}

	// Create IOCP
	_hCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, NULL, runningThreadCnt);
	if (_hCP == NULL)
	{
		closesocket(_listen_sock);
		return false;
	}

	// Change server state
	_serverOn = true;

	// Create Session DataStructure
	_sessionArr = new SESSION[maxUser];
	int idx;
	for (idx = 0; idx < maxUser; idx++)
	{
		_sessionArr[idx].ioFlag.ioCount = 0;
		_freeSessionStack.Push(idx);
	}

	OnStart();

	// Create thread
	_hThreadArr = new HANDLE[workerThreadCnt];
	for (int i = 0; i < workerThreadCnt; i++)
		_hThreadArr[i] = (HANDLE)_beginthreadex(NULL, 0, RunIOCPWorkerThread, (LPVOID)this, NULL, NULL);
	_hAcceptThread = (HANDLE)_beginthreadex(NULL, 0, RunAcceptThread, (LPVOID)this, NULL, NULL);
	_hMonitorThread = (HANDLE)_beginthreadex(NULL, 0, RunMonitorThread, (LPVOID)this, NULL, NULL);

	// Check Thread state
	bool isSafe = true;
	do
	{
		for (int i = 0; i < workerThreadCnt; i++)
		{
			if (_hThreadArr[i] == NULL)
				isSafe = false;
		}

		if (_hAcceptThread == NULL || _hMonitorThread == NULL)
			isSafe = false;

		if (isSafe)
			break;

		// Exception handling
		for (int i = 0; i < workerThreadCnt; i++)
		{
			if (_hThreadArr[i] != NULL)
				CloseHandle(_hThreadArr[i]);
		}

		if (_hAcceptThread != NULL)
			CloseHandle(_hAcceptThread);

		if (_hMonitorThread != NULL)
			CloseHandle(_hMonitorThread);

		_serverOn = false;
		closesocket(_listen_sock);
		CloseHandle(_hCP);
		delete[] _hThreadArr;
		delete[] _sessionArr;
		return false;
	} while (1);

	return true;
}

void CNetServer::Stop()
{
	if (!_serverOn)
		return;

	OnStop();

	/*

	클라이언트 소켓에 대한 접속 종료 부분 추가 예정

	*/

	// Accept 중단
	closesocket(_listen_sock);
	if (WaitForSingleObject(_hAcceptThread, 10000) != WAIT_OBJECT_0)
		TerminateThread(_hAcceptThread, 1);

	// IOCP WorkerThread 중단
	int idx;
	for (idx = 0; idx < _workerThreadCnt; idx++)
		PostQueuedCompletionStatus(_hCP, NULL, NULL, NULL);
	if (WaitForMultipleObjects(_workerThreadCnt, _hThreadArr, TRUE, 20000) == WAIT_TIMEOUT)
	{
		for (idx = 0; idx < _workerThreadCnt; idx++)
		{
			DWORD exitCode;
			GetExitCodeThread(_hThreadArr[idx], &exitCode);
			if (exitCode == STILL_ACTIVE)
				TerminateThread(_hThreadArr[idx], 1);
		}
	}

	// Monitor Thread 중단
	_monitorSwitch = false;
	if (WaitForSingleObject(_hMonitorThread, 10000) != WAIT_OBJECT_0)
		TerminateThread(_hMonitorThread, 1);

	// 동적할당 메모리 해제
	delete[] _hThreadArr;
	delete[] _sessionArr;

	// 핸들 반환
	for (idx = 0; idx < _workerThreadCnt; idx++)
		CloseHandle(_hThreadArr[idx]);
	CloseHandle(_hAcceptThread);
	CloseHandle(_hMonitorThread);
	CloseHandle(_hCP);

	// 재시작 대비 멤버 변수 초기화
	InitSettings();
}

unsigned WINAPI CNetServer::RunMonitorThread(LPVOID lpParam)
{
	CNetServer* server = (CNetServer*)lpParam;
	server->MonitorThread();
	return 0;
}

void CNetServer::MonitorThread()
{
	while (_monitorSwitch)
	{
		_sendTPS = InterlockedExchange((LONG*)&_sendCnt, 0);
		_recvTPS = InterlockedExchange((LONG*)&_recvCnt, 0);
		_acceptTPS = InterlockedExchange((LONG*)&_acceptCnt, 0);
		Sleep(1000);
	}
}

unsigned WINAPI CNetServer::RunAcceptThread(LPVOID lpParam)
{
	CNetServer* server = (CNetServer*)lpParam;
	server->AcceptThread();
	return 0;
}

void CNetServer::AcceptThread()
{
	SOCKET listen_sock = _listen_sock;
	HANDLE hCP = _hCP;
	SESSION* sessionArr = _sessionArr;
	int bkMaxUser = _maxUser;
	bool nagleOff = _nagleOff;
	while (1)
	{
		SOCKADDR_IN clientAddr;
		int addrLen = sizeof(clientAddr);
		SOCKET client_sock = accept(listen_sock, (SOCKADDR*)&clientAddr, &addrLen);
		if (client_sock == SOCKET_ERROR)
		{
			DWORD err = WSAGetLastError();
			if (err == WSAEINTR)
				return;
			else
				return;
		}

		InterlockedIncrement((LONG*)&_acceptCnt);

		// Set socket option
		int bufsize = 0; // 100% overlapped processing (send only)
		setsockopt(client_sock, SOL_SOCKET, SO_SNDBUF, (const char*)&bufsize, sizeof(bufsize));

		LINGER linger = { 1, 0 }; // Prevent server connecting from remaining TIME_WAIT state
		setsockopt(client_sock, SOL_SOCKET, SO_LINGER, (char*)&linger, sizeof(linger));

		if (nagleOff) // Nagle algorithm
		{
			int ngOpt = TRUE;
			setsockopt(client_sock, IPPROTO_TCP, TCP_NODELAY, (const char*)&ngOpt, sizeof(ngOpt));
		}

		// Check now user count
		if (_nowSession == bkMaxUser)
		{
			closesocket(client_sock);
			continue;
		}

		// Contents part connect request logic
		WCHAR ipBuf[16];
		InetNtopW(AF_INET, &clientAddr.sin_addr, ipBuf, 16);
		if (OnConnectionRequest(ipBuf, ntohs(clientAddr.sin_port)) == false)
		{
			closesocket(client_sock);
			continue;
		}

		InterlockedIncrement((PLONG)&_nowSession);

		// Set session count
		++_sessionCnt;
		if (_sessionCnt > 0xFFFFFFFFFFFF)
			_sessionCnt = 1;

		// Alloc session
		UINT idx;
		while (!_freeSessionStack.Pop(&idx));
		InterlockedIncrement((PULONG)&sessionArr[idx].ioFlag.ioCount);
		sessionArr[idx].socket = client_sock;
		sessionArr[idx].relSocket = client_sock;
		sessionArr[idx].sessionID = (_sessionCnt << 16) | (UINT64)idx;
		sessionArr[idx].ioFlag.releaseFlag = 0;
		sessionArr[idx].sendFlag = 0;
		sessionArr[idx].sendCnt = 0;
		//sessionArr[idx].sendQ.ClearBuffer();
		sessionArr[idx].recvQ.ClearBuffer();

		if (CreateIoCompletionPort((HANDLE)client_sock, hCP, (ULONG_PTR)&sessionArr[idx], NULL) == NULL)
			return;

		OnClientJoin(sessionArr[idx].sessionID);

		// Recv Part
		ZeroMemory(&_sessionArr[idx].recvOverlapped, sizeof(WSAOVERLAPPED));
		WSABUF rcvbuf;
		rcvbuf.buf = _sessionArr[idx].recvQ.GetRearBufferPtr();
		rcvbuf.len = _sessionArr[idx].recvQ.DirectEnqueueSize();

		DWORD flags = 0;
		int rcvret = WSARecv(client_sock, &rcvbuf, 1, NULL, &flags, &_sessionArr[idx].recvOverlapped, NULL);
		if (rcvret == SOCKET_ERROR)
		{
			DWORD err = WSAGetLastError();
			switch (err)
			{
			case WSA_IO_PENDING:
				if (_sessionArr[idx].socket == INVALID_SOCKET)
				{
					/*
					WSASend의 인자로 INVALID_SOCKET이 되기 전 정상 소켓 디스크립터가 인자로
					복사되어 전달되어 WSASend 호출이 되었으나 이 때 다른 스레드의 Disconnect 호출 부에서
					INVALID_SOCKET으로 변경하고 CancelIOEx를 호출한 후 다시 WSASend 내부에서 Overlapped IO가
					동작한 경우 이번 IO에 대해서도 CancelIOEx를 한 번 더 걸어줘야 하기 때문에 추가된 부분
					*/
					CancelIoEx((HANDLE)_sessionArr[idx].relSocket, NULL);
				}
				break;
			case WSAENOTSOCK:
			case WSAECONNABORTED:
			case WSAECONNRESET:
				if (InterlockedDecrement((PULONG)&_sessionArr[idx].ioFlag.ioCount) == 0)
					ReleaseSession(&_sessionArr[idx]);
				break;
			default:
				printf("[Error] WSARecv error! [error code: %d]\n", err);
				if (InterlockedDecrement((PULONG)&_sessionArr[idx].ioFlag.ioCount) == 0)
					ReleaseSession(&_sessionArr[idx]);
				break;
			}
		}
		InterlockedIncrement((LONG*)&_recvCnt);
	}
}

unsigned WINAPI CNetServer::RunIOCPWorkerThread(LPVOID lpParam)
{
	CNetServer* server = (CNetServer*)lpParam;
	server->IOCPWorkerThread();
	return 0;
}

void CNetServer::IOCPWorkerThread()
{
	HANDLE hCP = _hCP;
	DWORD dwTransferred;
	SESSION* pSession;
	WSAOVERLAPPED* lpOverlapped;
	while (1)
	{
		lpOverlapped = NULL;
		pSession = NULL;
		dwTransferred = 0;
		GetQueuedCompletionStatus(hCP, &dwTransferred, (PULONG_PTR)&pSession, (LPOVERLAPPED*)&lpOverlapped, INFINITE);

		// End Thread
		if (dwTransferred == 0 && pSession == nullptr && lpOverlapped == nullptr)
			break;

		if (dwTransferred != 0 && pSession->socket != INVALID_SOCKET)
		{
			if (lpOverlapped == NULL)
			{
				switch (dwTransferred)
				{
				case en_IOCP_JOB_SENDPOST:
					SendPost(pSession);
					break;
				case en_IOCP_JOB_RELEASE:
					ReleaseSession(pSession);
					continue;
				default:
					break;
				}
			}
			else if (&pSession->sendOverlapped == lpOverlapped) // Send Logic
			{
				// Send Queue 정리 작업
				UINT idx;
				UINT bkSendCnt = pSession->sendCnt;
				pSession->sendCnt = 0;
				for (idx = 0; idx < bkSendCnt; idx++)
					CNetPacket::Free(pSession->sendPacketArr[idx]);

				InterlockedExchange((LONG*)&pSession->sendFlag, 0);
				SendPost(pSession);
			}
			else // Recv Logic
			{
				int retSize = pSession->recvQ.MoveRear(dwTransferred);
				if (retSize != dwTransferred)
					CrashDump::Crash();

				CNetPacket::NetHeader header;
				CNetPacket* pPacket;
				while (1)
				{
					if (pSession->recvQ.GetUseSize() < sizeof(CNetPacket::NetHeader))
						break;

					pSession->recvQ.Peek((char*)&header, sizeof(CNetPacket::NetHeader));

					if (header.code != dfPACKET_CODE)
					{
						Disconnect(pSession->sessionID);
						break;
					}

					if (pSession->recvQ.GetUseSize() < sizeof(CNetPacket::NetHeader) + header.len)
						break;

					pPacket = CNetPacket::Alloc();
					int deqSize = pSession->recvQ.Dequeue(pPacket->GetBufferPtr(), sizeof(CNetPacket::NetHeader) + header.len);
					if (deqSize != sizeof(CNetPacket::NetHeader) + header.len)	// 임시, 추후 삭제
						printf("[ERROR] Receive ring buffer에 문제가 발생했습니다.\n");

					pPacket->MoveWritePos(header.len);
					InterlockedIncrement((LONG*)&_recvCnt);

					if (!pPacket->Decode()) // Decode 실패 시
					{
						CNetPacket::Free(pPacket);
						Disconnect(pSession->sessionID);
						break;
					}

					// 컨텐츠 로직
					OnRecv(pSession->sessionID, pPacket);
					CNetPacket::Free(pPacket);
				}
				RecvPost(pSession);
			}
		}

		if (InterlockedDecrement((PULONG)&pSession->ioFlag.ioCount) == 0)
			ReleaseSession(pSession);
	}
}

void CNetServer::SendPost(SESSION* pSession)
{
	int cnt = 0;
	int useSize;

	while (1)
	{
		if (pSession->sendQ.GetUseSize() == 0)
			return;
		
		if (InterlockedExchange((PULONG)&pSession->sendFlag, 1) == 1)
			return;

		useSize = pSession->sendQ.GetUseSize();
		if (useSize == 0)
			pSession->sendFlag = 0;
		else
			break;

		++cnt;

		if (cnt == 2)
			return;
	}

	WSABUF sendbuf[MAXBUF];
	DWORD flags = 0;
	ZeroMemory(&pSession->sendOverlapped, sizeof(WSAOVERLAPPED));

	if (useSize > MAXBUF)
		useSize = MAXBUF;

	for (int i = 0; i < useSize; i++)
	{
		pSession->sendQ.Dequeue(&pSession->sendPacketArr[i]);
		sendbuf[i].buf = pSession->sendPacketArr[i]->GetBufferPtr();
		sendbuf[i].len = pSession->sendPacketArr[i]->GetTotalSize();
	}
	pSession->sendCnt = useSize;

	InterlockedIncrement((PULONG)&pSession->ioFlag.ioCount);
	if (WSASend(pSession->socket, sendbuf, useSize, NULL, flags, &pSession->sendOverlapped, NULL) == SOCKET_ERROR)
	{
		DWORD err = WSAGetLastError();
		switch (err)
		{
		case WSA_IO_PENDING:
			if (pSession->socket == INVALID_SOCKET)
			{
				/*
				WSASend의 인자로 INVALID_SOCKET이 되기 전 정상 소켓 디스크립터가 인자로
				복사되어 전달되어 WSASend 호출이 되었으나 이 때 다른 스레드의 Disconnect 호출 부에서
				INVALID_SOCKET으로 변경하고 CancelIOEx를 호출한 후 다시 WSASend 내부에서 Overlapped IO가
				동작한 경우 이번 IO에 대해서도 CancelIOEx를 한 번 더 걸어줘야 하기 때문에 추가된 부분
				*/
				CancelIoEx((HANDLE)pSession->relSocket, NULL);
			}
			break;
		case WSAENOTSOCK:
		case WSAECONNABORTED:
		case WSAECONNRESET:
			InterlockedDecrement((PULONG)&pSession->ioFlag.ioCount);
			break;
		default:
			// printf("[Error] WSASend error! [error code: %d]\n", err);
			InterlockedDecrement((PULONG)&pSession->ioFlag.ioCount);
			break;
		}
	}
}

void CNetServer::RecvPost(SESSION* pSession)
{
	WSABUF recvbuf[2];
	DWORD flags = 0;
	ZeroMemory(&pSession->recvOverlapped, sizeof(WSAOVERLAPPED));
	int freeSize = pSession->recvQ.GetFreeSize();
	recvbuf[0].buf = pSession->recvQ.GetRearBufferPtr();
	recvbuf[0].len = (ULONG)pSession->recvQ.DirectEnqueueSize();

	InterlockedIncrement((PULONG)&pSession->ioFlag.ioCount);
	if (freeSize <= recvbuf[0].len)
	{
		if (WSARecv(pSession->socket, recvbuf, 1, NULL, &flags, &pSession->recvOverlapped, NULL) == SOCKET_ERROR)
		{
			DWORD err = WSAGetLastError();
			switch (err)
			{
			case WSA_IO_PENDING:
				if (pSession->socket == INVALID_SOCKET)
				{
					/*
					WSASend의 인자로 INVALID_SOCKET이 되기 전 정상 소켓 디스크립터가 인자로
					복사되어 전달되어 WSASend 호출이 되었으나 이 때 다른 스레드의 Disconnect 호출 부에서
					INVALID_SOCKET으로 변경하고 CancelIOEx를 호출한 후 다시 WSASend 내부에서 Overlapped IO가
					동작한 경우 이번 IO에 대해서도 CancelIOEx를 한 번 더 걸어줘야 하기 때문에 추가된 부분
					*/
					CancelIoEx((HANDLE)pSession->relSocket, NULL);
				}
				break;
			case WSAENOTSOCK:
			case WSAECONNABORTED:
			case WSAECONNRESET:
				InterlockedDecrement((PULONG)&pSession->ioFlag.ioCount);
				break;
			default:
				// printf("[Error] WSARecv error! [error code: %d]\n", err); // 추후 에러코드 함수 호출
				InterlockedDecrement((PULONG)&pSession->ioFlag.ioCount);
				break;
			}
		}
	}
	else
	{
		recvbuf[1].buf = pSession->recvQ.GetBufferPtr();
		recvbuf[1].len = (ULONG)(pSession->recvQ.GetFrontBufferPtr() - pSession->recvQ.GetBufferPtr() - 1);
		if (WSARecv(pSession->socket, recvbuf, 2, NULL, &flags, &pSession->recvOverlapped, NULL) == SOCKET_ERROR)
		{
			DWORD err = WSAGetLastError();
			switch (err)
			{
			case WSA_IO_PENDING:
				if (pSession->socket == INVALID_SOCKET)
				{
					/*
					WSASend의 인자로 INVALID_SOCKET이 되기 전 정상 소켓 디스크립터가 인자로
					복사되어 전달되어 WSASend 호출이 되었으나 이 때 다른 스레드의 Disconnect 호출 부에서
					INVALID_SOCKET으로 변경하고 CancelIOEx를 호출한 후 다시 WSASend 내부에서 Overlapped IO가
					동작한 경우 이번 IO에 대해서도 CancelIOEx를 한 번 더 걸어줘야 하기 때문에 추가된 부분
					*/
					CancelIoEx((HANDLE)pSession->relSocket, NULL);
				}
				break;
			case WSAENOTSOCK:
			case WSAECONNABORTED:
			case WSAECONNRESET:
				InterlockedDecrement((PULONG)&pSession->ioFlag.ioCount);
				break;
			default:
				// printf("[Error] WSARecv error! [error code: %d]\n", err); // 추후 에러코드 함수 호출
				InterlockedDecrement((PULONG)&pSession->ioFlag.ioCount);
				break;
			}
		}
	}
}

bool CNetServer::SendPacket(unsigned __int64 sessionID, CNetPacket* pPacket)
{
	SESSION* pSession = &_sessionArr[(USHORT)sessionID];

	if (!EnterSession(pSession, sessionID))
		return false;

	InterlockedIncrement((LONG*)&_sendCnt);
	pPacket->AddRefCount(); // Ref count 두는 이유? -> Broadcast 대응 위함
	pPacket->Encode();
	pSession->sendQ.Enqueue(pPacket);
	
	if (!PostQueuedCompletionStatus(_hCP, en_IOCP_JOB_SENDPOST, (ULONG_PTR)pSession, NULL))
		LeaveSession(pSession);

	return true;
}

void CNetServer::RequestRelease(SESSION* pSession)
{
	PostQueuedCompletionStatus(_hCP, en_IOCP_JOB_RELEASE, (ULONG_PTR)pSession, NULL);
}

void CNetServer::ReleaseSession(SESSION* pSession)
{
	if (InterlockedCompareExchange64((PLONG64)&pSession->ioFlag, 0x100000000, 0) != 0)
		return;

	int retval;
	CNetPacket* pPacket;
	while (1)
	{
		if (pSession->sendQ.Dequeue(&pPacket) == false)
			break;

		CNetPacket::Free(pPacket);
	}

	UINT bkCnt = pSession->sendCnt;
	for (int i = 0; i < bkCnt; i++)
		CNetPacket::Free(pSession->sendPacketArr[i]);

	closesocket(pSession->relSocket);
	OnClientLeave(pSession->sessionID);
	_freeSessionStack.Push((USHORT)pSession->sessionID);
	InterlockedDecrement((LONG*)&_nowSession);
}

bool CNetServer::Disconnect(unsigned __int64 sessionID)
{
	SESSION* pSession = &_sessionArr[(USHORT)sessionID];

	if (!EnterSession(pSession, sessionID))
		return false;

	if (InterlockedExchange((PLONG)&pSession->socket, INVALID_SOCKET) == INVALID_SOCKET) // 추가 I/O 방지
	{
		LeaveSession(pSession);
		return false;
	}

	CancelIoEx((HANDLE)pSession->relSocket, NULL);
	LeaveSession(pSession);
	return true;
}

bool CNetServer::EnterSession(SESSION* pSession, unsigned __int64 sessionID)
{
	InterlockedIncrement((PULONG)&pSession->ioFlag.ioCount);
	if (pSession->ioFlag.releaseFlag == 1 || pSession->sessionID != sessionID)
	{
		LeaveSession(pSession);
		return false;
	}
	return true;
}

void CNetServer::LeaveSession(SESSION* pSession)
{
	if (InterlockedDecrement((PULONG)&pSession->ioFlag.ioCount) == 0)
		RequestRelease(pSession);
}

void CNetServer::InitSettings()
{
	_listen_sock = INVALID_SOCKET;
	_maxUser = 0;
	_nowSession = 0;
	_sessionCnt = 0;
	_nagleOff = false;
	_sessionArr = nullptr;
	_hAcceptThread = NULL;
	_hMonitorThread = NULL;
	_hThreadArr = nullptr;
	_hCP = NULL;
	_acceptCnt = 0;
	_sendCnt = 0;
	_recvCnt = 0;
	_acceptTPS = 0;
	_sendTPS = 0;
	_recvTPS = 0;
	_workerThreadCnt = 0;
	_monitorSwitch = false;
	_serverOn = false;
}

int CNetServer::getAcceptTPS()
{
	return _acceptTPS;
}

int CNetServer::getRecvMessageTPS()
{
	return _recvTPS;
}

int CNetServer::getSendMessageTPS()
{
	return _sendTPS;
}

int CNetServer::GetSessionCount()
{
	return _nowSession;
}
