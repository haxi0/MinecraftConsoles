#include "stdafx.h"
#include "InputOutputStream.h"
#include "PacketListener.h"
#include "VoiceChatPacket.h"

VoiceChatPacket::VoiceChatPacket()
	: senderPlayerId(-1), dataLength(0), audioData()
{
}

VoiceChatPacket::VoiceChatPacket(int senderPlayerId, byteArray audioData, short dataLength)
	: senderPlayerId(senderPlayerId), audioData(audioData), dataLength(dataLength)
{
}

VoiceChatPacket::~VoiceChatPacket()
{
}

void VoiceChatPacket::read(DataInputStream *dis)
{
	senderPlayerId = dis->readInt();
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
	return sizeof(int) + sizeof(short) + dataLength;
}
