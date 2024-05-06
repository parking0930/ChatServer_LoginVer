#pragma once
#define DEFAULT_BUFSIZE 10000
class CRingBuffer
{
public:
	CRingBuffer(void);
	CRingBuffer(int iBufferSize);
	~CRingBuffer();
	void Resize(int iSize);
	int	GetBufferSize(void);
	int	GetUseSize(void);
	int	GetFreeSize(void);
	int	DirectEnqueueSize(void);
	int	DirectDequeueSize(void);
	int	Enqueue(const char* chpData, int iSize);
	int	Dequeue(char* chpDest, int iSize);
	int	Peek(char* chpDest, int iSize);
	int	MoveRear(int iSize);
	int	MoveFront(int iSize);
	void ClearBuffer(void);
	char* GetFrontBufferPtr(void);
	char* GetRearBufferPtr(void);
	char* GetBufferPtr(void);
private:
	char* buffer;
	int iFront;
	int iRear;
	int iTotalSize;
};