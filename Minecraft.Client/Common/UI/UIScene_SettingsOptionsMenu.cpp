#include "stdafx.h"
#include "UI.h"
#include "UIScene_SettingsOptionsMenu.h"
#include "..\..\VoiceChatManager.h"

#if defined(_XBOX_ONE)
#define _ENABLE_LANGUAGE_SELECT
#endif

int UIScene_SettingsOptionsMenu::m_iDifficultySettingA[4]=
{
	IDS_DIFFICULTY_PEACEFUL,
	IDS_DIFFICULTY_EASY,
	IDS_DIFFICULTY_NORMAL,
	IDS_DIFFICULTY_HARD
};

int UIScene_SettingsOptionsMenu::m_iDifficultyTitleSettingA[4]=
{
	IDS_DIFFICULTY_TITLE_PEACEFUL,
	IDS_DIFFICULTY_TITLE_EASY,
	IDS_DIFFICULTY_TITLE_NORMAL,
	IDS_DIFFICULTY_TITLE_HARD
};

namespace
{
	static int s_voiceChatMenuInitData = 1;
	static const int MAX_VOICE_VOLUME_PERCENT = 200;

	static void SetControlY(UIScene &scene, UIControl &control, int y)
	{
		IggyName yName = scene.registerFastName(L"y");
		IggyValueSetF64RS(control.getIggyValuePath(), yName, nullptr, static_cast<F64>(y));
		control.UpdateControl();
	}
}

UIScene_SettingsOptionsMenu::UIScene_SettingsOptionsMenu(int iPad, void *initData, UILayer *parentLayer) : UIScene(iPad, parentLayer)
{
	m_bVoiceChatMode = (initData != nullptr && *(static_cast<int *>(initData)) == s_voiceChatMenuInitData);

	// Setup all the Iggy references we need for this scene
	initialiseMovie();

	if (m_bVoiceChatMode)
	{
		setupVoiceChatMenu();
	}
	else
	{
		setupStandardOptionsMenu();
	}
}

UIScene_SettingsOptionsMenu::~UIScene_SettingsOptionsMenu()
{
}

void UIScene_SettingsOptionsMenu::setupVoiceChatMenu()
{
	VoiceChatManager &vcm = VoiceChatManager::getInstance();
	if (!vcm.isInitialized())
	{
		vcm.init();
	}

	m_bNotInGame = (Minecraft::GetInstance()->level == nullptr);
	m_bMashUpWorldsUnhideOption = false;

	const bool pushToTalkEnabled = vcm.getVoiceInputMode() == VoiceChatManager::VOICE_INPUT_PUSH_TO_TALK;
	const bool gainActivationEnabled = !pushToTalkEnabled;
	m_checkboxViewBob.init(UIString(L"Gain Activation"), eControl_ViewBob, gainActivationEnabled);
	m_checkboxShowHints.init(UIString(L"Push-To-Talk"), eControl_ShowHints, pushToTalkEnabled);

	removeControl(&m_checkboxShowTooltips, true);
	removeControl(&m_checkboxInGameGamertags, true);
	removeControl(&m_checkboxMashupWorlds, true);
	removeControl(&m_labelDifficultyText, true);

	static wchar_t micVolumeLabels[MAX_VOICE_VOLUME_PERCENT + 1][256];
	static wchar_t voiceVolumeLabels[MAX_VOICE_VOLUME_PERCENT + 1][256];
	for (int i = 0; i <= MAX_VOICE_VOLUME_PERCENT; ++i)
	{
		swprintf(micVolumeLabels[i], 256, L"Mic Volume: %d%%", i);
		swprintf(voiceVolumeLabels[i], 256, L"Voice Chat Volume: %d%%", i);
	}

	int micVolume = vcm.getMicVolumePercent();
	int voiceVolume = vcm.getVoiceChatVolumePercent();
	if (micVolume < 0) micVolume = 0;
	if (micVolume > MAX_VOICE_VOLUME_PERCENT) micVolume = MAX_VOICE_VOLUME_PERCENT;
	if (voiceVolume < 0) voiceVolume = 0;
	if (voiceVolume > MAX_VOICE_VOLUME_PERCENT) voiceVolume = MAX_VOICE_VOLUME_PERCENT;

	m_sliderAutosave.setAllPossibleLabels(MAX_VOICE_VOLUME_PERCENT + 1, micVolumeLabels);
	m_sliderAutosave.init(micVolumeLabels[micVolume], eControl_Autosave, 0, MAX_VOICE_VOLUME_PERCENT, micVolume);
	m_sliderDifficulty.setAllPossibleLabels(MAX_VOICE_VOLUME_PERCENT + 1, voiceVolumeLabels);
	m_sliderDifficulty.init(voiceVolumeLabels[voiceVolume], eControl_Difficulty, 0, MAX_VOICE_VOLUME_PERCENT, voiceVolume);

	m_buttonLanguageSelect.init(UIString(L"Back"), eControl_Languages);

	m_labelDifficultyText.setVisible(false);
	placeVoiceBackButtonAtBottom();

	doHorizontalResizeCheck();

	if (app.GetLocalPlayerCount() > 1)
	{
#if TO_BE_IMPLEMENTED
		app.AdjustSplitscreenScene(m_hObj,&m_OriginalPosition,m_iPad);
#endif
	}

	m_labelDifficultyText.disableReinitialisation();
}

