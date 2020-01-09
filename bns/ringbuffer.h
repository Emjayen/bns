/*
 * ringbuffer.h
 *
 */
#ifndef RINGBUFFER_H
#define RINGBUFFER_H
#include <pce\pce.h>





struct ring_buffer
{
	byte* pBuffer;
	byte* pWrite;
	byte* pRead;
	uid Length;
	uid BufferSize;
	void* hSection;
};



uib rb_init(ring_buffer* rb, uid Size, char* pName);
uid rb_write(ring_buffer* rb, void* pBuffer, uid Length);
uid rb_length(ring_buffer* rb);
byte* rb_read(ring_buffer* rb, uid Length);
void rb_reset(ring_buffer* rb);
byte* rb_write_ptr(ring_buffer* rb);
void rb_write_ex(ring_buffer* rb, uid Length);
byte* rb_read_ptr(ring_buffer* rb);
void rb_read_ex(ring_buffer* rb, uid Length);



#endif