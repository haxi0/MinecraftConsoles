#pragma once
#include <string>
#include <unordered_map>
#include <vector>
using namespace std;

class Connection;
class Minecraft;

// Forward-declare miniaudio types to avoid including the massive header here
struct ma_device;

class VoiceChatManager
{
public:
	enum VoiceInputMode
	{
		VOICE_INPUT_PUSH_TO_TALK = 0,
		VOICE_INPUT_VOICE_ACTIVATION = 1
	};

	static VoiceChatManager &getInstance();

	void init();
	void shutdown();

	// Called each game tick to send buffered captured audio
	void tick(Minecraft *minecraft);

	// Called when a voice packet arrives from the network
	void receiveVoiceData(int playerId, unsigned short sequence, double x, double y, double z, const unsigned char *data, int dataLength);

	// Update the local listener's position for 3D attenuation
	void setListenerPosition(double x, double y, double z);

	// Push-to-talk control
	void setPushToTalk(bool active) { m_isPushToTalkActive = active; }
	bool isPushToTalkActive() const { return m_isPushToTalkActive; }

	// Voice input mode controls
	void setVoiceInputMode(VoiceInputMode mode);
	VoiceInputMode getVoiceInputMode() const { return m_voiceInputMode; }
	void toggleVoiceInputMode();

	// Proximity chat controls
	void setProximityEnabled(bool enabled) { m_proximityEnabled = enabled; }
	bool isProximityEnabled() const { return m_proximityEnabled; }
	void toggleProximityEnabled();

	// Audio device selection controls
	bool cycleCaptureDevice(int dir = 1);
	bool cyclePlaybackDevice(int dir = 1);
	wstring getSelectedCaptureDeviceName() const;
	wstring getSelectedPlaybackDeviceName() const;
	void getCaptureDeviceNames(vector<wstring> &outNames, bool includeDefault = true) const;
	void getPlaybackDeviceNames(vector<wstring> &outNames, bool includeDefault = true) const;
	int getSelectedCaptureMenuIndex() const;
	int getSelectedPlaybackMenuIndex() const;
	bool selectCaptureMenuIndex(int menuIndex);
	bool selectPlaybackMenuIndex(int menuIndex);

	// Voice volume controls (percent, 0-200)
	void setMicVolumePercent(int percent);
	int getMicVolumePercent() const { return m_micVolumePercent; }
	void setVoiceChatVolumePercent(int percent);
	int getVoiceChatVolumePercent() const { return m_voiceChatVolumePercent; }

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

	struct ma_context *m_audioContext;
	ma_device *m_captureDevice;
	ma_device *m_playbackDevice;
	bool m_isPushToTalkActive;
	VoiceInputMode m_voiceInputMode;
	bool m_proximityEnabled;
	int m_voiceThreshold;
	std::vector<std::wstring> m_captureDevices;
	std::vector<std::wstring> m_playbackDevices;
	int m_selectedCaptureDevice;
	int m_selectedPlaybackDevice;
	unsigned short m_localSequence;
	int m_micVolumePercent;
	int m_voiceChatVolumePercent;

	// Capture buffer (ring buffer for PCM data captured from mic)
	static const int CAPTURE_BUFFER_SIZE = 96000; // 2 seconds at 48kHz mono
	short m_captureBuffer[CAPTURE_BUFFER_SIZE];
	int m_captureWritePos;
	int m_captureReadPos;
	CRITICAL_SECTION m_captureLock;

	// Per-player playback buffers
	struct RemoteVoiceStream
	{
		static const int BUFFER_SIZE = 96000; // 2 seconds at 48kHz mono
		short buffer[BUFFER_SIZE];
		int writePos;
		int readPos;
		double x, y, z;        // Last known position of this speaker
		int64_t lastReceiveTime; // For cleanup of stale streams
		unsigned short lastSequence;
		bool hasSequence;

		RemoteVoiceStream() : writePos(0), readPos(0), x(0), y(0), z(0), lastReceiveTime(0), lastSequence(0), hasSequence(false)
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
	static const int FRAME_SIZE_MS = 10;
	static const int FRAMES_PER_PACKET = SAMPLE_RATE * FRAME_SIZE_MS / 1000;
	static const int MAX_VOICE_DISTANCE = 32; // blocks

	// Speaking indicator state: entityId -> remaining ticks
	unordered_map<int, int> m_speakingEntities;
	CRITICAL_SECTION m_speakingLock;
};
