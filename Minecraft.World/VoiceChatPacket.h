#pragma once
using namespace std;

#include "Packet.h"

class VoiceChatPacket : public Packet, public enable_shared_from_this<VoiceChatPacket>
{
public:
	int senderPlayerId;
	unsigned short sequence;
	short dataLength;
	byteArray audioData;

	VoiceChatPacket();
	VoiceChatPacket(int senderPlayerId, unsigned short sequence, byteArray audioData, short dataLength);
	~VoiceChatPacket();

	virtual void read(DataInputStream *dis);
	virtual void write(DataOutputStream *dos);
	virtual void handle(PacketListener *listener);
	virtual int getEstimatedSize();
	virtual bool usesUdpTransport() { return true; }
	virtual bool isAync() { return true; }

	static shared_ptr<Packet> create() { return std::make_shared<VoiceChatPacket>(); }
	virtual int getId() { return 251; }
};
