#include "stdafx.h"
#include "VoiceChatManager.h"
#include "Minecraft.h"
#include "MultiPlayerLocalPlayer.h"
#include "ClientConnection.h"
#include "..\Minecraft.World\net.minecraft.network.packet.h"
#include "..\Minecraft.World\net.minecraft.world.entity.player.h"
#include "..\Minecraft.World\Connection.h"
#include "..\Minecraft.World\System.h"

// Include miniaudio
#include "Common\Audio\miniaudio.h"

VoiceChatManager &VoiceChatManager::getInstance()
{
	static VoiceChatManager instance;
	return instance;
}

VoiceChatManager::VoiceChatManager()
	: m_initialized(false)
	, m_captureDevice(nullptr)
	, m_playbackDevice(nullptr)
	, m_captureWritePos(0)
	, m_captureReadPos(0)
	, m_listenerX(0), m_listenerY(0), m_listenerZ(0)
	, m_isPushToTalkActive(false)
{
	memset(m_captureBuffer, 0, sizeof(m_captureBuffer));
	InitializeCriticalSection(&m_captureLock);
	InitializeCriticalSection(&m_playbackLock);
	InitializeCriticalSection(&m_speakingLock);
}

VoiceChatManager::~VoiceChatManager()
{
	shutdown();
	DeleteCriticalSection(&m_captureLock);
	DeleteCriticalSection(&m_playbackLock);
	DeleteCriticalSection(&m_speakingLock);
}

void VoiceChatManager::onCaptureAudio(ma_device *pDevice, void *pOutput, const void *pInput, unsigned int frameCount)
{
	VoiceChatManager *mgr = (VoiceChatManager *)pDevice->pUserData;
	const short *input = (const short *)pInput;

	if (input == nullptr || mgr == nullptr) return;

	EnterCriticalSection(&mgr->m_captureLock);
	for (unsigned int i = 0; i < frameCount; i++)
	{
		mgr->m_captureBuffer[mgr->m_captureWritePos] = input[i];
		mgr->m_captureWritePos = (mgr->m_captureWritePos + 1) % CAPTURE_BUFFER_SIZE;
	}
	LeaveCriticalSection(&mgr->m_captureLock);
}

void VoiceChatManager::onPlaybackAudio(ma_device *pDevice, void *pOutput, const void *pInput, unsigned int frameCount)
{
	VoiceChatManager *mgr = (VoiceChatManager *)pDevice->pUserData;
	short *output = (short *)pOutput;

	if (output == nullptr || mgr == nullptr) return;

	memset(output, 0, frameCount * sizeof(short));

	EnterCriticalSection(&mgr->m_playbackLock);
	for (auto &pair : mgr->m_remoteStreams)
	{
		RemoteVoiceStream &stream = pair.second;

		// Calculate distance attenuation
		double dx = stream.x - mgr->m_listenerX;
		double dy = stream.y - mgr->m_listenerY;
		double dz = stream.z - mgr->m_listenerZ;
		double dist = sqrt(dx * dx + dy * dy + dz * dz);

		float volume = 1.0f;
		if (dist >= MAX_VOICE_DISTANCE)
		{
			volume = 0.0f;
		}
		else if (dist > 1.0)
		{
			// Linear falloff with exponential tail
			float linearFactor = 1.0f - (float)(dist / MAX_VOICE_DISTANCE);
			float expFactor = (float)exp(-dist * 0.08);
			volume = linearFactor * expFactor;
		}

		if (volume <= 0.001f) continue;

		// Mix this stream's audio into the output
		for (unsigned int i = 0; i < frameCount; i++)
		{
			if (stream.readPos != stream.writePos)
			{
				int sample = (int)(stream.buffer[stream.readPos] * volume);
				int mixed = output[i] + sample;

				// Clamp to prevent clipping
				if (mixed > 32767) mixed = 32767;
				if (mixed < -32768) mixed = -32768;

				output[i] = (short)mixed;
				stream.readPos = (stream.readPos + 1) % RemoteVoiceStream::BUFFER_SIZE;
			}
		}
	}
	LeaveCriticalSection(&mgr->m_playbackLock);
}