void UIScene_SettingsOptionsMenu::setupStandardOptionsMenu()
{
	m_bNotInGame=(Minecraft::GetInstance()->level==nullptr);

	m_checkboxViewBob.init(IDS_VIEW_BOBBING,eControl_ViewBob,(app.GetGameSettings(m_iPad,eGameSetting_ViewBob)!=0));
	m_checkboxShowHints.init(IDS_HINTS,eControl_ShowHints,(app.GetGameSettings(m_iPad,eGameSetting_Hints)!=0));
	m_checkboxShowTooltips.init(IDS_IN_GAME_TOOLTIPS,eControl_ShowTooltips,(app.GetGameSettings(m_iPad,eGameSetting_Tooltips)!=0));
	m_checkboxInGameGamertags.init(IDS_IN_GAME_GAMERTAGS,eControl_InGameGamertags,(app.GetGameSettings(m_iPad,eGameSetting_GamertagsVisible)!=0));

	// check if we should display the mash-up option
	if(m_bNotInGame && app.GetMashupPackWorlds(m_iPad)!=0xFFFFFFFF)
	{
		// the mash-up option is needed
		m_bMashUpWorldsUnhideOption=true;
		m_checkboxMashupWorlds.init(IDS_UNHIDE_MASHUP_WORLDS,eControl_ShowMashUpWorlds,false);
	}
	else
	{
		removeControl(&m_checkboxMashupWorlds, true);
		m_bMashUpWorldsUnhideOption=false;
	}

	unsigned char ucValue=app.GetGameSettings(m_iPad,eGameSetting_Autosave);

	wchar_t autosaveLabels[9][256];
	for(unsigned int i = 0; i < 9; ++i)
	{
		if(i==0)
		{
			swprintf( autosaveLabels[i], 256, L"%ls", app.GetString( IDS_SLIDER_AUTOSAVE_OFF ));
		}
		else
		{
			swprintf( autosaveLabels[i], 256, L"%ls: %d %ls", app.GetString( IDS_SLIDER_AUTOSAVE ),i*15, app.GetString( IDS_MINUTES ));
		}
	}
	m_sliderAutosave.setAllPossibleLabels(9,autosaveLabels);
	m_sliderAutosave.init(autosaveLabels[ucValue],eControl_Autosave,0,8,ucValue);

#if defined(_XBOX_ONE) || defined(__ORBIS__)
	removeControl(&m_sliderAutosave,true);
#endif

	ucValue = app.GetGameSettings(m_iPad,eGameSetting_Difficulty);
	wchar_t difficultyLabels[4][256];
	for(unsigned int i = 0; i < 4; ++i)
	{
		swprintf( difficultyLabels[i], 256, L"%ls: %ls", app.GetString( IDS_SLIDER_DIFFICULTY ),app.GetString(m_iDifficultyTitleSettingA[i]));
	}
	m_sliderDifficulty.setAllPossibleLabels(4,difficultyLabels);
	m_sliderDifficulty.init(difficultyLabels[ucValue],eControl_Difficulty,0,3,ucValue);

	wstring wsText=app.GetString(m_iDifficultySettingA[app.GetGameSettings(m_iPad,eGameSetting_Difficulty)]);
	wchar_t startTags[64];
	swprintf(startTags,64,L"<font color=\"#%08x\">",app.GetHTMLColour(eHTMLColor_White));
	wsText= startTags + wsText;
	m_labelDifficultyText.init(wsText);

	// If you are in-game, only the game host can change in-game gamertags, and you can't change difficulty
	// only the primary player gets to change the autosave and difficulty settings
	bool bRemoveDifficulty=false;
	bool bRemoveAutosave=false;
	bool bRemoveInGameGamertags=false;

	bool bNotInGame=(Minecraft::GetInstance()->level==nullptr);
	bool bPrimaryPlayer = ProfileManager.GetPrimaryPad()==m_iPad;
	if(!bPrimaryPlayer)
	{
		bRemoveDifficulty=true;
		bRemoveAutosave=true;
		bRemoveInGameGamertags=true;
	}

	if(!bNotInGame)
	{
		bRemoveDifficulty=true;
		if(!g_NetworkManager.IsHost())
		{
			bRemoveAutosave=true;
			bRemoveInGameGamertags=true;
		}
	}
	if(bRemoveDifficulty)
	{
		m_labelDifficultyText.setVisible( false );
		removeControl(&m_sliderDifficulty, true);
	}

	if(bRemoveAutosave)
	{
		removeControl(&m_sliderAutosave, true);
	}

	if(bRemoveInGameGamertags)
	{
		removeControl(&m_checkboxInGameGamertags, true);
	}

	// 4J-JEV: Changing languages in-game will produce many a bug.
	// MGH - disabled the language select for the patch build, we'll re-enable afterwards
	// 4J Stu - Removed it with a preprocessor def as we turn this off in various places
#ifdef _ENABLE_LANGUAGE_SELECT
	if (app.GetGameStarted())
	{
		removeControl( &m_buttonLanguageSelect, false );
	}
	else
	{
		m_buttonLanguageSelect.init(IDS_LANGUAGE_SELECTOR, eControl_Languages);
	}
#else
	removeControl( &m_buttonLanguageSelect, false );
#endif

	doHorizontalResizeCheck();

	if(app.GetLocalPlayerCount()>1)
	{
#if TO_BE_IMPLEMENTED
		app.AdjustSplitscreenScene(m_hObj,&m_OriginalPosition,m_iPad);
#endif
	}

	m_labelDifficultyText.disableReinitialisation();
}

