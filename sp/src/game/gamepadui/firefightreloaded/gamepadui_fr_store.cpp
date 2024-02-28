#include "gamepadui_interface.h"
#include "gamepadui_image.h"
#include "gamepadui_util.h"
#include "gamepadui_frame.h"
#include "gamepadui_scroll.h"
#include "gamepadui_genericconfirmation.h"

#include "ienginevgui.h"
#include "vgui/ILocalize.h"
#include "vgui/ISurface.h"
#include "vgui/IVGui.h"

#include "vgui_controls/ComboBox.h"
#include "vgui_controls/ImagePanel.h"
#include "vgui_controls/ScrollBar.h"

#include "KeyValues.h"
#include "filesystem.h"

#include "icommandline.h"

#include "tier0/memdbgon.h"

class GamepadUIStoreButton;

#define GAMEPADUI_MAP_SCHEME GAMEPADUI_RESOURCE_FOLDER "schemechapterbutton.res"

class GamepadUIStore : public GamepadUIFrame
{
    DECLARE_CLASS_SIMPLE( GamepadUIStore, GamepadUIFrame );

public:
    GamepadUIStore( vgui::Panel *pParent, const char* pPanelName );

    void UpdateGradients();

    void OnThink() OVERRIDE;
    void ApplySchemeSettings(vgui::IScheme* pScheme) OVERRIDE;
    void OnCommand( char const* pCommand ) OVERRIDE;
    void LoadItemFile(const char* kvName, const char* scriptPath);
    void CreateItemList();
    void UpdateFrameTitle();

    MESSAGE_FUNC_HANDLE( OnGamepadUIButtonNavigatedTo, "OnGamepadUIButtonNavigatedTo", button );

    void LayoutMapButtons();

    void OnMouseWheeled( int delta ) OVERRIDE;

private:
    CUtlVector<KeyValues*> m_pItems;
    CUtlVector< GamepadUIStoreButton* > m_pChapterButtons;

    GamepadUIScrollState m_ScrollState;

    GAMEPADUI_PANEL_PROPERTY( float, m_ChapterOffsetX, "Chapters.OffsetX", "0", SchemeValueTypes::ProportionalFloat );
    GAMEPADUI_PANEL_PROPERTY( float, m_ChapterOffsetY, "Chapters.OffsetY", "0", SchemeValueTypes::ProportionalFloat );
    GAMEPADUI_PANEL_PROPERTY( float, m_ChapterSpacing, "Chapters.Spacing", "0", SchemeValueTypes::ProportionalFloat );

    bool m_bCommentaryMode = false;
};

class GamepadUIStoreButton : public GamepadUIButton
{
public:
    DECLARE_CLASS_SIMPLE( GamepadUIStoreButton, GamepadUIButton );

    GamepadUIStoreButton( vgui::Panel* pParent, vgui::Panel* pActionSignalTarget, const char* pSchemeFile, const char* pCommand, const char* pText, const char* pDescription, const char *pChapterImage, int pKashValue, bool bOnlyOne)
        : BaseClass( pParent, pActionSignalTarget, pSchemeFile, pCommand, pText, pDescription )
        , m_Image( pChapterImage )
        , m_iKashValue(pKashValue)
        , m_bOnlyOne(bOnlyOne)
    {
        m_iItemPurchases = 0;
    }

    GamepadUIStoreButton( vgui::Panel* pParent, vgui::Panel* pActionSignalTarget, const char* pSchemeFile, const char* pCommand, const wchar* pText, const wchar* pDescription, const char *pChapterImage, int pKashValue, bool bOnlyOne)
        : BaseClass( pParent, pActionSignalTarget, pSchemeFile, pCommand, pText, pDescription )
        , m_Image( pChapterImage )
        , m_iKashValue(pKashValue)
        , m_bOnlyOne(bOnlyOne)
    {
        m_iItemPurchases = 0;
    }

    ~GamepadUIStoreButton()
    {
    }

    ButtonState GetCurrentButtonState() OVERRIDE
    {
        return BaseClass::GetCurrentButtonState();
    }

