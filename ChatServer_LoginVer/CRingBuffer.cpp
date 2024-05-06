#include "CRingBuffer.h"
#include <string.h>
#include <stdio.h>

CRingBuffer::CRingBuffer() :iFront(0), iRear(0), iTotalSize(DEFAULT_BUFSIZE + 1)
{
	buffer = new char[DEFAULT_BUFSIZE + 1];
}

CRingBuffer::CRingBuffer(int iBufferSize) :iFront(0), iRear(0), iTotalSize(iBufferSize + 1)
{
	buffer = new char[iBufferSize + 1];
}

CRingBuffer::~CRingBuffer()
{
	delete[] buffer;
}

void CRingBuffer::Resize(int iSize)
{
	if (iSize <= iTotalSize)
		return;
	char* newbuffer = new char[iSize + 1];
	memcpy_s(newbuffer, iSize + 1, buffer, iTotalSize);
	delete[] buffer;
	buffer = newbuffer;
	iTotalSize = iSize + 1;
}

int	CRingBuffer::GetBufferSize()
{
	return iTotalSize - 1;
}


/////////////////////////////////////////////////////////////////////////
// 현재 사용중인 용량 얻기.
//
// Parameters: 없음.
// Return: (int)사용중인 용량.
/////////////////////////////////////////////////////////////////////////
int	CRingBuffer::GetUseSize(void)
{
	int iBkupFront = iFront;
	int iBkupRear = iRear;

	if (iBkupFront == iBkupRear)
		return 0;
	if (iBkupFront < iBkupRear)
		return iBkupRear - iBkupFront;
	if (iBkupFront > iBkupRear)
		return iTotalSize - iBkupFront + iRear;
}

/////////////////////////////////////////////////////////////////////////
// 현재 버퍼에 남은 용량 얻기. 
//
// Parameters: 없음.
// Return: (int)남은용량.
/////////////////////////////////////////////////////////////////////////
int	CRingBuffer::GetFreeSize(void)
{
	int iBkupFront = iFront;
	int iBkupRear = iRear;

	if (iBkupFront == iBkupRear)
		return iTotalSize - 1;
	if (iBkupFront > iBkupRear)
		return iBkupFront - iBkupRear - 1;
	if (iBkupFront < iBkupRear)
		return iTotalSize - 1 - iBkupRear + iBkupFront;
}

/////////////////////////////////////////////////////////////////////////
// 버퍼 포인터로 외부에서 한방에 읽고, 쓸 수 있는 길이.
// (끊기지 않은 길이)
//
// 원형 큐의 구조상 버퍼의 끝단에 있는 데이터는 끝 -> 처음으로 돌아가서
// 2번에 데이터를 얻거나 넣을 수 있음. 이 부분에서 끊어지지 않은 길이를 의미
//
// Parameters: 없음.
// Return: (int)사용가능 용량.
////////////////////////////////////////////////////////////////////////
int	CRingBuffer::DirectEnqueueSize(void)
{
	int iBkupFront = iFront;
	int iBkupRear = (iRear + 1) % iTotalSize;

	if (iBkupFront < iBkupRear)
		return iTotalSize - iBkupRear;
	if (iBkupFront >= iBkupRear)
		return iBkupFront - iBkupRear;
}

int	CRingBuffer::DirectDequeueSize(void)
{
	int iBkupFront = iFront;
	int iBkupRear = iRear;

	if (iBkupFront == iBkupRear)
		return 0;

	iBkupFront = (iBkupFront + 1) % iTotalSize;

	if (iBkupFront > iBkupRear)
		return iTotalSize - iBkupFront;
	if (iBkupFront <= iBkupRear)
		return iBkupRear - iBkupFront + 1;
}


