/**********************************************************************

 HGE Music Studio — In-App AI Mix Chat

 A prompt box that lives inside the app: pick a track, type what you want
 done to it, and it exports that track, runs the AI Mix Assistant engine
 (ai_mix.py) on it, and imports the result back as a new track.

 **********************************************************************/

#ifndef __HGE_AI_CHAT_H__
#define __HGE_AI_CHAT_H__

#include <wx/dialog.h>
#include <wx/choice.h>
#include <wx/textctrl.h>
#include <wx/button.h>
#include <wx/sizer.h>
#include <wx/stattext.h>

class AudacityProject;

class HgeAiChat final : public wxDialog
{
public:
   HgeAiChat(wxWindow *parent, AudacityProject &project);
   ~HgeAiChat() override;

private:
   void BuildUi();
   void RefreshTrackList();
   void OnApply(wxCommandEvent &evt);
   void OnClose(wxCommandEvent &evt);
   void OnSaveSkill(wxCommandEvent &evt);
   void OnRenameSkill(wxCommandEvent &evt);
   void OnDeleteSkill(wxCommandEvent &evt);
   void OnOpenSettings(wxCommandEvent &evt);

   // Name of the currently selected saved skill, or empty if "(none)".
   wxString SelectedSkillName() const;
   void Log(const wxString &line);

   // Returns empty string on failure (and logs why).
   wxString ExportSelectedTrackToWav();
   // Returns the path ai_mix.py wrote to, or empty if it failed or asked a
   // clarifying question instead (see outClarifyQuestion in that case).
   // outPlanJson receives the single-line PLAN_JSON: payload on success,
   // for "Save as Skill" to persist later.
   wxString RunAiMix(const wxString &inputWav, const wxString &prompt,
                      const wxString &settingsArgs, const wxString &skillFile,
                      wxString &outClarifyQuestion, wxString &outPlanJson);
   wxString FindAiMixScript() const;

   wxString SkillsDir() const;
   void RefreshSkillChoice();

   AudacityProject &mProject;

   wxChoice   *mTrackChoice;
   wxTextCtrl *mPromptCtrl;
   wxTextCtrl *mStatusCtrl;
   wxButton   *mApplyBtn;

   // Mastering Settings — manual overrides. Each defaults to "Auto" (let
   // the AI decide from the prompt); picking anything else forces that
   // exact value/style regardless of what the AI would have chosen.
   wxChoice   *mLufsChoice;
   wxChoice   *mTruePeakChoice;
   wxChoice   *mEqStyleChoice;
   wxChoice   *mCompressionChoice;
   wxChoice   *mSaturationChoice;

   // Builds the --target-lufs/--true-peak/--eq-style/--compression/
   // --saturation CLI flags for ai_mix.py from the current dropdown
   // selections; empty string for any left on "Auto".
   wxString BuildSettingsArgs() const;

   // Saved Skills — named recipes (a previously produced plan, saved to
   // disk) the user can recall so the AI matches a style they already
   // liked instead of guessing fresh every time.
   wxChoice   *mSkillChoice;
   wxButton   *mSaveSkillBtn;
   wxButton   *mRenameSkillBtn;
   wxButton   *mDeleteSkillBtn;
   wxButton   *mSettingsBtn;

   // Multi-turn state. mPendingInputWav is the audio the NEXT Apply click
   // operates on -- empty means "export fresh from the selected track".
   // It's set two different ways, distinguished by mAwaitingClarify:
   //   - AI asked a clarifying question: mPendingInputWav stays the
   //     ORIGINAL exported track (unmixed); the next prompt is treated as
   //     the answer (mHistory carries the Q&A so far).
   //   - A mix completed successfully: mPendingInputWav becomes that
   //     MIXED result; the next prompt is treated as a touch-up applied
   //     on top of it, chaining further edits instead of starting over.
   // Switching the track dropdown resets this back to fresh (see
   // mLastTrackName).
   bool       mAwaitingClarify = false;
   wxString   mPendingInputWav;
   wxString   mHistory;
   wxString   mLastTrackName;
   wxString   mLastPlanJson;

   DECLARE_EVENT_TABLE()
};

#endif // __HGE_AI_CHAT_H__
