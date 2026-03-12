#include "stdafx.h"
#include "VoiceChatManager.h"
#include "Minecraft.h"
#include "MultiPlayerLocalPlayer.h"
#include "ClientConnection.h"
#include "..\Minecraft.World\net.minecraft.network.packet.h"
#include "..\Minecraft.World\net.minecraft.world.entity.player.h"
#include "..\Minecraft.World\Connection.h"
#include "..\Minecraft.World\System.h"
#include <cstring>
#include <cmath>

// Include miniaudio
#include "Common\Audio\miniaudio.h"

namespace
{
	static const int MIN_VOICE_ACTIVATION_GAIN_PERCENT = 25;
	static const int MAX_VOICE_ACTIVATION_GAIN_PERCENT = 400;
	static const int VOICE_ACTIVATION_OPEN_PEAK = 900;
	static const int VOICE_ACTIVATION_OPEN_RMS = 220;
	static const int VOICE_ACTIVATION_CLOSE_PEAK = 600;
	static const int VOICE_ACTIVATION_CLOSE_RMS = 160;
	static const int VOICE_ACTIVATION_HOLD_FRAMES = 10;
	static const int PUSH_TO_TALK_GATE_PEAK = 450;
	static const int PUSH_TO_TALK_GATE_RMS = 120;
	static const int PUSH_TO_TALK_HOLD_FRAMES = 4;
	static const float CAPTURE_HIGHPASS_ALPHA = 0.995f;

	static void RefreshDeviceLists(
		std::vector<std::wstring> &playbackDevices,
		std::vector<std::wstring> &captureDevices,
		ma_device_info *pPlaybackInfos,
		ma_uint32 playbackCount,
		ma_device_info *pCaptureInfos,
		ma_uint32 captureCount)
	{
		playbackDevices.clear();
		captureDevices.clear();

		for (ma_uint32 i = 0; i < playbackCount; ++i)
		{
			const char *name = pPlaybackInfos[i].name;
			playbackDevices.emplace_back(name, name + strlen(name));
		}

		for (ma_uint32 i = 0; i < captureCount; ++i)
		{
			const char *name = pCaptureInfos[i].name;
			captureDevices.emplace_back(name, name + strlen(name));
		}
	}

	static const ma_device_id *GetSelectedDeviceId(int selectedIndex, ma_device_info *pInfos, ma_uint32 count)
	{
		if (selectedIndex < 0) return nullptr; // default device
		if (selectedIndex >= static_cast<int>(count)) return nullptr;
		return &pInfos[selectedIndex].id;
	}

	static int SampleMagnitude(int sample)
	{
		return sample >= 0 ? sample : -sample;
	}

	static short ClampSampleToShort(int sample)
	{
		if (sample > 32767) return 32767;
		if (sample < -32768) return -32768;
		return static_cast<short>(sample);
	}

	struct VoiceFrameMetrics
	{
		int peak;
		int rms;
	};

	static VoiceFrameMetrics MeasureVoiceFrame(const short *samples, int sampleCount, int gainPercent)
	{
		VoiceFrameMetrics metrics = { 0, 0 };
		if (samples == nullptr || sampleCount <= 0)
		{
			return metrics;
		}

		long long energy = 0;
		for (int i = 0; i < sampleCount; ++i)
		{
			int sample = (static_cast<int>(samples[i]) * gainPercent) / 100;
			const int magnitude = SampleMagnitude(sample);
			if (magnitude > metrics.peak)
			{
				metrics.peak = magnitude;
			}
			energy += static_cast<long long>(magnitude) * static_cast<long long>(magnitude);
		}

		metrics.rms = static_cast<int>(std::sqrt(static_cast<double>(energy) / static_cast<double>(sampleCount)));
		return metrics;
	}
}

VoiceChatManager &VoiceChatManager::getInstance()
{
	static VoiceChatManager instance;
	return instance;
}

