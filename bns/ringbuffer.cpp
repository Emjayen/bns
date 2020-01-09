/*
 * ringbuffer.cpp
 *
 */
#include "ringbuffer.h"
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>







uib rb_init(ring_buffer* rb, uid Size, char* pName)
{
	rb->BufferSize = (Size & 0xFFFF ? ((Size + 0x10000) & 0xFFFF0000) : (Size & 0xFFFF0000));

	if(!(rb->pBuffer = (byte*) VirtualAlloc(NULL, rb->BufferSize*2, MEM_RESERVE, PAGE_READWRITE)))
		return FALSE;

	if(!VirtualFree(rb->pBuffer, 0, MEM_RELEASE))
		return FALSE;

	if(!(rb->hSection = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, NULL, rb->BufferSize, NULL)))
		return FALSE;

	if(!MapViewOfFileEx(rb->hSection, FILE_MAP_ALL_ACCESS, 0, 0, rb->BufferSize, rb->pBuffer))
		return FALSE;

	if(!MapViewOfFileEx(rb->hSection, FILE_MAP_ALL_ACCESS, 0, 0, rb->BufferSize, rb->pBuffer+rb->BufferSize))
		return FALSE;

	rb_reset(rb);

	return TRUE;
}




uid rb_write(ring_buffer* rb, void* pBuffer, uid Length)
{
	if((rb->Length += Length) > rb->BufferSize)
	{
		rb->Length -= Length;
		return 0;
	}

	memcpy(rb->pWrite, pBuffer, Length);
	rb->pWrite += Length;

	if(rb->pWrite >= (rb->pBuffer + rb->BufferSize))
	{
		rb->pWrite -= rb->BufferSize;
	}

	return Length;
}


uid rb_length(ring_buffer* rb)
{
	return rb->Length;
}


byte* rb_read(ring_buffer* rb, uid Length)
{
	byte* p = rb->pRead;

	if(Length > rb->Length)
		Length = rb->Length;

	rb->Length -= Length;
	rb->pRead += Length;

	if(rb->pRead >= (rb->pBuffer + rb->BufferSize))
	{
		rb->pRead -= rb->BufferSize;
	}

	return p;
}


void rb_read_ex(ring_buffer* rb, uid Length)
{
	rb->pRead += Length;
	rb->Length -= Length;

	if(rb->pRead >= (rb->pBuffer + rb->BufferSize))
	{
		rb->pRead -= rb->BufferSize;
	}
}


void rb_reset(ring_buffer* rb)
{
	rb->pWrite = rb->pBuffer;
	rb->pRead = rb->pBuffer;
	rb->Length = 0;
}


byte* rb_write_ptr(ring_buffer* rb)
{
	return rb->pWrite;
}

void rb_write_ex(ring_buffer* rb, uid Length)
{
	rb->pWrite += Length;
	rb->Length += Length;

	if(rb->pWrite >= (rb->pBuffer + rb->BufferSize))
	{
		rb->pWrite -= rb->BufferSize;
	}
}


byte* rb_read_ptr(ring_buffer* rb)
{
	return rb->pRead;
}