void UIScene_SettingsOptionsMenu::setVoiceDifficultyLabel()
{
	m_labelDifficultyText.setVisible(false);
}

void UIScene_SettingsOptionsMenu::enforceVoiceModeSwitch(int preferredControl)
{
	(void)preferredControl;

	if (!m_bVoiceChatMode)
	{
		return;
	}

	bool proximity = m_checkboxViewBob.IsChecked();
	bool pushToTalk = m_checkboxShowHints.IsChecked();

	if (proximity == pushToTalk)
	{
		if (preferredControl == eControl_ShowHints)
		{
			proximity = false;
			pushToTalk = true;
		}
		else if (preferredControl == eControl_ViewBob)
		{
			proximity = true;
			pushToTalk = false;
		}
		else
		{
			VoiceChatManager &vcm = VoiceChatManager::getInstance();
			const bool preferPushToTalk = vcm.getVoiceInputMode() == VoiceChatManager::VOICE_INPUT_PUSH_TO_TALK;
			proximity = !preferPushToTalk;
			pushToTalk = preferPushToTalk;
		}
	}

	m_checkboxViewBob.setChecked(proximity);
	m_checkboxShowHints.setChecked(pushToTalk);
}

void UIScene_SettingsOptionsMenu::placeVoiceBackButtonAtBottom()
{
	if (!m_bVoiceChatMode)
	{
		return;
	}

	m_sliderAutosave.UpdateControl();
	m_sliderDifficulty.UpdateControl();
	m_buttonLanguageSelect.UpdateControl();
	m_checkboxViewBob.UpdateControl();
	m_checkboxShowHints.UpdateControl();

	const int rowSpacing = 8;
	const int bottomPadding = 6;
	const int controlsTop = m_checkboxViewBob.getYPos() + m_checkboxViewBob.getHeight();
	const int hintsBottom = m_checkboxShowHints.getYPos() + m_checkboxShowHints.getHeight();
	const int minAutosaveY = ((controlsTop > hintsBottom) ? controlsTop : hintsBottom) + rowSpacing;
	int autosaveY = m_sliderAutosave.getYPos();
	if (autosaveY < minAutosaveY)
	{
		autosaveY = minAutosaveY;
	}

	int difficultyY = autosaveY + m_sliderAutosave.getHeight() + rowSpacing;
	int backY = difficultyY + m_sliderDifficulty.getHeight() + rowSpacing;

	UIControl *pPanel = m_buttonLanguageSelect.getParentPanel();
	if (pPanel != nullptr)
	{
		pPanel->UpdateControl();
		const int maxBackY = pPanel->getHeight() - m_buttonLanguageSelect.getHeight() - bottomPadding;
		if (backY > maxBackY)
		{
			const int overflow = backY - maxBackY;
			autosaveY -= overflow;
			if (autosaveY < minAutosaveY)
			{
				autosaveY = minAutosaveY;
			}
			difficultyY = autosaveY + m_sliderAutosave.getHeight() + rowSpacing;
			backY = difficultyY + m_sliderDifficulty.getHeight() + rowSpacing;
			if (backY > maxBackY)
			{
				backY = maxBackY;
			}
		}
	}

	SetControlY(*this, m_sliderAutosave, autosaveY);
	SetControlY(*this, m_sliderDifficulty, difficultyY);
	SetControlY(*this, m_buttonLanguageSelect, backY);
}