VoiceChatManager::VoiceChatManager()
	: m_initialized(false)
	, m_audioContext(nullptr)
	, m_captureDevice(nullptr)
	, m_playbackDevice(nullptr)
	, m_captureWritePos(0)
	, m_captureReadPos(0)
	, m_listenerX(0), m_listenerY(0), m_listenerZ(0)
	, m_isPushToTalkActive(false)
	, m_voiceInputMode(VOICE_INPUT_PUSH_TO_TALK)
	, m_proximityEnabled(true)
	, m_selectedCaptureDevice(-1)
	, m_selectedPlaybackDevice(-1)
	, m_localSequence(0)
	, m_micVolumePercent(100)
	, m_voiceChatVolumePercent(100)
	, m_voiceActivationGainPercent(100)
	, m_voiceActivationHoldFrames(0)
	, m_pushToTalkHoldFrames(0)
	, m_captureFilterLastInput(0.0f)
	, m_captureFilterLastOutput(0.0f)
	, m_localMuted(false)
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

		float volume = 1.0f;

		if (mgr->m_proximityEnabled)
		{
			// Calculate distance attenuation
			double dx = stream.x - mgr->m_listenerX;
			double dy = stream.y - mgr->m_listenerY;
			double dz = stream.z - mgr->m_listenerZ;
			double dist = sqrt(dx * dx + dy * dy + dz * dz);

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
		}

		if (volume <= 0.001f) continue;
		volume *= (float)mgr->m_voiceChatVolumePercent / 100.0f;
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

	m_captureWritePos = 0;
	m_captureReadPos = 0;
	m_voiceActivationHoldFrames = 0;
	m_pushToTalkHoldFrames = 0;
	m_captureFilterLastInput = 0.0f;
	m_captureFilterLastOutput = 0.0f;
	memset(m_captureBuffer, 0, sizeof(m_captureBuffer));

	m_audioContext = new ma_context();
	if (ma_context_init(nullptr, 0, nullptr, m_audioContext) != MA_SUCCESS)
	{
		app.DebugPrintf("VoiceChat: Failed to initialize audio context\n");
		delete m_audioContext;
		m_audioContext = nullptr;
		return;
	}

	ma_device_info *pPlaybackInfos = nullptr;
	ma_uint32 playbackCount = 0;
	ma_device_info *pCaptureInfos = nullptr;
	ma_uint32 captureCount = 0;
	if (ma_context_get_devices(m_audioContext, &pPlaybackInfos, &playbackCount, &pCaptureInfos, &captureCount) != MA_SUCCESS)
	{
		app.DebugPrintf("VoiceChat: Failed to enumerate audio devices\n");
		ma_context_uninit(m_audioContext);
		delete m_audioContext;
		m_audioContext = nullptr;
		return;
	}

	RefreshDeviceLists(m_playbackDevices, m_captureDevices, pPlaybackInfos, playbackCount, pCaptureInfos, captureCount);
	if (m_selectedCaptureDevice >= static_cast<int>(m_captureDevices.size())) m_selectedCaptureDevice = -1;
	if (m_selectedPlaybackDevice >= static_cast<int>(m_playbackDevices.size())) m_selectedPlaybackDevice = -1;

	// Initialize capture device (microphone)
	m_captureDevice = new ma_device();
	ma_device_config captureConfig = ma_device_config_init(ma_device_type_capture);
	captureConfig.capture.format = ma_format_s16;
	captureConfig.capture.channels = CHANNELS;
	captureConfig.capture.pDeviceID = GetSelectedDeviceId(m_selectedCaptureDevice, pCaptureInfos, captureCount);
	captureConfig.sampleRate = SAMPLE_RATE;
	captureConfig.dataCallback = onCaptureAudio;
	captureConfig.pUserData = this;

	if (ma_device_init(m_audioContext, &captureConfig, m_captureDevice) != MA_SUCCESS)
	{
		app.DebugPrintf("VoiceChat: Failed to initialize capture device\n");
		delete m_captureDevice;
		m_captureDevice = nullptr;
	}
	else if (ma_device_start(m_captureDevice) != MA_SUCCESS)
	{
		app.DebugPrintf("VoiceChat: Failed to start capture device\n");
		ma_device_uninit(m_captureDevice);
		delete m_captureDevice;
		m_captureDevice = nullptr;
	}

	// Initialize playback device (speakers)
	m_playbackDevice = new ma_device();
	ma_device_config playbackConfig = ma_device_config_init(ma_device_type_playback);
	playbackConfig.playback.format = ma_format_s16;
	playbackConfig.playback.channels = CHANNELS;
	playbackConfig.playback.pDeviceID = GetSelectedDeviceId(m_selectedPlaybackDevice, pPlaybackInfos, playbackCount);
	playbackConfig.sampleRate = SAMPLE_RATE;
	playbackConfig.dataCallback = onPlaybackAudio;
	playbackConfig.pUserData = this;

	if (ma_device_init(m_audioContext, &playbackConfig, m_playbackDevice) != MA_SUCCESS)
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

	m_initialized = (m_captureDevice != nullptr || m_playbackDevice != nullptr);
	if (!m_initialized)
	{
		ma_context_uninit(m_audioContext);
		delete m_audioContext;
		m_audioContext = nullptr;
	}

	app.DebugPrintf("VoiceChat: Initialized (capture=%s, playback=%s, mode=%s, proximity=%s)\n",
		m_captureDevice ? "OK" : "FAIL",
		m_playbackDevice ? "OK" : "FAIL",
		m_voiceInputMode == VOICE_INPUT_PUSH_TO_TALK ? "PTT" : "VAD",
		m_proximityEnabled ? "ON" : "OFF");
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

	if (m_audioContext)
	{
		ma_context_uninit(m_audioContext);
		delete m_audioContext;
		m_audioContext = nullptr;
	}
	m_captureDevices.clear();
	m_playbackDevices.clear();
	m_voiceActivationHoldFrames = 0;
	m_pushToTalkHoldFrames = 0;
	m_captureFilterLastInput = 0.0f;
	m_captureFilterLastOutput = 0.0f;

	m_initialized = false;
	app.DebugPrintf("VoiceChat: Shutdown\n");
}