/////////////////////////////////////////////////////////////////////////
// WritePos 에 데이타 넣음.
//
// Parameters: (char *)데이타 포인터. (int)크기. 
// Return: (int)넣은 크기.
/////////////////////////////////////////////////////////////////////////
int	CRingBuffer::Enqueue(const char* chpData, int iSize)
{
	int iBkupRear = (iRear + 1) % iTotalSize;
	int iBkupFront = iFront;
	int iEnqSize = 0;

	// 1차 Direct Enqueue Size 측정
	int iDEsize;
	if (iBkupFront < iBkupRear)
		iDEsize = iTotalSize - iBkupRear;
	else if (iBkupFront > iBkupRear)
		iDEsize = iBkupFront - iBkupRear;
	else // queue full
		return 0;

	// 한 번의 Enqueue로 끝낼 수 있는지 판단
	if (iSize > iDEsize)
	{
		iEnqSize = iDEsize;

		// 1차 Enqueue
		memcpy_s(buffer + iBkupRear, iEnqSize, chpData, iEnqSize);
		iBkupRear = iBkupRear + (iEnqSize - 1);

		int tmpRear = (iBkupRear + 1) % iTotalSize;
		// Enqueue가 다 마무리되지 못한 경우(버퍼 꽉차서 일부만 넣은 경우이다.)
		if (tmpRear == iBkupFront)
		{
			iRear = iBkupRear;
			return iEnqSize;
		}

		// 2차 Enqueue
		iBkupRear = tmpRear;
		iSize -= iEnqSize;

		if (iBkupFront < iBkupRear)
			iDEsize = iTotalSize - iBkupRear;
		else if (iBkupFront > iBkupRear)
			iDEsize = iBkupFront - iBkupRear;

		// Enqueue가 다 마무리되지 못한 경우(버퍼 꽉차서 일부만 넣은 경우이다.)
		if (iSize > iDEsize)
			iSize = iDEsize;

		// 2차 Enqueue
		memcpy_s(buffer + iBkupRear, iSize, chpData + iEnqSize, iSize);
		iRear = iBkupRear + (iSize - 1);
		iEnqSize += iSize;
		return iEnqSize;
	}
	else
	{
		iEnqSize = iSize;

		// Enqueue
		memcpy_s(buffer + iBkupRear, iEnqSize, chpData, iEnqSize);
		iRear = iBkupRear + (iEnqSize - 1);
		return iEnqSize;
	}
}

/////////////////////////////////////////////////////////////////////////
// ReadPos 에서 데이타 가져옴. ReadPos 이동.
//
// Parameters: (char *)데이타 포인터. (int)크기.
// Return: (int)가져온 크기.
/////////////////////////////////////////////////////////////////////////
int	CRingBuffer::Dequeue(char* chpDest, int iSize)
{
	int iBkupRear = iRear;
	int iBkupFront = iFront;
	int iDqSize = 0;

	// Queue empty case
	if (iBkupRear == iBkupFront)
		return 0;

	iBkupFront = (iBkupFront + 1) % iTotalSize;

	// 1차 Direct dequeue size 측정
	int iDDEsize;
	if (iBkupFront > iBkupRear)
		iDDEsize = iTotalSize - iBkupFront;
	else // if (iBkupFront <= iBkupRear)
		iDDEsize = iBkupRear - iBkupFront + 1;

	// 한 번의 Dequeue로 끝낼 수 있는지 판단
	if (iSize > iDDEsize)
	{
		iDqSize = iDDEsize;

		// 1차 Dequeue
		memcpy_s(chpDest, iDqSize, buffer + iBkupFront, iDqSize);
		iBkupFront = iBkupFront + iDqSize - 1;

		// 더 이상 Dequeue할 수 없는 경우(요청 크기보다 들고있는 게 적을 경우)
		if (iBkupRear == iBkupFront)
		{
			iFront = iBkupFront;
			return iDqSize;
		}

		// 2차 Dequeue 시작
		iBkupFront = (iBkupFront + 1) % iTotalSize;
		iSize -= iDqSize;

		if (iBkupFront > iBkupRear)
			iDDEsize = iTotalSize - iBkupFront;
		else // if (iBkupFront <= iBkupRear)
			iDDEsize = iBkupRear - iBkupFront + 1;


		// 더 이상 Dequeue할 수 없는 경우(요청 크기보다 들고있는 게 적을 경우)
		if (iSize > iDDEsize)
			iSize = iDDEsize;

		// 2차 Dequeue
		memcpy_s(chpDest + iDqSize, iSize, buffer + iBkupFront, iSize);
		iFront = iBkupFront + iSize - 1;
		iDqSize += iSize;
		return iDqSize;
	}
	else
	{
		iDqSize = iSize;

		memcpy_s(chpDest, iDqSize, buffer + iBkupFront, iDqSize);
		iBkupFront = iBkupFront + iDqSize - 1;

		iFront = iBkupFront;
		return iDqSize;
	}
}

