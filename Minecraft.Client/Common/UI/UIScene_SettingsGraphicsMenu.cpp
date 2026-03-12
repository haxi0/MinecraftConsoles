#include "stdafx.h"
#include "UI.h"
#include "UIScene_SettingsGraphicsMenu.h"
#include "..\..\Minecraft.h"
#include "..\..\Options.h"
#include "..\..\GameRenderer.h"
#include "..\..\VoiceChatManager.h"

namespace
{
	static int s_voiceChatGraphicsInitData = 2;
    constexpr int FOV_MIN = 70;
    constexpr int FOV_MAX = 110;
    constexpr int FOV_SLIDER_MAX = 100;
	static const int MAX_VOICE_VOLUME_PERCENT = 200;
	static const int MIN_VOICE_ACTIVATION_GAIN_PERCENT = 25;
	static const int MAX_VOICE_ACTIVATION_GAIN_PERCENT = 400;
	static const int MAX_DEVICE_LABELS = 64;

	int ClampFov(int value)
	{
		if (value < FOV_MIN) return FOV_MIN;
		if (value > FOV_MAX) return FOV_MAX;
		return value;
	}

    [[maybe_unused]]
    int FovToSliderValue(float fov)
	{
		const int clampedFov = ClampFov(static_cast<int>(fov + 0.5f));
		return ((clampedFov - FOV_MIN) * FOV_SLIDER_MAX) / (FOV_MAX - FOV_MIN);
	}

	int sliderValueToFov(int sliderValue)
	{
		if (sliderValue < 0) sliderValue = 0;
		if (sliderValue > FOV_SLIDER_MAX) sliderValue = FOV_SLIDER_MAX;
		return FOV_MIN + ((sliderValue * (FOV_MAX - FOV_MIN)) / FOV_SLIDER_MAX);
	}

	static void BuildDeviceLabels(const vector<wstring> &deviceNames, const wchar_t *prefix, wchar_t labels[][256], int &count)
	{
		count = static_cast<int>(deviceNames.size());
		if (count <= 0)
		{
			count = 1;
			swprintf(labels[0], 256, L"%ls: Default", prefix);
			return;
		}
		if (count > MAX_DEVICE_LABELS)
		{
			count = MAX_DEVICE_LABELS;
		}

		for (int i = 0; i < count; ++i)
		{
			swprintf(labels[i], 256, L"%ls: %ls", prefix, deviceNames[i].c_str());
		}
	}

	[[maybe_unused]]
	static void SetControlY(UIScene &scene, UIControl &control, int y)
	{
		IggyName yName = scene.registerFastName(L"y");
		IggyValueSetF64RS(control.getIggyValuePath(), yName, nullptr, static_cast<F64>(y));
		control.UpdateControl();
	}

	[[maybe_unused]]
	static void SetControlHeight(UIScene &scene, UIControl &control, int height)
	{
		IggyName heightName = scene.registerFastName(L"height");
		IggyValueSetF64RS(control.getIggyValuePath(), heightName, nullptr, static_cast<F64>(height));
		control.UpdateControl();
	}

}

int UIScene_SettingsGraphicsMenu::LevelToDistance(int level)
{
	static const int table[6] = {2,4,8,16,32,64};
	if(level < 0) level = 0;
	if(level > 5) level = 5;
	return table[level];
}

int UIScene_SettingsGraphicsMenu::DistanceToLevel(int dist)
{
    static const int table[6] = {2,4,8,16,32,64};
    for(int i = 0; i < 6; i++){
        if(table[i] == dist)
            return i;
    }
    return 3;
}