void VoiceChatManager::init()
{
	if (m_initialized) return;

	// Initialize capture device (microphone)
	m_captureDevice = new ma_device();
	ma_device_config captureConfig = ma_device_config_init(ma_device_type_capture);
	captureConfig.capture.format = ma_format_s16;
	captureConfig.capture.channels = CHANNELS;
	captureConfig.sampleRate = SAMPLE_RATE;
	captureConfig.dataCallback = onCaptureAudio;
	captureConfig.pUserData = this;

	if (ma_device_init(nullptr, &captureConfig, m_captureDevice) != MA_SUCCESS)
	{
		app.DebugPrintf("VoiceChat: Failed to initialize capture device\n");
		delete m_captureDevice;
		m_captureDevice = nullptr;
		return;
	}

	if (ma_device_start(m_captureDevice) != MA_SUCCESS)
	{
		app.DebugPrintf("VoiceChat: Failed to start capture device\n");
		ma_device_uninit(m_captureDevice);
		delete m_captureDevice;
		m_captureDevice = nullptr;
		return;
	}

	// Initialize playback device (speakers)
	m_playbackDevice = new ma_device();
	ma_device_config playbackConfig = ma_device_config_init(ma_device_type_playback);
	playbackConfig.playback.format = ma_format_s16;
	playbackConfig.playback.channels = CHANNELS;
	playbackConfig.sampleRate = SAMPLE_RATE;
	playbackConfig.dataCallback = onPlaybackAudio;
	playbackConfig.pUserData = this;

	if (ma_device_init(nullptr, &playbackConfig, m_playbackDevice) != MA_SUCCESS)
	{
		app.DebugPrintf("VoiceChat: Failed to initialize playback device\n");
		delete m_playbackDevice;
		m_playbackDevice = nullptr;
		// Still keep capture running
	}
	else
	{
		if (ma_device_start(m_playbackDevice) != MA_SUCCESS)
		{
			app.DebugPrintf("VoiceChat: Failed to start playback device\n");
			ma_device_uninit(m_playbackDevice);
			delete m_playbackDevice;
			m_playbackDevice = nullptr;
		}
	}

	m_initialized = true;
	app.DebugPrintf("VoiceChat: Initialized (capture=%s, playback=%s)\n",
		m_captureDevice ? "OK" : "FAIL", m_playbackDevice ? "OK" : "FAIL");
}

void VoiceChatManager::shutdown()
{
	if (!m_initialized) return;

	if (m_captureDevice)
	{
		ma_device_uninit(m_captureDevice);
		delete m_captureDevice;
		m_captureDevice = nullptr;
	}

	if (m_playbackDevice)
	{
		ma_device_uninit(m_playbackDevice);
		delete m_playbackDevice;
		m_playbackDevice = nullptr;
	}

	EnterCriticalSection(&m_playbackLock);
	m_remoteStreams.clear();
	LeaveCriticalSection(&m_playbackLock);

	m_initialized = false;
	app.DebugPrintf("VoiceChat: Shutdown\n");
}

