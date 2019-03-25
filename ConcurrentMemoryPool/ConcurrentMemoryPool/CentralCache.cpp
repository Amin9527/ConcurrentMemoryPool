#include"CentralCache.h"
#include"PageCache.h"

CentralCache CentralCache::_inst;

//��ȡһ��span
Span* CentralCache::GetOneSpan(SpanList* spanlist, size_t bytes)
{
	Span* span = spanlist->begin();//��ȡ��ͷ˫��ѭ������ͷ������һ���ڵ�
	while (span != spanlist->end())//ѭ����һ���ж����span
	{
		if (span->_objlist != nullptr)
			return span;
		span = span->_next;
	}

	//��CentralCache��û���ҵ�span����pagecache����һ���µĺ��ʴ�С��span
	size_t npage = ClassSize::NumMovePage(bytes);
	Span* newspan = PageCache::GetInstance()->NewSpan(npage);

	//��newspan���ڴ��и��һ����bytes��С�Ķ��������
	char* start = (char*)(newspan->_pageid << PAGE_SHIFT);  //��ȡ�����span�Ŀ�ʼ�ĵ�ַ
	char* end = start + (newspan->_npage << PAGE_SHIFT);   //��ȡ�����span�Ľ�����λ��
	char* cur = start;
	char* next = start+bytes;
	while (next < end)  //�������span����byte��С�ֿ���ͨ��ָ��������
	{
		NEXT_OBJ(cur) = next; //��ǰһ��byte�ڴ��ĺ��ĸ��ֽ���������ָ��ָ���һ��byte��С���ڴ��
		cur = next;
		next += bytes;
	}
	NEXT_OBJ(cur) = nullptr;
	newspan->_objlist = start;
	newspan->_objsize = bytes;
	newspan->_usecount = 0;

	//��newspan���뵽SpanList��
	spanlist->PushFront(newspan);
	return newspan;
}

//�����Ļ�ȡһ�������Ķ����threadcache
size_t CentralCache::FetchRangeObj(void*& start, void*& end, size_t num, size_t bytes)
{
	size_t index = ClassSize::Index(bytes);//�ҵ��ô�С�ڴ��Ӧ����������λ�ñ�ʶ
	SpanList* spanlist = &_spanlist[index];//�ҵ��ô�С�ڴ��Ӧ����������λ��

	//�Ե�ǰͰ���м���
	std::unique_lock<std::mutex> lock(spanlist->_mtx);

	//��ȡһ��span
	Span* span = GetOneSpan(spanlist, bytes);
	void* cur = span->_objlist;
	void* prev = cur;
	size_t fetchnum = 0;
	while (cur != nullptr && fetchnum < num)//�ڶ�Ӧ��span���Ҷ���������ȡnum������
	{                                       //�������num��������ȫ��ȡ��
		prev = cur;
		cur = NEXT_OBJ(cur);
		++fetchnum;
	}

	NEXT_OBJ(prev) = nullptr;//��ȡ�������һ�������next�ƿ�
	start = span->_objlist;
	end = prev;
	span->_objlist = cur;//span��_objlistָ��ʣ�����
	span->_usecount += fetchnum;//ʹ�ü�������
	
	return fetchnum;
}

//��һ�������Ķ����ͷŻ�span
void CentralCache::ReleaseListToSpan(void* start, size_t bytes)
{
	size_t index = ClassSize::Index(bytes);
	SpanList* spanlist = &_spanlist[index];

	//����
	std::unique_lock<std::mutex> lock(spanlist->_mtx);

	while (start)
	{
		void* next = NEXT_OBJ(start);
		Span* span = PageCache::GetInstance()->MapObjectToSpan(start);  //�ҵ�start�ڴ���Ӧ��ҳ��ӳ���span
		NEXT_OBJ(start) = span->_objlist; //��start�ڴ��ҵ���Ӧ��span��
		span->_objlist = start;

		//usecount==0 ��ʾspan�г��Ķ���ȫ���ջ�
		//�ͷ�span�ص�PageCache���кϲ�
		if (--span->_usecount == 0)
		{
			spanlist->Erase(span); //��span��spanlist����ȥ��

			span->_next = nullptr;
			span->_prev = nullptr;
			span->_objsize = 0;
			span->_objlist = nullptr;

			PageCache::GetInstance()->ReleaseSpanToPageCache(span);
		}
		start = next;
	}
}