UIScene_SettingsGraphicsMenu::UIScene_SettingsGraphicsMenu(int iPad, void *initData, UILayer *parentLayer)
	: UIScene(iPad, parentLayer)
	, m_bVoiceChatMode(false)
{
	// Setup all the Iggy references we need for this scene
	initialiseMovie();
	m_bVoiceChatMode = (initData != nullptr && *(static_cast<int *>(initData)) == s_voiceChatGraphicsInitData);
	if (m_bVoiceChatMode)
	{
		initVoiceChatSliders();
		return;
	}

	Minecraft* pMinecraft = Minecraft::GetInstance();
	
	m_bNotInGame=(Minecraft::GetInstance()->level==nullptr);

	m_checkboxClouds.init(app.GetString(IDS_CHECKBOX_RENDER_CLOUDS),eControl_Clouds,(app.GetGameSettings(m_iPad,eGameSetting_Clouds)!=0));
	m_checkboxBedrockFog.init(app.GetString(IDS_CHECKBOX_RENDER_BEDROCKFOG),eControl_BedrockFog,(app.GetGameSettings(m_iPad,eGameSetting_BedrockFog)!=0));
	m_checkboxCustomSkinAnim.init(app.GetString(IDS_CHECKBOX_CUSTOM_SKIN_ANIM),eControl_CustomSkinAnim,(app.GetGameSettings(m_iPad,eGameSetting_CustomSkinAnim)!=0));

	
	WCHAR TempString[256];

	swprintf(TempString, 256, L"Render Distance: %d",app.GetGameSettings(m_iPad,eGameSetting_RenderDistance));	
	m_sliderRenderDistance.init(TempString,eControl_RenderDistance,0,5,DistanceToLevel(app.GetGameSettings(m_iPad,eGameSetting_RenderDistance)));
	
	swprintf( TempString, 256, L"%ls: %d%%", app.GetString( IDS_SLIDER_GAMMA ),app.GetGameSettings(m_iPad,eGameSetting_Gamma));	
	m_sliderGamma.init(TempString,eControl_Gamma,0,100,app.GetGameSettings(m_iPad,eGameSetting_Gamma));

    const int initialFovSlider = app.GetGameSettings(m_iPad, eGameSetting_FOV);
	const int initialFovDeg = sliderValueToFov(initialFovSlider);
	swprintf(TempString, 256, L"FOV: %d", initialFovDeg);
	m_sliderFOV.init(TempString, eControl_FOV, 0, FOV_SLIDER_MAX, initialFovSlider);
	
	swprintf( TempString, 256, L"%ls: %d%%", app.GetString( IDS_SLIDER_INTERFACEOPACITY ),app.GetGameSettings(m_iPad,eGameSetting_InterfaceOpacity));	
	m_sliderInterfaceOpacity.init(TempString,eControl_InterfaceOpacity,0,100,app.GetGameSettings(m_iPad,eGameSetting_InterfaceOpacity));

	doHorizontalResizeCheck();

	const bool bInGame=(Minecraft::GetInstance()->level!=nullptr);
	const bool bIsPrimaryPad=(ProfileManager.GetPrimaryPad()==m_iPad);
	// if we're not in the game, we need to use basescene 0 
	if(bInGame)
	{
		// If the game has started, then you need to be the host to change the in-game gamertags
		if(bIsPrimaryPad)
		{	
			// we are the primary player on this machine, but not the game host
			// are we the game host? If not, we need to remove the bedrockfog setting
			if(!g_NetworkManager.IsHost())
			{
				// hide the in-game bedrock fog setting
				removeControl(&m_checkboxBedrockFog, true);
			}
		}
		else
		{
			// We shouldn't have the bedrock fog option, or the m_CustomSkinAnim option
			removeControl(&m_checkboxBedrockFog, true);
			removeControl(&m_checkboxCustomSkinAnim, true);
		}
	}

	if(app.GetLocalPlayerCount()>1)
	{
#if TO_BE_IMPLEMENTED
		app.AdjustSplitscreenScene(m_hObj,&m_OriginalPosition,m_iPad);
#endif
	}
}

