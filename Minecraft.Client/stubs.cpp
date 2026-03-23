#include "stdafx.h"

#ifdef _WINDOWS64
#include "Windows64\KeyboardMouseInput.h"

static const int s_keyToVK[] = {
	'A',        // KEY_A = 0
	'B',        // KEY_B = 1
	'C',        // KEY_C = 2
	'D',        // KEY_D = 3
	'E',        // KEY_E = 4
	'F',        // KEY_F = 5
	'G',        // KEY_G = 6
	'H',        // KEY_H = 7
	'I',        // KEY_I = 8
	'J',        // KEY_J = 9
	'K',        // KEY_K = 10
	'L',        // KEY_L = 11
	'M',        // KEY_M = 12
	'N',        // KEY_N = 13
	'O',        // KEY_O = 14
	'P',        // KEY_P = 15
	'Q',        // KEY_Q = 16
	'R',        // KEY_R = 17
	'S',        // KEY_S = 18
	'T',        // KEY_T = 19
	'U',        // KEY_U = 20
	'V',        // KEY_V = 21
	'W',        // KEY_W = 22
	'X',        // KEY_X = 23
	'Y',        // KEY_Y = 24
	'Z',        // KEY_Z = 25
	VK_SPACE,   // KEY_SPACE = 26
	VK_LSHIFT,  // KEY_LSHIFT = 27
	VK_ESCAPE,  // KEY_ESCAPE = 28
	VK_BACK,    // KEY_BACK = 29
	VK_RETURN,  // KEY_RETURN = 30
	VK_RSHIFT,  // KEY_RSHIFT = 31
	VK_UP,      // KEY_UP = 32
	VK_DOWN,    // KEY_DOWN = 33
	VK_TAB,     // KEY_TAB = 34
	'1',        // KEY_1 = 35
	'2',        // KEY_2 = 36
	'3',        // KEY_3 = 37
	'4',        // KEY_4 = 38
	'5',        // KEY_5 = 39
	'6',        // KEY_6 = 40
	'7',        // KEY_7 = 41
	'8',        // KEY_8 = 42
	'9',        // KEY_9 = 43
	VK_F1,      // KEY_F1 = 44
	VK_F3,      // KEY_F3 = 45
	VK_F4,      // KEY_F4 = 46
	VK_F5,      // KEY_F5 = 47
	VK_F6,      // KEY_F6 = 48
	VK_F8,      // KEY_F8 = 49
	VK_F9,      // KEY_F9 = 50
	VK_F11,     // KEY_F11 = 51
	VK_ADD,     // KEY_ADD = 52
	VK_SUBTRACT,// KEY_SUBTRACT = 53
	VK_LEFT,    // KEY_LEFT = 54
	VK_RIGHT,   // KEY_RIGHT = 55
};
static const int s_keyToVKCount = sizeof(s_keyToVK) / sizeof(s_keyToVK[0]);

int Keyboard::toVK(int keyConst)
{
	if (keyConst >= 0 && keyConst < s_keyToVKCount)
		return s_keyToVK[keyConst];
	return 0;
}

bool Keyboard::isKeyDown(int keyCode)
{
	int vk = toVK(keyCode);
	if (vk > 0)
		return g_KBMInput.IsKeyDown(vk);
	return false;
}

int Mouse::getX()
{
	return g_KBMInput.GetMouseX();
}

int Mouse::getY()
{
	// Return Y in bottom-up coordinates (OpenGL convention, matching original Java LWJGL Mouse)
	extern HWND g_hWnd;
	RECT rect;
	GetClientRect(g_hWnd, &rect);
	return (rect.bottom - 1) - g_KBMInput.GetMouseY();
}

bool Mouse::isButtonDown(int button)
{
	return g_KBMInput.IsMouseButtonDown(button);
}
#endif

#if defined(MINECRAFT_SERVER_BUILD)
#include "VoiceChatManager.h"

VoiceChatManager::VoiceChatManager()
	: m_initialized(false)
	, m_audioContext(NULL)
	, m_captureDevice(NULL)
	, m_playbackDevice(NULL)
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
	, m_captureWritePos(0)
	, m_captureReadPos(0)
	, m_listenerX(0.0)
	, m_listenerY(0.0)
	, m_listenerZ(0.0)
{
}

VoiceChatManager::~VoiceChatManager()
{
}

VoiceChatManager &VoiceChatManager::getInstance()
{
	static VoiceChatManager instance;
	return instance;
}

void VoiceChatManager::init()
{
	m_initialized = true;
}

void VoiceChatManager::shutdown()
{
	m_initialized = false;
}

void VoiceChatManager::tick(Minecraft *)
{
	tickSpeakingState();
}

void VoiceChatManager::receiveVoiceData(int playerId, unsigned short, double, double, double, const unsigned char *, int)
{
	markSpeaking(playerId);
}

void VoiceChatManager::setListenerPosition(double x, double y, double z)
{
	m_listenerX = x;
	m_listenerY = y;
	m_listenerZ = z;
}

void VoiceChatManager::setVoiceInputMode(VoiceInputMode mode)
{
	m_voiceInputMode = mode;
}