    void DoClick() OVERRIDE
    {
        BaseClass::DoClick();

        //todo, fix this state not updating when trying to buy 2 weapons.
        static ConVarRef player_cur_money("player_cur_money");
        int money = player_cur_money.GetInt();

        m_iItemPurchases++;

        if (money >= m_iKashValue)
        {
            if (m_iItemPurchases == 1)
            {
                SetButtonDescription(GamepadUIString("#FR_Store_Purchased"));
            }
            else if (!m_bOnlyOne)
            {
                char szPurchases[1024];
                itoa(m_iItemPurchases, szPurchases, 10);

                wchar_t wzPurchases[1024];
                g_pVGuiLocalize->ConvertANSIToUnicode(szPurchases, wzPurchases, sizeof(wzPurchases));

                wchar_t string1[1024];
                g_pVGuiLocalize->ConstructString(string1, sizeof(string1), g_pVGuiLocalize->Find("#FR_Store_Purchased_Multiple"), 1, wzPurchases);

                SetButtonDescription(GamepadUIString(string1));
            }
            else
            {
                SetButtonDescription(GamepadUIString("#FR_Store_Denied"));
            }
        }
        else
        {
            SetButtonDescription(GamepadUIString("#FR_Store_Denied"));
        }

        GamepadUIStore* pParent = (GamepadUIStore*)GetParent();

        if (pParent)
        {
            pParent->UpdateFrameTitle();
        }
    }

    void Paint() OVERRIDE
    {
        int x, y, w, t;
        GetBounds( x, y, w, t );

        PaintButton();

        vgui::surface()->DrawSetColor( Color( 255, 255, 255, 255 ) );
        vgui::surface()->DrawSetTexture( m_Image );
        int imgH = ( w * 9 ) / 16;
        //vgui::surface()->DrawTexturedRect( 0, 0, w, );
        float offset = m_flTextOffsetYAnimationValue[ButtonStates::Out] - m_flTextOffsetY;
        vgui::surface()->DrawTexturedSubRect( 0, 0, w, imgH - offset, 0.0f, 0.0f, 1.0f, ( imgH - offset ) / imgH );
        vgui::surface()->DrawSetTexture( 0 );
        if ( !IsEnabled() )
        {
            vgui::surface()->DrawSetColor( Color( 255, 255, 255, 16 ) );
            vgui::surface()->DrawFilledRect( 0, 0, w, imgH - offset );
        }

        PaintText();
    }

    void ApplySchemeSettings(vgui::IScheme* pScheme)
    {
        BaseClass::ApplySchemeSettings(pScheme);

        if (GamepadUI::GetInstance().GetScreenRatio() != 1.0f)
        {
            float flScreenRatio = GamepadUI::GetInstance().GetScreenRatio();

            m_flHeight *= flScreenRatio;
            for (int i = 0; i < ButtonStates::Count; i++)
                m_flHeightAnimationValue[i] *= flScreenRatio;

            // Also change the text offset
            m_flTextOffsetY *= flScreenRatio;
            for (int i = 0; i < ButtonStates::Count; i++)
                m_flTextOffsetYAnimationValue[i] *= flScreenRatio;

            SetSize(m_flWidth, m_flHeight + m_flExtraHeight);
            DoAnimations(true);
        }
    }

    void NavigateTo() OVERRIDE
    {
        BaseClass::NavigateTo();
    }

private:
    GamepadUIImage m_Image;
    int m_iKashValue;
    int m_iItemPurchases;
    bool m_bOnlyOne;
};

GamepadUIStore::GamepadUIStore( vgui::Panel *pParent, const char* PanelName ) : BaseClass( pParent, PanelName )
{
    vgui::HScheme hScheme = vgui::scheme()->LoadSchemeFromFile( GAMEPADUI_DEFAULT_PANEL_SCHEME, "SchemePanel" );
    SetScheme( hScheme );

    UpdateFrameTitle();
    
    FooterButtonMask buttons = FooterButtons::Back | FooterButtons::Select;
    SetFooterButtons( buttons, FooterButtons::Select );

    Activate();
    
    FileFindHandle_t findHandle;
    
    const char* pFilename = g_pFullFileSystem->FindFirst("scripts/*.txt", &findHandle);
	while (pFilename)
	{
		const char* prefix = "shopcatalog_";
		if (Q_strncmp(pFilename, prefix, sizeof(prefix)) == 0)
		{
			char szScriptName[2048];
			Q_snprintf(szScriptName, sizeof(szScriptName), "scripts/%s", pFilename);
			LoadItemFile("ShopCatalog", szScriptName);
			DevMsg("'%s' added to shop item list!\n", szScriptName);
		}

		pFilename = g_pFullFileSystem->FindNext(findHandle);
	}

    CreateItemList();

    if ( m_pChapterButtons.Count() > 0 )
        m_pChapterButtons[0]->NavigateTo();

    for ( int i = 1; i < m_pChapterButtons.Count(); i++ )
    {
        m_pChapterButtons[i]->SetNavLeft( m_pChapterButtons[i - 1] );
        m_pChapterButtons[i - 1]->SetNavRight( m_pChapterButtons[i] );
    }

    UpdateGradients();
}