void VoiceChatManager::setVoiceInputMode(VoiceInputMode mode)
{
	if (m_voiceInputMode == mode)
	{
		return;
	}

	m_voiceInputMode = mode;
	m_voiceActivationHoldFrames = 0;
	m_pushToTalkHoldFrames = 0;
	app.DebugPrintf("VoiceChat: Input mode set to %s\n", m_voiceInputMode == VOICE_INPUT_PUSH_TO_TALK ? "PTT" : "VoiceActivation");
}

void VoiceChatManager::toggleVoiceInputMode()
{
	if (m_voiceInputMode == VOICE_INPUT_PUSH_TO_TALK)
	{
		setVoiceInputMode(VOICE_INPUT_VOICE_ACTIVATION);
	}
	else
	{
		setVoiceInputMode(VOICE_INPUT_PUSH_TO_TALK);
	}
}

void VoiceChatManager::toggleProximityEnabled()
{
	m_proximityEnabled = !m_proximityEnabled;
	app.DebugPrintf("VoiceChat: Proximity %s\n", m_proximityEnabled ? "enabled" : "disabled");
}

void VoiceChatManager::setProximityEnabled(bool enabled)
{
	if (m_proximityEnabled == enabled)
	{
		return;
	}

	m_proximityEnabled = enabled;
	app.DebugPrintf("VoiceChat: Proximity %s\n", m_proximityEnabled ? "enabled" : "disabled");
}

bool VoiceChatManager::cycleCaptureDevice(int dir)
{
	if (!m_initialized) init();
	if (m_captureDevices.empty()) return false;

	const int totalSlots = static_cast<int>(m_captureDevices.size()) + 1; // default + explicit devices
	int slot = m_selectedCaptureDevice + 1; // -1 -> 0
	slot = (slot + dir) % totalSlots;
	if (slot < 0) slot += totalSlots;
	m_selectedCaptureDevice = slot - 1;

	shutdown();
	init();

	app.DebugPrintf("VoiceChat: Capture device -> %ls\n", getSelectedCaptureDeviceName().c_str());
	return m_initialized;
}

bool VoiceChatManager::cyclePlaybackDevice(int dir)
{
	if (!m_initialized) init();
	if (m_playbackDevices.empty()) return false;

	const int totalSlots = static_cast<int>(m_playbackDevices.size()) + 1; // default + explicit devices
	int slot = m_selectedPlaybackDevice + 1; // -1 -> 0
	slot = (slot + dir) % totalSlots;
	if (slot < 0) slot += totalSlots;
	m_selectedPlaybackDevice = slot - 1;

	shutdown();
	init();

	app.DebugPrintf("VoiceChat: Playback device -> %ls\n", getSelectedPlaybackDeviceName().c_str());
	return m_initialized;
}

wstring VoiceChatManager::getSelectedCaptureDeviceName() const
{
	if (m_selectedCaptureDevice < 0 || m_selectedCaptureDevice >= static_cast<int>(m_captureDevices.size()))
	{
		return L"Default";
	}
	return m_captureDevices[m_selectedCaptureDevice];
}

wstring VoiceChatManager::getSelectedPlaybackDeviceName() const
{
	if (m_selectedPlaybackDevice < 0 || m_selectedPlaybackDevice >= static_cast<int>(m_playbackDevices.size()))
	{
		return L"Default";
	}
	return m_playbackDevices[m_selectedPlaybackDevice];
}