void UIScene_SettingsOptionsMenu::tick()
{
	UIScene::tick();
	if (m_bVoiceChatMode)
	{
		syncVoiceModeCheckboxesFromManager();
	}
}

wstring UIScene_SettingsOptionsMenu::getMoviePath()
{
	if(app.GetLocalPlayerCount() > 1)
	{
		return L"SettingsOptionsMenuSplit";
	}
	else
	{
		return L"SettingsOptionsMenu";
	}
}

void UIScene_SettingsOptionsMenu::updateTooltips()
{
	ui.SetTooltips( m_iPad, IDS_TOOLTIPS_SELECT,IDS_TOOLTIPS_BACK);
}

void UIScene_SettingsOptionsMenu::updateComponents()
{
	bool bNotInGame=(Minecraft::GetInstance()->level==nullptr);
	if(bNotInGame)
	{
		m_parentLayer->showComponent(m_iPad,eUIComponent_Panorama,true);
		m_parentLayer->showComponent(m_iPad,eUIComponent_Logo,true);
	}
	else
	{
		m_parentLayer->showComponent(m_iPad,eUIComponent_Panorama,false);

		if( app.GetLocalPlayerCount() == 1 ) m_parentLayer->showComponent(m_iPad,eUIComponent_Logo,RenderManager.IsHiDef());
		else m_parentLayer->showComponent(m_iPad,eUIComponent_Logo,false);
	}
}