void VoiceChatManager::toggleVoiceInputMode()
{
	m_voiceInputMode = (m_voiceInputMode == VOICE_INPUT_PUSH_TO_TALK) ? VOICE_INPUT_VOICE_ACTIVATION : VOICE_INPUT_PUSH_TO_TALK;
}

void VoiceChatManager::setProximityEnabled(bool enabled)
{
	m_proximityEnabled = enabled;
}

void VoiceChatManager::toggleProximityEnabled()
{
	m_proximityEnabled = !m_proximityEnabled;
}

bool VoiceChatManager::cycleCaptureDevice(int)
{
	return false;
}

bool VoiceChatManager::cyclePlaybackDevice(int)
{
	return false;
}

wstring VoiceChatManager::getSelectedCaptureDeviceName() const
{
	return L"Default";
}

wstring VoiceChatManager::getSelectedPlaybackDeviceName() const
{
	return L"Default";
}

void VoiceChatManager::getCaptureDeviceNames(vector<wstring> &outNames, bool includeDefault) const
{
	outNames.clear();
	if (includeDefault)
	{
		outNames.push_back(L"Default");
	}
}

void VoiceChatManager::getPlaybackDeviceNames(vector<wstring> &outNames, bool includeDefault) const
{
	outNames.clear();
	if (includeDefault)
	{
		outNames.push_back(L"Default");
	}
}

int VoiceChatManager::getSelectedCaptureMenuIndex() const
{
	return 0;
}

int VoiceChatManager::getSelectedPlaybackMenuIndex() const
{
	return 0;
}

bool VoiceChatManager::selectCaptureMenuIndex(int)
{
	return false;
}

bool VoiceChatManager::selectPlaybackMenuIndex(int)
{
	return false;
}

void VoiceChatManager::setMicVolumePercent(int percent)
{
	m_micVolumePercent = percent;
}

void VoiceChatManager::setVoiceChatVolumePercent(int percent)
{
	m_voiceChatVolumePercent = percent;
}

void VoiceChatManager::setVoiceActivationGainPercent(int percent)
{
	m_voiceActivationGainPercent = percent;
}

void VoiceChatManager::setLocalMuted(bool muted)
{
	m_localMuted = muted;
}

void VoiceChatManager::toggleLocalMuted()
{
	m_localMuted = !m_localMuted;
}

void VoiceChatManager::markSpeaking(int entityId)
{
	m_speakingEntities[entityId] = 8;
}

bool VoiceChatManager::isEntitySpeaking(int entityId)
{
	auto it = m_speakingEntities.find(entityId);
	return it != m_speakingEntities.end() && it->second > 0;
}

void VoiceChatManager::clearSpeaking(int entityId)
{
	m_speakingEntities.erase(entityId);
}

void VoiceChatManager::tickSpeakingState()
{
	for (auto it = m_speakingEntities.begin(); it != m_speakingEntities.end(); )
	{
		if (--(it->second) <= 0)
		{
			it = m_speakingEntities.erase(it);
		}
		else
		{
			++it;
		}
	}
}

void VoiceChatManager::onCaptureAudio(ma_device *, void *, const void *, unsigned int)
{
}

void VoiceChatManager::onPlaybackAudio(ma_device *, void *, const void *, unsigned int)
{
}
#endif

void glReadPixels(int,int, int, int, int, int, ByteBuffer *)
{
}

void glClearDepth(double)
{
}

void glVertexPointer(int, int, int, int)
{
}

void glVertexPointer(int, int, FloatBuffer *)
{
}

void glTexCoordPointer(int, int, int, int)
{
}

void glTexCoordPointer(int, int, FloatBuffer *)
{
}

void glNormalPointer(int, int, int)
{
}

void glNormalPointer(int, ByteBuffer *)
{
}

void glEnableClientState(int)
{
}

void glDisableClientState(int)
{
}

void glColorPointer(int, int, int, int)
{
}

void glColorPointer(int, bool, int, ByteBuffer *)
{
}

void glDrawArrays(int,int,int)
{
}

void glNormal3f(float,float,float)
{
}

void glGenQueriesARB(IntBuffer *)
{
}

void glBeginQueryARB(int,int)
{
}

void glEndQueryARB(int)
{
}

void glGetQueryObjectuARB(int,int,IntBuffer *)
{
}

void glShadeModel(int)
{
}

void glColorMaterial(int,int)
{
}

//1.8.2
void glClientActiveTexture(int)
{
}

void glActiveTexture(int)
{
}

void glFlush()
{
}

void glTexGeni(int,int,int)
{
}

#ifdef _XBOX
// 4J Stu - Added these to stop us needing to pull in loads of media libraries just to use Qnet
#include <xcam.h>
DWORD XCamInitialize(){ return 0; }
VOID XCamShutdown() {}
 
DWORD XCamCreateStreamEngine(
         CONST XCAM_STREAM_ENGINE_INIT_PARAMS *pParams,
         PIXCAMSTREAMENGINE *ppEngine
		 ) { return 0; }
 
DWORD XCamSetView(
         XCAMZOOMFACTOR ZoomFactor,
         LONG XCenter,
         LONG YCenter,
         PXOVERLAPPED pOverlapped
) { return 0; }
 
XCAMDEVICESTATE XCamGetStatus() { return XCAMDEVICESTATE_DISCONNECTED; }
#endif
