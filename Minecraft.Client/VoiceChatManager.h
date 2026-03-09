#pragma once
using namespace std;

class Connection;
class Minecraft;

// Forward-declare miniaudio types to avoid including the massive header here
struct ma_device;

class VoiceChatManager
{
public:
	static VoiceChatManager &getInstance();

	void init();
	void shutdown();

	// Called each game tick to send buffered captured audio
	void tick(Minecraft *minecraft);

	// Called when a voice packet arrives from the network
	void receiveVoiceData(int playerId, double x, double y, double z, const unsigned char *data, int dataLength);

	// Update the local listener's position for 3D attenuation
	void setListenerPosition(double x, double y, double z);

	// Push-to-talk control
	void setPushToTalk(bool active) { m_isPushToTalkActive = active; }
	bool isPushToTalkActive() const { return m_isPushToTalkActive; }

	bool isInitialized() const { return m_initialized; }

	// Speaking state tracking for rendering indicators
	void markSpeaking(int entityId);
	bool isEntitySpeaking(int entityId);
	void tickSpeakingState();

private:
	VoiceChatManager();
	~VoiceChatManager();
	VoiceChatManager(const VoiceChatManager &) = delete;
	VoiceChatManager &operator=(const VoiceChatManager &) = delete;

	// Miniaudio capture callback (static, called from audio thread)
	static void onCaptureAudio(ma_device *pDevice, void *pOutput, const void *pInput, unsigned int frameCount);

	// Miniaudio playback callback (static, called from audio thread)
	static void onPlaybackAudio(ma_device *pDevice, void *pOutput, const void *pInput, unsigned int frameCount);

	bool m_initialized;

	ma_device *m_captureDevice;
	ma_device *m_playbackDevice;
	bool m_isPushToTalkActive;

	// Capture buffer (ring buffer for PCM data captured from mic)
	static const int CAPTURE_BUFFER_SIZE = 96000; // 2 seconds at 48kHz mono
	short m_captureBuffer[CAPTURE_BUFFER_SIZE];
	int m_captureWritePos;
	int m_captureReadPos;
	CRITICAL_SECTION m_captureLock;

	// Voice activity detection threshold
	static const int VOICE_THRESHOLD = 500;

	// Per-player playback buffers
	struct RemoteVoiceStream
	{
		static const int BUFFER_SIZE = 96000; // 2 seconds at 48kHz mono
		short buffer[BUFFER_SIZE];
		int writePos;
		int readPos;
		double x, y, z;        // Last known position of this speaker
		int64_t lastReceiveTime; // For cleanup of stale streams

		RemoteVoiceStream() : writePos(0), readPos(0), x(0), y(0), z(0), lastReceiveTime(0)
		{
			memset(buffer, 0, sizeof(buffer));
		}
	};

	unordered_map<int, RemoteVoiceStream> m_remoteStreams;
	CRITICAL_SECTION m_playbackLock;

	// Listener position
	double m_listenerX, m_listenerY, m_listenerZ;

	// Constants
	static const int SAMPLE_RATE = 48000;
	static const int CHANNELS = 1;
	static const int FRAME_SIZE_MS = 20;
	static const int FRAMES_PER_PACKET = SAMPLE_RATE * FRAME_SIZE_MS / 1000; // 320 samples
	static const int MAX_VOICE_DISTANCE = 32; // blocks

	// Speaking indicator state: entityId -> remaining ticks
	unordered_map<int, int> m_speakingEntities;
	CRITICAL_SECTION m_speakingLock;
};