void UIScene_SettingsGraphicsMenu::initVoiceChatSliders()
{
	VoiceChatManager &vcm = VoiceChatManager::getInstance();
	if (!vcm.isInitialized())
	{
		vcm.init();
	}

	const bool pushToTalkEnabled = vcm.getVoiceInputMode() == VoiceChatManager::VOICE_INPUT_PUSH_TO_TALK;
	const bool gainActivationEnabled = !pushToTalkEnabled;
	m_checkboxClouds.init(UIString(L"Gain Activation"), eControl_Clouds, gainActivationEnabled);
	m_checkboxBedrockFog.init(UIString(L"Push-To-Talk"), eControl_BedrockFog, pushToTalkEnabled);
	m_checkboxCustomSkinAnim.init(UIString(L"Select Input Device"), eControl_CustomSkinAnim, false);
	refreshVoiceDeviceCheckboxLabel();
	enforceVoiceModeSwitch(-1);
	applyVoiceModeFromCheckboxes();

	WCHAR tempString[256];

	int micVolume = vcm.getMicVolumePercent();
	if (micVolume < 0) micVolume = 0;
	if (micVolume > MAX_VOICE_VOLUME_PERCENT) micVolume = MAX_VOICE_VOLUME_PERCENT;
	swprintf(tempString, 256, L"Mic Volume: %d%%", micVolume);
	m_sliderRenderDistance.init(tempString, eControl_RenderDistance, 0, MAX_VOICE_VOLUME_PERCENT, micVolume);

	int voiceVolume = vcm.getVoiceChatVolumePercent();
	if (voiceVolume < 0) voiceVolume = 0;
	if (voiceVolume > MAX_VOICE_VOLUME_PERCENT) voiceVolume = MAX_VOICE_VOLUME_PERCENT;
	swprintf(tempString, 256, L"Voice Chat Volume: %d%%", voiceVolume);
	m_sliderGamma.init(tempString, eControl_Gamma, 0, MAX_VOICE_VOLUME_PERCENT, voiceVolume);

	int voiceActivationGain = vcm.getVoiceActivationGainPercent();
	if (voiceActivationGain < MIN_VOICE_ACTIVATION_GAIN_PERCENT) voiceActivationGain = MIN_VOICE_ACTIVATION_GAIN_PERCENT;
	if (voiceActivationGain > MAX_VOICE_ACTIVATION_GAIN_PERCENT) voiceActivationGain = MAX_VOICE_ACTIVATION_GAIN_PERCENT;
	swprintf(tempString, 256, L"Activation Gain: %d%%", voiceActivationGain);
	m_sliderFOV.init(tempString, eControl_FOV, MIN_VOICE_ACTIVATION_GAIN_PERCENT, MAX_VOICE_ACTIVATION_GAIN_PERCENT, voiceActivationGain);

	refreshVoiceDeviceSlider();

	doHorizontalResizeCheck();
}

UIScene_SettingsGraphicsMenu::~UIScene_SettingsGraphicsMenu()
{
}

void UIScene_SettingsGraphicsMenu::tick()
{
	UIScene::tick();
}

wstring UIScene_SettingsGraphicsMenu::getMoviePath()
{
	if(app.GetLocalPlayerCount() > 1)
	{
		return L"SettingsGraphicsMenuSplit";
	}
	else
	{
		return L"SettingsGraphicsMenu";
	}
}

void UIScene_SettingsGraphicsMenu::updateTooltips()
{
	ui.SetTooltips( m_iPad, IDS_TOOLTIPS_SELECT,IDS_TOOLTIPS_BACK);
}

void UIScene_SettingsGraphicsMenu::updateComponents()
{
	const bool bNotInGame=(Minecraft::GetInstance()->level==nullptr);
	if(bNotInGame)
	{
		m_parentLayer->showComponent(m_iPad,eUIComponent_Panorama,true);
		m_parentLayer->showComponent(m_iPad,eUIComponent_Logo,true);
	}
	else
	{
		m_parentLayer->showComponent(m_iPad,eUIComponent_Panorama,false);
	
		if( app.GetLocalPlayerCount() == 1 ) m_parentLayer->showComponent(m_iPad,eUIComponent_Logo,true);
		else m_parentLayer->showComponent(m_iPad,eUIComponent_Logo,false);

	}
}

