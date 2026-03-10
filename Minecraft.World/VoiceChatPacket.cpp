#include "stdafx.h"
#include "InputOutputStream.h"
#include "PacketListener.h"
#include "VoiceChatPacket.h"

VoiceChatPacket::VoiceChatPacket()
	: senderPlayerId(-1), sequence(0), dataLength(0), audioData()
{
}

VoiceChatPacket::VoiceChatPacket(int senderPlayerId, unsigned short sequence, byteArray audioData, short dataLength)
	: senderPlayerId(senderPlayerId), sequence(sequence), audioData(audioData), dataLength(dataLength)
{
}

VoiceChatPacket::~VoiceChatPacket()
{
}

void VoiceChatPacket::read(DataInputStream *dis)
{
	senderPlayerId = dis->readInt();
	sequence = static_cast<unsigned short>(dis->readShort());
	dataLength = dis->readShort();
	if (dataLength > 0 && dataLength <= 8192)
	{
		audioData = byteArray(dataLength);
		dis->readFully(audioData);
	}
	else
	{
		audioData = byteArray();
		dataLength = 0;
	}
}

void VoiceChatPacket::write(DataOutputStream *dos)
{
	dos->writeInt(senderPlayerId);
	dos->writeShort(static_cast<short>(sequence));
	dos->writeShort(dataLength);
	if (dataLength > 0)
	{
		dos->write(audioData);
	}
}

void VoiceChatPacket::handle(PacketListener *listener)
{
	listener->handleVoiceChat(this);
}

int VoiceChatPacket::getEstimatedSize()
{
	return sizeof(int) + sizeof(short) + sizeof(short) + dataLength;
}
