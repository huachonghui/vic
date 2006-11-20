#include "packetbuffer.h"
#include "databuffer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


PacketBuffer::PacketBuffer(int maxPacket, int maxLength)
{
    totalPkts = 0;
    maxPkts = maxPacket;
    maxLen = maxLength;
    stream = new DataBuffer(maxPacket * maxLength);

    for (int i = 0; i < maxPkts; i++) {
	isDataRecv[i] = false;
	packets[i] = new DataBuffer(maxLen);
	//std::cout << "allocate databuffer " << i << "\n";
    }
}

PacketBuffer::~PacketBuffer()
{
    delete stream;
    for (int i = 0; i < maxPkts; i++)
	delete packets[i];
}

void PacketBuffer::write(int idx, int size, char *buffer)
{
    if (idx > MAX_PACKETS || idx < 0)
	return;
    isDataRecv[idx] = packets[idx]->write(buffer, size);
}

bool PacketBuffer::isComplete()
{
    if (totalPkts == 0)
	return false;
    for (int i = 0; i < totalPkts; i++){	
	if (!isDataRecv[i]){
	    printf("lost packet %d\n", i);	
	    //return false;
	}
    }
    return true;
}

void PacketBuffer::setTotalPkts(int n)
{
    totalPkts = n;
}

int PacketBuffer::getTotalPkts()
{
    return totalPkts;
}

//return bitstream , corrupted packets will be ignored
DataBuffer *PacketBuffer::getStream()
{
    if (totalPkts == 0)
	return NULL;

    //int offset = 0;
    //char* dst = stream->getData();
    for (int i = 0; i < totalPkts; i++) {
	if (isDataRecv[i]) {
	    //memcpy(dst+offset, packets[i]->getData(), packets[i]->getDataSize() );
	    //offset+=packets[i]->getDataSize();
	    stream->writeAppend(packets[i]->getData(),
				packets[i]->getDataSize());
	}
    }
    //stream->setSize(offset);
    return stream;
}

void PacketBuffer::clear()
{
    //std::cout << "Total packets " << totalPkts << "\n";
    for (int i = 0; i < totalPkts; i++) {
	packets[i]->setSize(0);
	stream->setSize(0);
	isDataRecv[i] = false;
    }
    totalPkts = 0;
}