void UIScene_SettingsOptionsMenu::handleInput(int iPad, int key, bool repeat, bool pressed, bool released, bool &handled)
{
	ui.AnimateKeyPress(iPad, key, repeat, pressed, released);
	switch(key)
	{
	case ACTION_MENU_CANCEL:
		if(pressed)
		{
			setGameSettings();
			navigateBack();
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

void UIScene_SettingsOptionsMenu::handlePress(F64 controlId, F64 childId)
{
	//CD - Added for audio
	ui.PlayUISFX(eSFX_Press);

	switch(static_cast<int>(controlId))
	{
	case eControl_ViewBob:
		if (m_bVoiceChatMode)
		{
			break;
		}
		break;
	case eControl_ShowHints:
		if (m_bVoiceChatMode)
		{
			break;
		}
		break;
	case eControl_Languages:
		if (m_bVoiceChatMode)
		{
			setGameSettings();
			navigateBack();
		}
		else
		{
			setGameSettings();
			ui.NavigateToScene(m_iPad, eUIScene_LanguageSelector);
		}
		break;
	}
}

void UIScene_SettingsOptionsMenu::handleCheckboxToggled(F64 controlId, bool selected)
{
	if (!m_bVoiceChatMode)
	{
		return;
	}

	switch (static_cast<int>(controlId))
	{
	case eControl_ViewBob:
		{
			VoiceChatManager &vcm = VoiceChatManager::getInstance();
			vcm.setProximityEnabled(true);
			vcm.setVoiceInputMode(selected
				? VoiceChatManager::VOICE_INPUT_VOICE_ACTIVATION
				: VoiceChatManager::VOICE_INPUT_PUSH_TO_TALK);
			syncVoiceModeCheckboxesFromManager();
		}
		break;
	case eControl_ShowHints:
		{
			VoiceChatManager &vcm = VoiceChatManager::getInstance();
			vcm.setProximityEnabled(true);
			vcm.setVoiceInputMode(selected
				? VoiceChatManager::VOICE_INPUT_PUSH_TO_TALK
				: VoiceChatManager::VOICE_INPUT_VOICE_ACTIVATION);
			syncVoiceModeCheckboxesFromManager();
		}
		break;
	}
}

void UIScene_SettingsOptionsMenu::handleReload()
{
	if (m_bVoiceChatMode)
	{
		setupVoiceChatMenu();
	}
	else
	{
		setupStandardOptionsMenu();
	}
}

void UIScene_SettingsOptionsMenu::handleSliderMove(F64 sliderId, F64 currentValue)
{
	int value = static_cast<int>(currentValue);

	if (m_bVoiceChatMode)
	{
		VoiceChatManager &vcm = VoiceChatManager::getInstance();
		WCHAR TempString[256];
		switch(static_cast<int>(sliderId))
		{
		case eControl_Autosave:
			vcm.setMicVolumePercent(value);
			swprintf(TempString, 256, L"Mic Volume: %d%%", value);
			m_sliderAutosave.setLabel(TempString);
			m_sliderAutosave.handleSliderMove(value);
			break;
		case eControl_Difficulty:
			vcm.setVoiceChatVolumePercent(value);
			swprintf(TempString, 256, L"Voice Chat Volume: %d%%", value);
			m_sliderDifficulty.setLabel(TempString);
			m_sliderDifficulty.handleSliderMove(value);
			break;
		}
		return;
	}

	switch(static_cast<int>(sliderId))
	{
	case eControl_Autosave:
		m_sliderAutosave.handleSliderMove(value);

		app.SetGameSettings(m_iPad,eGameSetting_Autosave,value);
		// Update the autosave timer
		app.SetAutosaveTimerTime();

		break;
	case eControl_Difficulty:
		m_sliderDifficulty.handleSliderMove(value);

		app.SetGameSettings(m_iPad,eGameSetting_Difficulty,value);
		
		wstring wsText=app.GetString(m_iDifficultySettingA[value]);
		EHTMLFontSize size = eHTMLSize_Normal;
		if(!RenderManager.IsHiDef() && !RenderManager.IsWidescreen())
		{
			size = eHTMLSize_Splitscreen;
		}
		wchar_t startTags[64];
		swprintf(startTags,64,L"<font color=\"#%08x\">",app.GetHTMLColour(eHTMLColor_White));
		wsText= startTags + wsText;
		m_labelDifficultyText.setLabel(wsText.c_str());
		break;
	}
}

void UIScene_SettingsOptionsMenu::setGameSettings()
{
	if (m_bVoiceChatMode)
	{
		VoiceChatManager &vcm = VoiceChatManager::getInstance();
		const bool gainActivation = m_checkboxViewBob.IsChecked();
		const bool pushToTalk = m_checkboxShowHints.IsChecked();
		if (gainActivation != pushToTalk)
		{
			vcm.setVoiceInputMode(pushToTalk
				? VoiceChatManager::VOICE_INPUT_PUSH_TO_TALK
				: VoiceChatManager::VOICE_INPUT_VOICE_ACTIVATION);
		}
		vcm.setProximityEnabled(true);
		syncVoiceModeCheckboxesFromManager();
		return;
	}

	// check the checkboxes
	app.SetGameSettings(m_iPad,eGameSetting_ViewBob,m_checkboxViewBob.IsChecked()?1:0);
	app.SetGameSettings(m_iPad,eGameSetting_GamertagsVisible,m_checkboxInGameGamertags.IsChecked()?1:0);
	app.SetGameSettings(m_iPad,eGameSetting_Hints,m_checkboxShowHints.IsChecked()?1:0);
	app.SetGameSettings(m_iPad,eGameSetting_Tooltips,m_checkboxShowTooltips.IsChecked()?1:0);

	// the mashup option will only be shown if some worlds have been previously hidden
	if(m_bMashUpWorldsUnhideOption && m_checkboxMashupWorlds.IsChecked())
	{
		// unhide all worlds
		app.EnableMashupPackWorlds(m_iPad);
	}

	// 4J-PB - don't action changes here or we might write to the profile on backing out here and then get a change in the settings all, and write again on backing out there
	//app.CheckGameSettingsChanged(true,pInputData->UserIndex);
}

void UIScene_SettingsOptionsMenu::syncVoiceModeCheckboxesFromManager()
{
	if (!m_bVoiceChatMode)
	{
		return;
	}

	VoiceChatManager &vcm = VoiceChatManager::getInstance();
	const bool pushToTalk = vcm.getVoiceInputMode() == VoiceChatManager::VOICE_INPUT_PUSH_TO_TALK;
	const bool gainActivation = !pushToTalk;

	if (m_checkboxViewBob.IsChecked() != gainActivation)
	{
		m_checkboxViewBob.setChecked(gainActivation);
	}
	if (m_checkboxShowHints.IsChecked() != pushToTalk)
	{
		m_checkboxShowHints.setChecked(pushToTalk);
	}
}