void VoiceChatManager::getCaptureDeviceNames(vector<wstring> &outNames, bool includeDefault) const
{
	outNames.clear();
	if (includeDefault) outNames.push_back(L"Default");
	for (const auto &name : m_captureDevices)
	{
		outNames.push_back(name);
	}
}

void VoiceChatManager::getPlaybackDeviceNames(vector<wstring> &outNames, bool includeDefault) const
{
	outNames.clear();
	if (includeDefault) outNames.push_back(L"Default");
	for (const auto &name : m_playbackDevices)
	{
		outNames.push_back(name);
	}
}

int VoiceChatManager::getSelectedCaptureMenuIndex() const
{
	return m_selectedCaptureDevice + 1;
}

int VoiceChatManager::getSelectedPlaybackMenuIndex() const
{
	return m_selectedPlaybackDevice + 1;
}

bool VoiceChatManager::selectCaptureMenuIndex(int menuIndex)
{
	if (!m_initialized) init();
	const int maxIndex = static_cast<int>(m_captureDevices.size());
	if (menuIndex < 0 || menuIndex > maxIndex) return false;

	const int nextSelected = menuIndex - 1;
	if (nextSelected == m_selectedCaptureDevice) return true;

	m_selectedCaptureDevice = nextSelected;
	shutdown();
	init();
	return m_initialized;
}