/////////////////////////////////////////////////////////////////////////
// ReadPos 에서 데이타 읽어옴. ReadPos 고정.
//
// Parameters: (char *)데이타 포인터. (int)크기.
// Return: (int)가져온 크기.
/////////////////////////////////////////////////////////////////////////
int	CRingBuffer::Peek(char* chpDest, int iSize)
{
	int iBkupRear = iRear;
	int iBkupFront = iFront;
	int iDqSize = 0;

	// Queue empty case
	if (iBkupRear == iBkupFront)
		return 0;

	iBkupFront = (iBkupFront + 1) % iTotalSize;

	// 1차 Direct dequeue size 측정
	int iDDEsize;
	if (iBkupFront > iBkupRear)
		iDDEsize = iTotalSize - iBkupFront;
	else // if (iBkupFront <= iBkupRear)
		iDDEsize = iBkupRear - iBkupFront + 1;

	// 한 번의 Dequeue로 끝낼 수 있는지 판단
	if (iSize > iDDEsize)
	{
		iDqSize = iDDEsize;

		// 1차 Dequeue
		memcpy_s(chpDest, iDqSize, buffer + iBkupFront, iDqSize);
		iBkupFront = iBkupFront + iDqSize - 1;

		// 더 이상 Dequeue할 수 없는 경우(요청 크기보다 들고있는 게 적을 경우)
		if (iBkupRear == iBkupFront)
		{
			return iDqSize;
		}

		// 2차 Dequeue 시작
		iBkupFront = (iBkupFront + 1) % iTotalSize;
		iSize -= iDqSize;

		if (iBkupFront > iBkupRear)
			iDDEsize = iTotalSize - iBkupFront;
		else // if (iBkupFront <= iBkupRear)
			iDDEsize = iBkupRear - iBkupFront + 1;


		// 더 이상 Dequeue할 수 없는 경우(요청 크기보다 들고있는 게 적을 경우)
		if (iSize > iDDEsize)
			iSize = iDDEsize;

		// 2차 Dequeue
		memcpy_s(chpDest + iDqSize, iSize, buffer + iBkupFront, iSize);
		iDqSize += iSize;
		return iDqSize;
	}
	else
	{
		iDqSize = iSize;

		memcpy_s(chpDest, iDqSize, buffer + iBkupFront, iDqSize);
		iBkupFront = iBkupFront + iDqSize - 1;
		return iDqSize;
	}
}


/////////////////////////////////////////////////////////////////////////
// 원하는 길이만큼 읽기위치 에서 삭제 / 쓰기 위치 이동
//
// Parameters: 없음.
// Return: (int)이동크기
/////////////////////////////////////////////////////////////////////////
int	CRingBuffer::MoveRear(int iSize)
{
	int iBkupRear = (iRear + 1) % iTotalSize;
	int iBkupFront = iFront;
	int iEnqSize = 0;

	// 1차 Direct Enqueue Size 측정
	int iDEsize;
	if (iBkupFront < iBkupRear)
		iDEsize = iTotalSize - iBkupRear;
	else if (iBkupFront > iBkupRear)
		iDEsize = iBkupFront - iBkupRear;
	else // queue full
		return 0;

	// 한 번의 Enqueue로 끝낼 수 있는지 판단
	if (iSize > iDEsize)
	{
		iEnqSize = iDEsize;

		// 1차 Enqueue
		iBkupRear = iBkupRear + (iEnqSize - 1);

		int tmpRear = (iBkupRear + 1) % iTotalSize;
		// Enqueue가 다 마무리되지 못한 경우(버퍼 꽉차서 일부만 넣은 경우이다.)
		if (tmpRear == iBkupFront)
		{
			iRear = iBkupRear;
			return iEnqSize;
		}

		// 2차 Enqueue
		iBkupRear = tmpRear;
		iSize -= iEnqSize;

		if (iBkupFront < iBkupRear)
			iDEsize = iTotalSize - iBkupRear;
		else if (iBkupFront > iBkupRear)
			iDEsize = iBkupFront - iBkupRear;

		// Enqueue가 다 마무리되지 못한 경우(버퍼 꽉차서 일부만 넣은 경우이다.)
		if (iSize > iDEsize)
			iSize = iDEsize;

		// 2차 Enqueue
		iRear = iBkupRear + (iSize - 1);
		iEnqSize += iSize;
		return iEnqSize;
	}
	else
	{
		iEnqSize = iSize;

		// Enqueue
		iRear = iBkupRear + (iEnqSize - 1);
		return iEnqSize;
	}
}

