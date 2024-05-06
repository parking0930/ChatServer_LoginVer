#pragma once
#include <Windows.h>
#include "MemoryPoolTLS.h"

template <typename T>
class CLockFreeQueue
{
public:
	CLockFreeQueue();
	CLockFreeQueue(int initBlock, int maxBlock);
	~CLockFreeQueue();
	void Enqueue(T data);
	bool Dequeue(T* data);
	int GetUseSize();
private:
	struct Node
	{
		T		data;
		Node*	next;
	};
	static Node* Alloc();
	static void Free(Node* pNode);
private:
	volatile Node*	_head;
	volatile Node*	_tail;
	Node		_dummy;
	long		_size;
	volatile ULONGLONG	_myCode;
	static ULONGLONG				uniqueCode;
	static MemoryPoolTLS<Node>		poolManager;
	static BOOL						initLock;
};

template <typename T>
ULONGLONG CLockFreeQueue<T>::uniqueCode;

template <typename T>
MemoryPoolTLS<typename CLockFreeQueue<T>::Node> CLockFreeQueue<T>::poolManager(100000, POOL_MAX_ALLOC, false);

template <typename T>
BOOL CLockFreeQueue<T>::initLock;

template <typename T>
CLockFreeQueue<T>::CLockFreeQueue()
{
	_myCode = InterlockedDecrement(&uniqueCode);
	_size = 0;
	_head = _tail = &_dummy;
	_head->next = (Node*)_myCode;
}

template <typename T>
CLockFreeQueue<T>::CLockFreeQueue(int initBlock, int maxBlock)
{
	_myCode = InterlockedDecrement(&uniqueCode);
	_size = 0;
	_head = _tail = _dummy = CLockFreeQueue<T>::Alloc();
	_head->next = (Node*)_myCode;
}

template <typename T>
CLockFreeQueue<T>::~CLockFreeQueue()
{

}

template <typename T>
typename CLockFreeQueue<T>::Node* CLockFreeQueue<T>::Alloc()
{
	return poolManager.Alloc();
}

template <typename T>
void CLockFreeQueue<T>::Free(Node* pNode)
{
	poolManager.Free(pNode);
}

template <typename T>
void CLockFreeQueue<T>::Enqueue(T data)
{
	Node* newNode = CLockFreeQueue<T>::Alloc();
	newNode->data = data;
	newNode->next = (Node*)_myCode;
	volatile Node* bkTail;
	volatile Node* bkRealTail;
	volatile Node* bkNext;
	volatile UINT64 idx;
	while (1)
	{
		bkTail = _tail;
		bkRealTail = (Node*)((UINT64)bkTail & 0x00007FFFFFFFFFFF);
		bkNext = bkRealTail->next;
		idx = ((UINT64)bkTail >> 47) + 1;

		if (bkNext == (Node*)_myCode)
		{
			if (InterlockedCompareExchange64((PLONG64)&bkRealTail->next, (LONG64)newNode, (LONG64)_myCode) == (LONG64)_myCode)
				break;
		}
		else
		{
			InterlockedCompareExchange64((PLONG64)&_tail, (LONG64)((UINT64)bkNext | ((UINT64)idx << 47)), (LONG64)bkTail);
		}
	}

	InterlockedCompareExchange64((PLONG64)&_tail, (LONG64)((UINT64)newNode | ((UINT64)idx << 47)), (LONG64)bkTail);
	InterlockedIncrement(&_size);
}

template <typename T>
bool CLockFreeQueue<T>::Dequeue(T* data)
{
	if (InterlockedDecrement(&_size) < 0)
	{
		InterlockedIncrement(&_size);
		return false;
	}

	while (1)
	{
		volatile Node* bkHead = _head;
		Node* bkRealHead = (Node*)((UINT64)bkHead & 0x00007FFFFFFFFFFF);
		volatile Node* bkNext = bkRealHead->next;
		volatile UINT64 idx = ((UINT64)bkHead >> 47) + 1;

		if (bkNext == (Node*)_myCode)
		{
			if (bkHead == _head)
				return false;
			continue;
		}

		volatile Node* bkTail = _tail;
		volatile Node* bkRealTail = (Node*)((UINT64)bkTail & 0x00007FFFFFFFFFFF);
		volatile Node* bkTailNext = bkRealTail->next;
		volatile UINT64 tailIdx = ((UINT64)bkTail >> 47) + 1;

		if (bkTailNext != (Node*)_myCode)
		{
			InterlockedCompareExchange64((PLONG64)&_tail, (LONG64)((UINT64)bkTailNext | ((UINT64)tailIdx << 47)), (LONG64)bkTail);
			continue;
		}

		*data = bkNext->data;
		if (InterlockedCompareExchange64((PLONG64)&_head, (LONG64)((UINT64)bkNext | ((UINT64)idx << 47)), (LONG64)bkHead) == (LONG64)bkHead)
		{
			CLockFreeQueue<T>::Free(bkRealHead);
			break;
		}
	}
	return true;
}

template <typename T>
int CLockFreeQueue<T>::GetUseSize()
{
	return _size;
}