void GamepadUIStore::UpdateFrameTitle()
{
    static ConVarRef player_cur_money("player_cur_money");
    const char* money = player_cur_money.GetString();

    wchar_t wzmoney[1024];
    g_pVGuiLocalize->ConvertANSIToUnicode(money, wzmoney, sizeof(wzmoney));

    wchar_t string1[1024];
    g_pVGuiLocalize->ConstructString(string1, sizeof(string1), g_pVGuiLocalize->Find("#FR_Store_Title_Expanded"), 1, wzmoney);

    GetFrameTitle() = GamepadUIString(string1);
}

void GamepadUIStore::LoadItemFile(const char* kvName, const char* scriptPath)
{
	KeyValues* pKV = new KeyValues(kvName);
	if (pKV->LoadFromFile(g_pFullFileSystem, scriptPath))
	{
		KeyValues* pNode = pKV->GetFirstSubKey();
		while (pNode)
		{
            m_pItems.AddToTail(pNode);
			pNode = pNode->GetNextKey();
		}
	}
}

void GamepadUIStore::CreateItemList()
{
    for (int i = 0; i < m_pItems.Count(); i++)
    {
        KeyValues* pNode = m_pItems[i];

        const char* itemName = pNode->GetString("name", "");
        const char* itemPrice = pNode->GetString("price", 0);
        const char* itemCMD = pNode->GetString("command", "");

        char szNameString[2048];
        Q_snprintf(szNameString, sizeof(szNameString), "#GameUI_Store_Buy_%s", itemName);
        wchar_t* convertedText = g_pVGuiLocalize->Find(szNameString);
        if (!convertedText)
        {
            Q_snprintf(szNameString, sizeof(szNameString), "%s", itemName);
        }

        wchar_t text[32];
        wchar_t num[32];
        wchar_t* chapter = g_pVGuiLocalize->Find("#Valve_Hud_MONEY");
        g_pVGuiLocalize->ConvertANSIToUnicode(itemPrice, num, sizeof(num));
        _snwprintf(text, ARRAYSIZE(text), L"%ls %ls", num, chapter ? chapter : L"KASH");

        GamepadUIString strItemPrice(text);

        char szCommand[2048];
        Q_snprintf(szCommand, sizeof(szCommand), "purchase_item %s \"%s\"", itemPrice, itemCMD);

        GamepadUIString strChapterName(szNameString);

        char chapterImage[64];
        Q_snprintf(chapterImage, sizeof(chapterImage), "gamepadui/store/%s.vmt", itemName);
        if (!g_pFullFileSystem->FileExists(chapterImage))
        {
            Q_snprintf(chapterImage, sizeof(chapterImage), "vgui/hud/icon_locked.vmt");
        }

        bool onlyOne = pNode->GetString("onlyone", 0);
        int itemPriceInt = pNode->GetInt("price", 0);

        GamepadUIStoreButton* pChapterButton = new GamepadUIStoreButton(
            this, this,
            GAMEPADUI_MAP_SCHEME, szCommand,
            strChapterName.String(), strItemPrice.String(), chapterImage, itemPriceInt, onlyOne);
        pChapterButton->SetEnabled(true);
        pChapterButton->SetPriority(i);
        pChapterButton->SetForwardToParent(true);

        m_pChapterButtons.AddToTail(pChapterButton);
    }
}

void GamepadUIStore::UpdateGradients()
{
    const float flTime = GamepadUI::GetInstance().GetTime();
    GamepadUI::GetInstance().GetGradientHelper()->ResetTargets( flTime );
    GamepadUI::GetInstance().GetGradientHelper()->SetTargetGradient( GradientSide::Up, { 1.0f, 1.0f }, flTime );
    GamepadUI::GetInstance().GetGradientHelper()->SetTargetGradient( GradientSide::Down, { 1.0f, 0.5f }, flTime );
}

void GamepadUIStore::OnThink()
{
    BaseClass::OnThink();

    LayoutMapButtons();
}

void GamepadUIStore::ApplySchemeSettings(vgui::IScheme* pScheme)
{
    BaseClass::ApplySchemeSettings(pScheme);

    if (GamepadUI::GetInstance().GetScreenRatio() != 1.0f)
    {
        float flScreenRatio = GamepadUI::GetInstance().GetScreenRatio();
        m_ChapterOffsetX *= (flScreenRatio * flScreenRatio);
    }
}