void UIScene_SettingsGraphicsMenu::handleInput(int iPad, int key, bool repeat, bool pressed, bool released, bool &handled)
{
	ui.AnimateKeyPress(iPad, key, repeat, pressed, released);
	switch(key)
	{
	case ACTION_MENU_CANCEL:
		if(pressed)
		{
			if (m_bVoiceChatMode)
			{
				applyVoiceModeFromCheckboxes();
				navigateBack();
				handled = true;
				break;
			}

			// check the checkboxes
			app.SetGameSettings(m_iPad,eGameSetting_Clouds,m_checkboxClouds.IsChecked()?1:0);
			app.SetGameSettings(m_iPad,eGameSetting_BedrockFog,m_checkboxBedrockFog.IsChecked()?1:0);
			app.SetGameSettings(m_iPad,eGameSetting_CustomSkinAnim,m_checkboxCustomSkinAnim.IsChecked()?1:0);

			navigateBack();
			handled = true;
		}
		break;
	case ACTION_MENU_OK:
#ifdef __ORBIS__
	case ACTION_MENU_TOUCHPAD_PRESS:
#endif
		sendInputToMovie(key, repeat, pressed, released);
		break;
	case ACTION_MENU_UP:
	case ACTION_MENU_DOWN:
	case ACTION_MENU_LEFT:
	case ACTION_MENU_RIGHT:
		sendInputToMovie(key, repeat, pressed, released);
		break;
	}
}

void UIScene_SettingsGraphicsMenu::handleSliderMove(F64 sliderId, F64 currentValue)
{
	WCHAR TempString[256];
	const int value = static_cast<int>(currentValue);
	if (m_bVoiceChatMode)
	{
		handleVoiceSliderMove(static_cast<int>(sliderId), value);
		return;
	}

	switch(static_cast<int>(sliderId))
	{
	case eControl_RenderDistance:
		{
			m_sliderRenderDistance.handleSliderMove(value);

			const int dist = LevelToDistance(value);

			app.SetGameSettings(m_iPad,eGameSetting_RenderDistance,dist);

			const Minecraft* mc = Minecraft::GetInstance();
			mc->options->viewDistance = 3 - value;
			swprintf(TempString,256,L"Render Distance: %d",dist);
			m_sliderRenderDistance.setLabel(TempString);
		}
		break;

	case eControl_Gamma:
		m_sliderGamma.handleSliderMove(value);
		
		app.SetGameSettings(m_iPad,eGameSetting_Gamma,value);
		swprintf( TempString, 256, L"%ls: %d%%", app.GetString( IDS_SLIDER_GAMMA ),value);
		m_sliderGamma.setLabel(TempString);

		break;

	case eControl_FOV:
		{
			m_sliderFOV.handleSliderMove(value);
			const Minecraft* pMinecraft = Minecraft::GetInstance();
			const int fovValue = sliderValueToFov(value);
			pMinecraft->gameRenderer->SetFovVal(static_cast<float>(fovValue));
			app.SetGameSettings(m_iPad, eGameSetting_FOV, value);
			WCHAR tempString[256];
			swprintf(tempString, 256, L"FOV: %d", fovValue);
			m_sliderFOV.setLabel(tempString);
		}
		break;

	case eControl_InterfaceOpacity:
		m_sliderInterfaceOpacity.handleSliderMove(value);
		
		app.SetGameSettings(m_iPad,eGameSetting_InterfaceOpacity,value);
		swprintf( TempString, 256, L"%ls: %d%%", app.GetString( IDS_SLIDER_INTERFACEOPACITY ),value);	
		m_sliderInterfaceOpacity.setLabel(TempString);

		break;
	}
}

void UIScene_SettingsGraphicsMenu::handleCheckboxToggled(F64 controlId, bool selected)
{
	if (!m_bVoiceChatMode)
	{
		return;
	}

	switch (static_cast<int>(controlId))
	{
	case eControl_Clouds:
		m_checkboxClouds.setChecked(selected);
		enforceVoiceModeSwitch(eControl_Clouds);
		applyVoiceModeFromCheckboxes();
		break;

	case eControl_BedrockFog:
		m_checkboxBedrockFog.setChecked(selected);
		enforceVoiceModeSwitch(eControl_BedrockFog);
		applyVoiceModeFromCheckboxes();
		break;

	case eControl_CustomSkinAnim:
		m_checkboxCustomSkinAnim.setChecked(selected);
		refreshVoiceDeviceCheckboxLabel();
		refreshVoiceDeviceSlider();
		break;
	}
}

