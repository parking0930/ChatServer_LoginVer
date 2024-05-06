#include "ChatServer.h"

int wmain()
{
	ChatServer* server= new ChatServer();
	bool svStatus = server->Start(NULL, 20000, 4, 10, false, 20000);
	// Logic
	while (svStatus)
	{
		wprintf(L"Now connect session: %u\n", server->GetSessionCount());
		wprintf(L"Connect/Login: %d/%d\n", server->GetWaitCharactorCount(), server->GetLoginCharacterCount());
		wprintf(L"AcceptTotal: %d\n", server->GetAcceptTotal());
		wprintf(L"Accept TPS: %d\n", server->getAcceptTPS());
		wprintf(L"SendPacket TPS: %d\n", server->getSendMessageTPS());
		wprintf(L"RecvPacket TPS: %d\n", server->getRecvMessageTPS());
		printf("\n");
		Sleep(1000);
	}

	server->Stop();
	return 0;
}