void VoiceChatManager::tick(Minecraft *minecraft)
{
	if (!m_initialized || m_captureDevice == nullptr) return;
	if (minecraft == nullptr || minecraft->player == nullptr) return;

	int iPad = minecraft->player->GetXboxPad();
	ClientConnection *conn = minecraft->getConnection(iPad);
	if (conn == nullptr) return;

	// Update listener position
	setListenerPosition(minecraft->player->x, minecraft->player->y, minecraft->player->z);

	// Read captured audio and send as packets
	EnterCriticalSection(&m_captureLock);

	int available = (m_captureWritePos - m_captureReadPos + CAPTURE_BUFFER_SIZE) % CAPTURE_BUFFER_SIZE;

	// Send in chunks of FRAMES_PER_PACKET (320 samples = 20ms at 16kHz)
	while (available >= FRAMES_PER_PACKET)
	{
		short frameBuffer[FRAMES_PER_PACKET];
		for (int i = 0; i < FRAMES_PER_PACKET; i++)
		{
			frameBuffer[i] = m_captureBuffer[m_captureReadPos];
			m_captureReadPos = (m_captureReadPos + 1) % CAPTURE_BUFFER_SIZE;
		}

		// Send audio only if Push-To-Talk is active
		if (m_isPushToTalkActive)
		{
			// Mark our local player as speaking for the indicator
			markSpeaking(minecraft->player->entityId);

			// Send raw PCM as a voice chat packet
			short dataLen = (short)(FRAMES_PER_PACKET * sizeof(short));
			byteArray audioData(dataLen);
			memcpy(audioData.data, frameBuffer, dataLen);

			shared_ptr<Packet> packet = std::make_shared<VoiceChatPacket>(
				minecraft->player->entityId, audioData, dataLen);

			conn->send(packet);
		}

		available = (m_captureWritePos - m_captureReadPos + CAPTURE_BUFFER_SIZE) % CAPTURE_BUFFER_SIZE;
	}

	LeaveCriticalSection(&m_captureLock);

	// Cleanup stale remote streams (no data for >2 seconds)
	int64_t now = System::currentTimeMillis();
	EnterCriticalSection(&m_playbackLock);
	for (auto it = m_remoteStreams.begin(); it != m_remoteStreams.end(); )
	{
		if (now - it->second.lastReceiveTime > 2000)
		{
			it = m_remoteStreams.erase(it);
		}
		else
		{
			++it;
		}
	}
	LeaveCriticalSection(&m_playbackLock);

	// Tick speaking indicators
	tickSpeakingState();
}

void VoiceChatManager::receiveVoiceData(int playerId, double x, double y, double z, const unsigned char *data, int dataLength)
{
	if (!m_initialized || m_playbackDevice == nullptr) return;
	if (data == nullptr || dataLength <= 0) return;

	int sampleCount = dataLength / sizeof(short);
	const short *samples = (const short *)data;

	EnterCriticalSection(&m_playbackLock);

	RemoteVoiceStream &stream = m_remoteStreams[playerId];
	stream.x = x;
	stream.y = y;
	stream.z = z;
	stream.lastReceiveTime = System::currentTimeMillis();

	for (int i = 0; i < sampleCount; i++)
	{
		stream.buffer[stream.writePos] = samples[i];
		stream.writePos = (stream.writePos + 1) % RemoteVoiceStream::BUFFER_SIZE;
	}

	LeaveCriticalSection(&m_playbackLock);
}

void VoiceChatManager::setListenerPosition(double x, double y, double z)
{
	m_listenerX = x;
	m_listenerY = y;
	m_listenerZ = z;
}

void VoiceChatManager::markSpeaking(int entityId)
{
	EnterCriticalSection(&m_speakingLock);
	m_speakingEntities[entityId] = 30; // ~1.5 seconds at 20 TPS
	LeaveCriticalSection(&m_speakingLock);
}

bool VoiceChatManager::isEntitySpeaking(int entityId)
{
	EnterCriticalSection(&m_speakingLock);
	bool speaking = m_speakingEntities.count(entityId) > 0 && m_speakingEntities[entityId] > 0;
	LeaveCriticalSection(&m_speakingLock);
	return speaking;
}

void VoiceChatManager::tickSpeakingState()
{
	EnterCriticalSection(&m_speakingLock);
	for (auto it = m_speakingEntities.begin(); it != m_speakingEntities.end(); )
	{
		it->second--;
		if (it->second <= 0)
		{
			it = m_speakingEntities.erase(it);
		}
		else
		{
			++it;
		}
	}
	LeaveCriticalSection(&m_speakingLock);
}