void UIScene_SettingsGraphicsMenu::handleVoiceSliderMove(int controlId, int value)
{
	VoiceChatManager &vcm = VoiceChatManager::getInstance();
	WCHAR tempString[256];
	switch (controlId)
	{
	case eControl_RenderDistance:
		vcm.setMicVolumePercent(value);
		m_sliderRenderDistance.handleSliderMove(value);
		swprintf(tempString, 256, L"Mic Volume: %d%%", value);
		m_sliderRenderDistance.setLabel(tempString);
		break;

	case eControl_Gamma:
		vcm.setVoiceChatVolumePercent(value);
		m_sliderGamma.handleSliderMove(value);
		swprintf(tempString, 256, L"Voice Chat Volume: %d%%", value);
		m_sliderGamma.setLabel(tempString);
		break;

	case eControl_FOV:
		vcm.setVoiceActivationGainPercent(value);
		m_sliderFOV.handleSliderMove(value);
		swprintf(tempString, 256, L"Activation Gain: %d%%", value);
		m_sliderFOV.setLabel(tempString);
		break;

	case eControl_InterfaceOpacity:
		if (m_checkboxCustomSkinAnim.IsChecked())
		{
			if (vcm.selectPlaybackMenuIndex(value))
			{
				refreshVoiceDeviceSlider();
			}
		}
		else
		{
			if (vcm.selectCaptureMenuIndex(value))
			{
				refreshVoiceDeviceSlider();
			}
		}
		break;
	}
}

void UIScene_SettingsGraphicsMenu::enforceVoiceModeSwitch(int preferredControl)
{
	(void)preferredControl;

	if (!m_bVoiceChatMode)
	{
		return;
	}

	bool proximity = m_checkboxClouds.IsChecked();
	bool pushToTalk = m_checkboxBedrockFog.IsChecked();

	if (proximity == pushToTalk)
	{
		if (preferredControl == eControl_BedrockFog)
		{
			proximity = false;
			pushToTalk = true;
		}
		else
		{
			proximity = true;
			pushToTalk = false;
		}
	}

	m_checkboxClouds.setChecked(proximity);
	m_checkboxBedrockFog.setChecked(pushToTalk);
}

void UIScene_SettingsGraphicsMenu::applyVoiceModeFromCheckboxes()
{
	VoiceChatManager &vcm = VoiceChatManager::getInstance();
	enforceVoiceModeSwitch(-1);
	vcm.setProximityEnabled(true);
	vcm.setVoiceInputMode(m_checkboxBedrockFog.IsChecked()
		? VoiceChatManager::VOICE_INPUT_PUSH_TO_TALK
		: VoiceChatManager::VOICE_INPUT_VOICE_ACTIVATION);
}

void UIScene_SettingsGraphicsMenu::refreshVoiceDeviceCheckboxLabel()
{
	const bool usePlaybackDevice = m_checkboxCustomSkinAnim.IsChecked();
	m_checkboxCustomSkinAnim.setLabel(usePlaybackDevice ? UIString(L"Select Output Device") : UIString(L"Select Input Device"), true, true);
}

void UIScene_SettingsGraphicsMenu::refreshVoiceDeviceSlider()
{
	VoiceChatManager &vcm = VoiceChatManager::getInstance();
	const bool usePlaybackDevice = m_checkboxCustomSkinAnim.IsChecked();

	vector<wstring> deviceNames;
	if (usePlaybackDevice)
	{
		vcm.getPlaybackDeviceNames(deviceNames, true);
	}
	else
	{
		vcm.getCaptureDeviceNames(deviceNames, true);
	}

	wchar_t deviceLabels[MAX_DEVICE_LABELS][256];
	int deviceCount = 0;
	BuildDeviceLabels(deviceNames, usePlaybackDevice ? L"Output" : L"Input", deviceLabels, deviceCount);

	int selectedIndex = usePlaybackDevice ? vcm.getSelectedPlaybackMenuIndex() : vcm.getSelectedCaptureMenuIndex();
	if (selectedIndex < 0) selectedIndex = 0;
	if (selectedIndex >= deviceCount) selectedIndex = 0;

	m_sliderInterfaceOpacity.setAllPossibleLabels(deviceCount, deviceLabels);
	m_sliderInterfaceOpacity.init(deviceLabels[selectedIndex], eControl_InterfaceOpacity, 0, deviceCount - 1, selectedIndex);
}
