#pragma once
using namespace std;

#include "Packet.h"

class VoiceChatPacket : public Packet, public enable_shared_from_this<VoiceChatPacket>
{
public:
	int senderPlayerId;
	short dataLength;
	byteArray audioData;

	VoiceChatPacket();
	VoiceChatPacket(int senderPlayerId, byteArray audioData, short dataLength);
	~VoiceChatPacket();

	virtual void read(DataInputStream *dis);
	virtual void write(DataOutputStream *dos);
	virtual void handle(PacketListener *listener);
	virtual int getEstimatedSize();

	static shared_ptr<Packet> create() { return std::make_shared<VoiceChatPacket>(); }
	virtual int getId() { return 251; }
};