bool VoiceChatManager::selectPlaybackMenuIndex(int menuIndex)
{
	if (!m_initialized) init();
	const int maxIndex = static_cast<int>(m_playbackDevices.size());
	if (menuIndex < 0 || menuIndex > maxIndex) return false;

	const int nextSelected = menuIndex - 1;
	if (nextSelected == m_selectedPlaybackDevice) return true;

	m_selectedPlaybackDevice = nextSelected;
	shutdown();
	init();
	return m_initialized;
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

	// Send in short frames to keep UDP payloads under common MTU limits.
	while (available >= FRAMES_PER_PACKET)
	{
		short frameBuffer[FRAMES_PER_PACKET];
		for (int i = 0; i < FRAMES_PER_PACKET; i++)
		{
			int sample = m_captureBuffer[m_captureReadPos];
			sample = (sample * m_micVolumePercent) / 100;

			// Strip DC bias and low-frequency rumble so we do not transmit a bed of hiss/hum when the mic is idle.
			const float filteredSample =
				static_cast<float>(sample) - m_captureFilterLastInput + (CAPTURE_HIGHPASS_ALPHA * m_captureFilterLastOutput);
			m_captureFilterLastInput = static_cast<float>(sample);
			m_captureFilterLastOutput = filteredSample;

			frameBuffer[i] = ClampSampleToShort(static_cast<int>(filteredSample));
			m_captureReadPos = (m_captureReadPos + 1) % CAPTURE_BUFFER_SIZE;
		}

		const VoiceFrameMetrics pushToTalkMetrics = MeasureVoiceFrame(frameBuffer, FRAMES_PER_PACKET, 100);
		bool pushToTalkSpeechDetected =
			pushToTalkMetrics.peak >= PUSH_TO_TALK_GATE_PEAK ||
			pushToTalkMetrics.rms >= PUSH_TO_TALK_GATE_RMS;

		if (m_isPushToTalkActive)
		{
			if (pushToTalkSpeechDetected)
			{
				m_pushToTalkHoldFrames = PUSH_TO_TALK_HOLD_FRAMES;
			}
			else if (m_pushToTalkHoldFrames > 0)
			{
				--m_pushToTalkHoldFrames;
			}
		}
		else
		{
			m_pushToTalkHoldFrames = 0;
		}

		const VoiceFrameMetrics voiceActivationMetrics =
			MeasureVoiceFrame(frameBuffer, FRAMES_PER_PACKET, m_voiceActivationGainPercent);
		const bool useCloseThresholds = m_voiceActivationHoldFrames > 0;
		const int requiredPeak = useCloseThresholds ? VOICE_ACTIVATION_CLOSE_PEAK : VOICE_ACTIVATION_OPEN_PEAK;
		const int requiredRms = useCloseThresholds ? VOICE_ACTIVATION_CLOSE_RMS : VOICE_ACTIVATION_OPEN_RMS;
		const bool voiceActivationDetected =
			voiceActivationMetrics.peak >= requiredPeak ||
			voiceActivationMetrics.rms >= requiredRms;

		if (voiceActivationDetected)
		{
			m_voiceActivationHoldFrames = VOICE_ACTIVATION_HOLD_FRAMES;
		}
		else if (m_voiceActivationHoldFrames > 0)
		{
			--m_voiceActivationHoldFrames;
		}

		bool shouldSend = false;
		if (m_voiceInputMode == VOICE_INPUT_PUSH_TO_TALK)
		{
			shouldSend = m_isPushToTalkActive && (pushToTalkSpeechDetected || m_pushToTalkHoldFrames > 0);
		}
		else
		{
			shouldSend = m_voiceActivationHoldFrames > 0;
		}

		if (m_localMuted)
		{
			shouldSend = false;
		}

		if (shouldSend)
		{
			// Mark our local player as speaking for the indicator
			markSpeaking(minecraft->player->entityId);

			// Send raw PCM as a voice chat packet
			short dataLen = (short)(FRAMES_PER_PACKET * sizeof(short));
			byteArray audioData(dataLen);
			memcpy(audioData.data, frameBuffer, dataLen);

			shared_ptr<Packet> packet = std::make_shared<VoiceChatPacket>(
				minecraft->player->entityId, m_localSequence++, audioData, dataLen);

			// Queue to slow path so it is emitted as standalone UDP transport packets.
			conn->queueSend(packet);
		}

		available = (m_captureWritePos - m_captureReadPos + CAPTURE_BUFFER_SIZE) % CAPTURE_BUFFER_SIZE;
	}

	LeaveCriticalSection(&m_captureLock);

	if (m_localMuted)
	{
		clearSpeaking(minecraft->player->entityId);
	}

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

void VoiceChatManager::receiveVoiceData(int playerId, unsigned short sequence, double x, double y, double z, const unsigned char *data, int dataLength)
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

	// Reject very old/out-of-order packets aggressively, but tolerate wrap-around.
	if (stream.hasSequence)
	{
		unsigned short delta = static_cast<unsigned short>(sequence - stream.lastSequence);
		if (delta == 0)
		{
			LeaveCriticalSection(&m_playbackLock);
			return; // duplicate
		}
		if (delta > 32768)
		{
			LeaveCriticalSection(&m_playbackLock);
			return; // stale packet
		}

		// Packet loss concealment: for small gaps, insert silence frames.
		const int missingPackets = static_cast<int>(delta) - 1;
		if (missingPackets > 0)
		{
			const int concealPackets = (missingPackets > 3) ? 3 : missingPackets;
			const int concealSamples = concealPackets * FRAMES_PER_PACKET;
			for (int i = 0; i < concealSamples; ++i)
			{
				stream.buffer[stream.writePos] = 0;
				stream.writePos = (stream.writePos + 1) % RemoteVoiceStream::BUFFER_SIZE;
			}
		}
	}

	stream.lastSequence = sequence;
	stream.hasSequence = true;

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

void VoiceChatManager::clearSpeaking(int entityId)
{
	EnterCriticalSection(&m_speakingLock);
	m_speakingEntities.erase(entityId);
	LeaveCriticalSection(&m_speakingLock);
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

void VoiceChatManager::setMicVolumePercent(int percent)
{
	if (percent < 0) percent = 0;
	if (percent > 200) percent = 200;
	m_micVolumePercent = percent;
}

void VoiceChatManager::setVoiceChatVolumePercent(int percent)
{
	if (percent < 0) percent = 0;
	if (percent > 200) percent = 200;
	m_voiceChatVolumePercent = percent;
}

void VoiceChatManager::setVoiceActivationGainPercent(int percent)
{
	if (percent < MIN_VOICE_ACTIVATION_GAIN_PERCENT) percent = MIN_VOICE_ACTIVATION_GAIN_PERCENT;
	if (percent > MAX_VOICE_ACTIVATION_GAIN_PERCENT) percent = MAX_VOICE_ACTIVATION_GAIN_PERCENT;
	m_voiceActivationGainPercent = percent;
}

void VoiceChatManager::setLocalMuted(bool muted)
{
	if (m_localMuted == muted)
	{
		return;
	}

	m_localMuted = muted;
	if (m_localMuted)
	{
		m_isPushToTalkActive = false;
		m_pushToTalkHoldFrames = 0;
		m_voiceActivationHoldFrames = 0;
	}

	app.DebugPrintf("VoiceChat: Local mic %s\n", m_localMuted ? "muted" : "unmuted");
}

void VoiceChatManager::toggleLocalMuted()
{
	setLocalMuted(!m_localMuted);
}