int	CRingBuffer::MoveFront(int iSize)
{
	int iBkupRear = iRear;
	int iBkupFront = iFront;
	int iDqSize = 0;

	// Queue empty case
	if (iBkupRear == iBkupFront)
		return 0;

	iBkupFront = (iBkupFront + 1) % iTotalSize;

	// 1차 Direct dequeue size 측정
	int iDDEsize;
	if (iBkupFront > iBkupRear)
		iDDEsize = iTotalSize - iBkupFront;
	else // if (iBkupFront <= iBkupRear)
		iDDEsize = iBkupRear - iBkupFront + 1;

	// 한 번의 Dequeue로 끝낼 수 있는지 판단
	if (iSize > iDDEsize)
	{
		iDqSize = iDDEsize;

		// 1차 Dequeue
		iBkupFront = iBkupFront + iDqSize - 1;

		// 더 이상 Dequeue할 수 없는 경우(요청 크기보다 들고있는 게 적을 경우)
		if (iBkupRear == iBkupFront)
		{
			iFront = iBkupFront;
			return iDqSize;
		}

		// 2차 Dequeue 시작
		iBkupFront = (iBkupFront + 1) % iTotalSize;
		iSize -= iDqSize;

		if (iBkupFront > iBkupRear)
			iDDEsize = iTotalSize - iBkupFront;
		else // if (iBkupFront <= iBkupRear)
			iDDEsize = iBkupRear - iBkupFront + 1;


		// 더 이상 Dequeue할 수 없는 경우(요청 크기보다 들고있는 게 적을 경우)
		if (iSize > iDDEsize)
			iSize = iDDEsize;

		// 2차 Dequeue
		iFront = iBkupFront + iSize - 1;
		iDqSize += iSize;
		return iDqSize;
	}
	else
	{
		iDqSize = iSize;

		iBkupFront = iBkupFront + iDqSize - 1;
		iFront = iBkupFront;
		return iDqSize;
	}
}

/////////////////////////////////////////////////////////////////////////
// 버퍼의 모든 데이타 삭제.
//
// Parameters: 없음.
// Return: 없음.
/////////////////////////////////////////////////////////////////////////
void CRingBuffer::ClearBuffer(void)
{
	iRear = iFront = 0;
}


/////////////////////////////////////////////////////////////////////////
// 버퍼의 Front 포인터 얻음.
//
// Parameters: 없음.
// Return: (char *) 버퍼 포인터.
/////////////////////////////////////////////////////////////////////////
char* CRingBuffer::GetFrontBufferPtr(void)
{
	return buffer + ((iFront + 1) % iTotalSize);
}


/////////////////////////////////////////////////////////////////////////
// 버퍼의 RearPos 포인터 얻음.
//
// Parameters: 없음.
// Return: (char *) 버퍼 포인터.
/////////////////////////////////////////////////////////////////////////
char* CRingBuffer::GetRearBufferPtr(void)
{
	return buffer + ((iRear + 1) % iTotalSize);
}

char* CRingBuffer::GetBufferPtr(void)
{
	return buffer;
}