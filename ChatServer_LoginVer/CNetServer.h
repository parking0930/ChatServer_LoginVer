#pragma once
#include <WinSock2.h>
#include "CRingBuffer.h"
#include "CLockFreeStack.h"
#include "CLockFreeQueue.h"
#include "CNetPacket.h"
#define	MAXBUF	200
//#define SEND_VER1

class CNetServer
{
private:
	struct SESSION
	{
		UINT64 sessionID;
		SOCKET socket;
		SOCKET relSocket;
		struct IO_FLAG
		{
			UINT ioCount;
			UINT releaseFlag;
		}ioFlag;
		UINT sendFlag;
		UINT sendCnt; // CPacket 개수 체크용
		CLockFreeQueue<CNetPacket*> sendQ;
		CNetPacket* sendPacketArr[MAXBUF];
		CRingBuffer recvQ;
		WSAOVERLAPPED sendOverlapped;
		WSAOVERLAPPED recvOverlapped;
	};
	enum IOCP_JOB
	{
		en_IOCP_JOB_SENDPOST = 1,
		en_IOCP_JOB_RELEASE,
	};
public:
	CNetServer();
	virtual ~CNetServer();
	bool Start(const wchar_t* ip, unsigned short port, int workerThreadCnt, int runningThreadCnt, bool nagleOff, int maxUser);
	void Stop();
	int GetSessionCount();
	bool Disconnect(unsigned __int64 sessionID);
	bool SendPacket(unsigned __int64 sessionID, CNetPacket* pPacket);
	int getAcceptTPS();
	int getRecvMessageTPS();
	int getSendMessageTPS();
private:
	virtual bool OnConnectionRequest(wchar_t* ip, unsigned short port) = 0;
	virtual void OnClientJoin(unsigned __int64 sessionID) = 0; // < Accept 후 접속처리 완료 후 호출.
	virtual void OnClientLeave(unsigned __int64 sessionID) = 0; //< Release 후 호출
	virtual void OnRecv(unsigned __int64 sessionID, CNetPacket* pPacket) = 0; //< 패킷 수신 완료 후
	virtual void OnError(int errorcode, wchar_t* str) = 0; // 라이브러리화 시 Error코드 문서화할 예정
	virtual void OnStart() {}
	virtual void OnStop() {}
	void SendPost(SESSION* pSession);
	void RecvPost(SESSION* pSession);
	void RequestRelease(SESSION* pSession);
	void ReleaseSession(SESSION* pSession);
	bool EnterSession(SESSION* pSession, unsigned __int64 sessionID);
	void LeaveSession(SESSION* pSession);
	void InitSettings();
	void IOCPWorkerThread();
	void AcceptThread();
	void MonitorThread();
	static unsigned WINAPI RunMonitorThread(LPVOID lpParam);
	static unsigned WINAPI RunAcceptThread(LPVOID lpParam);
	static unsigned WINAPI RunIOCPWorkerThread(LPVOID lpParam);
private:
	SOCKET						_listen_sock;
	UINT						_maxUser;
	UINT						_nowSession;
	UINT64						_sessionCnt;
	SESSION* _sessionArr;
	HANDLE						_hAcceptThread;
	HANDLE						_hMonitorThread;
	HANDLE* _hThreadArr;
	HANDLE						_hCP;
	CLockFreeStack<UINT>		_freeSessionStack;
	bool						_nagleOff;
	unsigned int				_acceptCnt;
	unsigned int				_sendCnt;
	unsigned int				_recvCnt;
	unsigned int				_acceptTPS;
	unsigned int				_sendTPS;
	unsigned int				_recvTPS;
	int							_workerThreadCnt;
	bool						_monitorSwitch;
	bool						_serverOn;
};