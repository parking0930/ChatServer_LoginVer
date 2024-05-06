#pragma once
#include "MemoryPoolTLS.h"
#define	DEFAULT_SIZE		1024
#define dfPACKET_CODE		0x77
#define dfPACKET_KEY		0x32

class CNetPacket
{
public:
	void	Release(void);
	void	Clear(void);
	int		GetBufferSize(void);
	int		GetPayloadSize(void);
	int		GetTotalSize(void);
	char*	GetBufferPtr(void);
	char*	GetPayloadPtr(void);
	int		MoveWritePos(int size);
	int		MoveReadPos(int size);
	int		GetData(char* pDest, int size);
	int		SetData(char* pSrc, int srcSize);
	void	GetNetHeader(char* pDest);
	void	SetNetHeader(char* pSrc);
	void	AddRefCount();
	bool	Encode();
	bool	Decode();

	CNetPacket& operator<< (char val);
	CNetPacket& operator<< (unsigned char val);

	CNetPacket& operator<< (short val);
	CNetPacket& operator<< (unsigned short val);

	CNetPacket& operator<< (int val);
	CNetPacket& operator<< (unsigned int val);

	CNetPacket& operator<< (long val);
	CNetPacket& operator<< (unsigned long val);

	CNetPacket& operator<< (__int64 val);
	CNetPacket& operator<< (unsigned __int64 val);

	CNetPacket& operator<< (float val);
	CNetPacket& operator<< (double val);
	////////////////////////////////////////////////
	CNetPacket& operator>> (char& val);
	CNetPacket& operator>> (unsigned char& val);

	CNetPacket& operator>> (short& val);
	CNetPacket& operator>> (unsigned short& val);

	CNetPacket& operator>> (int& val);
	CNetPacket& operator>> (unsigned int& val);

	CNetPacket& operator>> (long val);
	CNetPacket& operator>> (unsigned long& val);

	CNetPacket& operator>> (__int64& val);
	CNetPacket& operator>> (unsigned __int64& val);

	CNetPacket& operator>> (float& val);
	CNetPacket& operator>> (double& val);
private:
	CNetPacket();
	CNetPacket(int size);
	CNetPacket(const CNetPacket& packet) = delete;
	CNetPacket& operator = (const CNetPacket& packet) = delete;
	virtual	~CNetPacket();
public:
	static CNetPacket*	Alloc();
	static void			Free(CNetPacket* pPacket);
private:
#pragma pack(1)
	struct NetHeader
	{
		BYTE	code;
		USHORT	len;
		BYTE	randKey;
		BYTE	chkSum;
	};
#pragma pack()
private:
	friend class CNetServer;
	friend class MemoryPoolTLS<CNetPacket>;
	static MemoryPoolTLS<CNetPacket> _packetpool;
protected:
	char*		_pBuffer;
	char*		_pWritePos;
	char*		_pReadPos;
	int			_refCount;
	bool		_isEncode;
	const int	_cBufferSize;
};