void GamepadUIStore::OnGamepadUIButtonNavigatedTo( vgui::VPANEL button )
{
    GamepadUIButton *pButton = dynamic_cast< GamepadUIButton * >( vgui::ipanel()->GetPanel( button, GetModuleName() ) );
    if ( pButton )
    {
        int nParentW, nParentH;
	    GetParent()->GetSize( nParentW, nParentH );

        int nX, nY;
        pButton->GetPos( nX, nY );
        if ( nX + pButton->m_flWidth > nParentW || nX < 0 )
        {
            int nTargetX = pButton->GetPriority() * (pButton->m_flWidth + m_ChapterSpacing);

            if ( nX < nParentW / 2 )
            {
                nTargetX += nParentW - m_ChapterOffsetX;
                // Add a bit of spacing to make this more visually appealing :)
                nTargetX -= m_ChapterSpacing;
            }
            else
            {
                nTargetX += pButton->m_flWidth;
                // Add a bit of spacing to make this more visually appealing :)
                nTargetX += (pButton->m_flWidth / 2) + m_ChapterSpacing;
            }


            m_ScrollState.SetScrollTarget( nTargetX - ( nParentW - m_ChapterOffsetX ), GamepadUI::GetInstance().GetTime() );
        }
    }
}

void GamepadUIStore::LayoutMapButtons()
{
    int nParentW, nParentH;
	GetParent()->GetSize( nParentW, nParentH );

    float flScrollClamp = 0.0f;
    for ( int i = 0; i < m_pChapterButtons.Count(); i++ )
    {
        int nSize = ( m_pChapterButtons[0]->GetWide() + m_ChapterSpacing );

        if ( i < m_pChapterButtons.Count() - 2 )
            flScrollClamp += nSize;
    }

    m_ScrollState.UpdateScrollBounds( 0.0f, flScrollClamp );

    for ( int i = 0; i < m_pChapterButtons.Count(); i++ )
    {
        int size = ( m_pChapterButtons[0]->GetWide() + m_ChapterSpacing );

        m_pChapterButtons[i]->SetPos( m_ChapterOffsetX + i * size - m_ScrollState.GetScrollProgress(), m_ChapterOffsetY );
        m_pChapterButtons[i]->SetVisible( true );
    }

    m_ScrollState.UpdateScrolling( 2.0f, GamepadUI::GetInstance().GetTime() );
}

void GamepadUIStore::OnCommand( char const* pCommand )
{
    if ( !V_strcmp( pCommand, "action_back" ) )
    {
        Close();
        if (GamepadUI::GetInstance().IsInLevel())
        {
            GamepadUI::GetInstance().GetEngineClient()->ClientCmd_Unrestricted("gamemenucommand resumegame");
            // I tried it and didn't like it.
            // Oh well.
            //vgui::surface()->PlaySound( "UI/buttonclickrelease.wav" );
        }
    }
    else if (StringHasPrefixCaseSensitive(pCommand, "purchase_item "))
    {
        const char* pszparams = pCommand + 14;

        if (*pszparams)
        {
            char szPurchaseCommand[1024];

            // create the command to execute
            Q_snprintf(szPurchaseCommand, sizeof(szPurchaseCommand), "purchase %s\n", pszparams);

            // exec
            GamepadUI::GetInstance().GetEngineClient()->ClientCmd_Unrestricted(szPurchaseCommand);
            /*Close();
            if (GamepadUI::GetInstance().IsInLevel())
            {
                GamepadUI::GetInstance().GetEngineClient()->ClientCmd_Unrestricted("gamemenucommand resumegame");
                // I tried it and didn't like it.
                // Oh well.
                //vgui::surface()->PlaySound( "UI/buttonclickrelease.wav" );
            }*/
        }
    }
    else
    {
        BaseClass::OnCommand( pCommand );
    }
}

void GamepadUIStore::OnMouseWheeled( int nDelta )
{
    m_ScrollState.OnMouseWheeled( nDelta * m_ChapterSpacing * 20.0f, GamepadUI::GetInstance().GetTime() );
}

CON_COMMAND( gamepadui_openstore, "" )
{
    if (GamepadUI::GetInstance().IsInLevel())
    {
        GamepadUI::GetInstance().GetEngineClient()->ClientCmd_Unrestricted("gameui_activate");
        new GamepadUIStore(GamepadUI::GetInstance().GetBasePanel(), "");
    }
}
