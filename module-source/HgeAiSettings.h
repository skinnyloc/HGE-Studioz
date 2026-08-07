/**********************************************************************

 HGE Music Studio — AI Mix Assistant Settings

 Where a sold-DMG buyer pastes their own Anthropic API key and picks a
 thinking effort, instead of hand-editing ai_mix_config.json. Van's own
 personal build just leaves this empty -- no key on file means ai_mix.py
 falls back to his Claude Code subscription automatically.

 **********************************************************************/

#ifndef __HGE_AI_SETTINGS_H__
#define __HGE_AI_SETTINGS_H__

#include <wx/dialog.h>
#include <wx/choice.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

class HgeAiSettings final : public wxDialog
{
public:
   explicit HgeAiSettings(wxWindow *parent);
   ~HgeAiSettings() override;

private:
   void BuildUi();
   void LoadExisting();
   void OnSave(wxCommandEvent &evt);
   void OnClearKey(wxCommandEvent &evt);
   void OnClose(wxCommandEvent &evt);

   static wxString ConfigPath();

   wxTextCtrl *mApiKeyCtrl;
   wxChoice   *mThinkChoice;
   wxTextCtrl *mModelOverrideCtrl;
   wxStaticText *mStatusText;

   DECLARE_EVENT_TABLE()
};

#endif // __HGE_AI_SETTINGS_H__
