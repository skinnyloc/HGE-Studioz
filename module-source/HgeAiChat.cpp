/**********************************************************************

 HGE Music Studio — In-App AI Mix Chat implementation

 **********************************************************************/

#include "HgeAiChat.h"
#include "HgeAiSettings.h"

#include <sndfile.h>

#include <wx/gbsizer.h>
#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/utils.h>
#include <wx/txtstrm.h>
#include <wx/process.h>
#include <wx/log.h>
#include <wx/dir.h>
#include <wx/file.h>
#include <wx/textdlg.h>

#include "Project.h"
#include "ProjectFileManager.h"
#include "Track.h"
#include "WaveTrack.h"
#include "AudacityMessageBox.h"

enum {
   ID_SAVE_SKILL = wxID_HIGHEST + 500,
   ID_OPEN_SETTINGS = wxID_HIGHEST + 501,
   ID_RENAME_SKILL = wxID_HIGHEST + 502,
   ID_DELETE_SKILL = wxID_HIGHEST + 503,
};

BEGIN_EVENT_TABLE(HgeAiChat, wxDialog)
   EVT_BUTTON(wxID_APPLY, HgeAiChat::OnApply)
   EVT_BUTTON(wxID_CLOSE, HgeAiChat::OnClose)
   EVT_BUTTON(ID_SAVE_SKILL, HgeAiChat::OnSaveSkill)
   EVT_BUTTON(ID_RENAME_SKILL, HgeAiChat::OnRenameSkill)
   EVT_BUTTON(ID_DELETE_SKILL, HgeAiChat::OnDeleteSkill)
   EVT_BUTTON(ID_OPEN_SETTINGS, HgeAiChat::OnOpenSettings)
END_EVENT_TABLE()

HgeAiChat::HgeAiChat(wxWindow *parent, AudacityProject &project)
   : wxDialog(parent, wxID_ANY, wxT("HGE AI Mix Assistant"),
              wxDefaultPosition, wxSize(600, 680),
              wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER)
   , mProject(project)
   , mTrackChoice(nullptr)
   , mPromptCtrl(nullptr)
   , mStatusCtrl(nullptr)
   , mApplyBtn(nullptr)
   , mLufsChoice(nullptr)
   , mTruePeakChoice(nullptr)
   , mEqStyleChoice(nullptr)
   , mCompressionChoice(nullptr)
   , mSaturationChoice(nullptr)
   , mSkillChoice(nullptr)
   , mSaveSkillBtn(nullptr)
   , mRenameSkillBtn(nullptr)
   , mDeleteSkillBtn(nullptr)
   , mSettingsBtn(nullptr)
{
   BuildUi();
   RefreshTrackList();
   RefreshSkillChoice();
}

HgeAiChat::~HgeAiChat() = default;

void HgeAiChat::BuildUi()
{
   auto *topSizer = new wxBoxSizer(wxVERTICAL);

   topSizer->Add(new wxStaticText(this, wxID_ANY, wxT("Track:")),
                 0, wxLEFT | wxTOP, 10);
   mTrackChoice = new wxChoice(this, wxID_ANY);
   topSizer->Add(mTrackChoice, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);

   topSizer->Add(new wxStaticText(this, wxID_ANY,
      wxT("Describe how you want this track mixed/mastered:")),
      0, wxLEFT | wxTOP, 10);
   mPromptCtrl = new wxTextCtrl(this, wxID_ANY,
      wxT("warm the vocal, tighten the low end, gentle glue compression"),
      wxDefaultPosition, wxSize(-1, 60), wxTE_MULTILINE);
   topSizer->Add(mPromptCtrl, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);

   // ---- Mastering Settings: the two hard targets (loudness + peak ceiling) ----
   topSizer->Add(new wxStaticText(this, wxID_ANY, wxT("Mastering Settings")),
                 0, wxLEFT | wxTOP, 10);

   auto *targetGrid = new wxFlexGridSizer(2, 5, 5);

   targetGrid->Add(new wxStaticText(this, wxID_ANY, wxT("Target LUFS:")));
   mLufsChoice = new wxChoice(this, wxID_ANY);
   mLufsChoice->Append(wxT("Auto (AI decides)"));
   mLufsChoice->Append(wxT("-9 (Club/DJ)"));
   mLufsChoice->Append(wxT("-11 (Hip-Hop)"));
   mLufsChoice->Append(wxT("-13"));
   mLufsChoice->Append(wxT("-14 (Streaming)"));
   mLufsChoice->Append(wxT("-15"));
   mLufsChoice->Append(wxT("-16 (Apple Music)"));
   mLufsChoice->SetSelection(0);
   targetGrid->Add(mLufsChoice, 0, wxEXPAND);

   targetGrid->Add(new wxStaticText(this, wxID_ANY, wxT("True Peak Limit:")));
   mTruePeakChoice = new wxChoice(this, wxID_ANY);
   mTruePeakChoice->Append(wxT("-0.5 dBTP"));
   mTruePeakChoice->Append(wxT("-1.0 dBTP"));
   mTruePeakChoice->Append(wxT("-1.5 dBTP"));
   mTruePeakChoice->Append(wxT("-2.0 dBTP"));
   mTruePeakChoice->SetSelection(1); // -1.0 default
   targetGrid->Add(mTruePeakChoice, 0, wxEXPAND);

   targetGrid->AddGrowableCol(1, 1);
   topSizer->Add(targetGrid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);

   // ---- Manual tweaks: character controls; each "Auto" hands it to the AI ----
   topSizer->Add(new wxStaticText(this, wxID_ANY,
      wxT("MANUAL TWEAKS — set these yourself, or let the AI above fill them in")),
      0, wxLEFT | wxTOP, 12);

   auto *tweakGrid = new wxFlexGridSizer(2, 5, 5);

   tweakGrid->Add(new wxStaticText(this, wxID_ANY, wxT("EQ Style:")));
   mEqStyleChoice = new wxChoice(this, wxID_ANY);
   mEqStyleChoice->Append(wxT("Auto (AI decides)"));
   mEqStyleChoice->Append(wxT("Natural"));
   mEqStyleChoice->Append(wxT("Bright"));
   mEqStyleChoice->Append(wxT("Warm"));
   mEqStyleChoice->SetSelection(0);
   tweakGrid->Add(mEqStyleChoice, 0, wxEXPAND);

   tweakGrid->Add(new wxStaticText(this, wxID_ANY, wxT("Compression:")));
   mCompressionChoice = new wxChoice(this, wxID_ANY);
   mCompressionChoice->Append(wxT("Auto (AI decides)"));
   mCompressionChoice->Append(wxT("Off"));
   mCompressionChoice->Append(wxT("Light"));
   mCompressionChoice->Append(wxT("Medium"));
   mCompressionChoice->Append(wxT("Heavy"));
   mCompressionChoice->SetSelection(0);
   tweakGrid->Add(mCompressionChoice, 0, wxEXPAND);

   tweakGrid->Add(new wxStaticText(this, wxID_ANY, wxT("Saturation:")));
   mSaturationChoice = new wxChoice(this, wxID_ANY);
   mSaturationChoice->Append(wxT("Auto (AI decides)"));
   mSaturationChoice->Append(wxT("None"));
   mSaturationChoice->Append(wxT("Subtle"));
   mSaturationChoice->Append(wxT("Medium"));
   mSaturationChoice->SetSelection(0);
   tweakGrid->Add(mSaturationChoice, 0, wxEXPAND);

   tweakGrid->AddGrowableCol(1, 1);
   topSizer->Add(tweakGrid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);

   auto *skillRow = new wxBoxSizer(wxHORIZONTAL);
   skillRow->Add(new wxStaticText(this, wxID_ANY, wxT("Saved Skill:")),
                 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
   mSkillChoice = new wxChoice(this, wxID_ANY);
   mSkillChoice->Append(wxT("(none)"));
   mSkillChoice->SetSelection(0);
   skillRow->Add(mSkillChoice, 1, wxALIGN_CENTER_VERTICAL | wxRIGHT, 10);
   mSaveSkillBtn = new wxButton(this, ID_SAVE_SKILL, wxT("Save as Skill..."));
   skillRow->Add(mSaveSkillBtn, 0, wxRIGHT, 5);
   mRenameSkillBtn = new wxButton(this, ID_RENAME_SKILL, wxT("Rename"));
   skillRow->Add(mRenameSkillBtn, 0, wxRIGHT, 5);
   mDeleteSkillBtn = new wxButton(this, ID_DELETE_SKILL, wxT("Remove"));
   skillRow->Add(mDeleteSkillBtn, 0);
   topSizer->Add(skillRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);

   auto *btnRow = new wxBoxSizer(wxHORIZONTAL);
   mApplyBtn = new wxButton(this, wxID_APPLY, wxT("Apply with AI"));
   btnRow->Add(mApplyBtn, 0, wxALL, 5);
   mSettingsBtn = new wxButton(this, ID_OPEN_SETTINGS, wxT("Settings..."));
   btnRow->Add(mSettingsBtn, 0, wxALL, 5);
   btnRow->AddStretchSpacer();
   btnRow->Add(new wxButton(this, wxID_CLOSE, wxT("Close")), 0, wxALL, 5);
   topSizer->Add(btnRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 5);

   topSizer->Add(new wxStaticText(this, wxID_ANY, wxT("Status:")),
                 0, wxLEFT | wxTOP, 10);
   mStatusCtrl = new wxTextCtrl(this, wxID_ANY, wxEmptyString,
      wxDefaultPosition, wxSize(-1, 220),
      wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH2);
   topSizer->Add(mStatusCtrl, 1, wxEXPAND | wxALL, 10);

   SetSizer(topSizer);
}

void HgeAiChat::RefreshTrackList()
{
   mTrackChoice->Clear();
   auto &tracks = TrackList::Get(mProject);
   for (auto *track : tracks.Any<WaveTrack>())
   {
      mTrackChoice->Append(track->GetName());
   }
   if (mTrackChoice->GetCount() > 0)
      mTrackChoice->SetSelection(0);
   else
      Log(wxT("No audio tracks in this project yet — add or record one first."));
}

void HgeAiChat::Log(const wxString &line)
{
   mStatusCtrl->AppendText(line + wxT("\n"));
}

wxString HgeAiChat::ExportSelectedTrackToWav()
{
   int sel = mTrackChoice->GetSelection();
   if (sel == wxNOT_FOUND)
   {
      Log(wxT("No track selected."));
      return wxEmptyString;
   }
   wxString wantedName = mTrackChoice->GetString(sel);

   WaveTrack *found = nullptr;
   auto &tracks = TrackList::Get(mProject);
   for (auto *track : tracks.Any<WaveTrack>())
   {
      if (track->GetName() == wantedName) { found = track; break; }
   }
   if (!found)
   {
      Log(wxT("Couldn't find that track anymore — try refreshing."));
      return wxEmptyString;
   }

   const int numChannels = static_cast<int>(found->NChannels());
   const double rate = found->GetRate();
   const sampleCount total = found->GetVisibleSampleCount();

   wxString tempPath = wxFileName::CreateTempFileName(wxT("hge_ai_in_"));
   wxRemoveFile(tempPath);
   tempPath += wxT(".wav");

   SF_INFO sfInfo;
   memset(&sfInfo, 0, sizeof(sfInfo));
   sfInfo.samplerate = static_cast<int>(rate);
   sfInfo.channels = numChannels;
   sfInfo.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;

   SNDFILE *sf = sf_open(tempPath.utf8_str(), SFM_WRITE, &sfInfo);
   if (!sf)
   {
      Log(wxString::Format(wxT("Couldn't open temp WAV for writing: %s"),
          wxString::FromUTF8(sf_strerror(nullptr))));
      return wxEmptyString;
   }

   const size_t blockSize = 65536;
   std::vector<float> interleaved(blockSize * numChannels);
   std::vector<std::vector<float>> perChannel(numChannels, std::vector<float>(blockSize));

   sampleCount pos = 0;
   bool ok = true;
   auto channels = found->Channels();
   while (pos < total && ok)
   {
      size_t thisBlock = std::min(blockSize, static_cast<size_t>((total - pos).as_long_long()));

      int ch = 0;
      for (auto channel : channels)
      {
         if (!channel->GetFloats(perChannel[ch].data(), pos, thisBlock))
         {
            ok = false;
            break;
         }
         ch++;
      }
      if (!ok) break;

      for (size_t i = 0; i < thisBlock; i++)
         for (int c = 0; c < numChannels; c++)
            interleaved[i * numChannels + c] = perChannel[c][i];

      sf_count_t written = sf_writef_float(sf, interleaved.data(), static_cast<sf_count_t>(thisBlock));
      if (written != static_cast<sf_count_t>(thisBlock)) { ok = false; break; }

      pos += thisBlock;
   }

   sf_close(sf);

   if (!ok)
   {
      Log(wxT("Failed reading track samples partway through export."));
      wxRemoveFile(tempPath);
      return wxEmptyString;
   }

   Log(wxString::Format(wxT("Exported \"%s\" -> %s"), wantedName, tempPath));
   return tempPath;
}

wxString HgeAiChat::FindAiMixScript() const
{
   // Bundled inside the shipped app first, dev-machine path as a fallback.
   wxFileName bundled(wxStandardPaths::Get().GetResourcesDir(),
                       wxEmptyString);
   bundled.AppendDir(wxT("ai_mix_assistant"));
   bundled.SetFullName(wxT("ai_mix.py"));
   if (bundled.FileExists())
      return bundled.GetFullPath();

   wxString devPath = wxT("/Users/homegrownentllc/HgeMusicStudio/ai_mix_assistant/ai_mix.py");
   if (wxFileExists(devPath))
      return devPath;

   return wxEmptyString;
}

wxString HgeAiChat::BuildSettingsArgs() const
{
   wxString args;

   // Index 0 is always "Auto (AI decides)" for these four -- only append a
   // flag when the user picked something more specific than that.
   static const double lufsValues[] = { 0, -9, -11, -13, -14, -15, -16 };
   int lufsSel = mLufsChoice->GetSelection();
   if (lufsSel > 0)
      args += wxString::Format(wxT(" --target-lufs %g"), lufsValues[lufsSel]);

   static const double truePeakValues[] = { -0.5, -1.0, -1.5, -2.0 };
   int tpSel = mTruePeakChoice->GetSelection();
   if (tpSel >= 0 && tpSel < 4)
      args += wxString::Format(wxT(" --true-peak %g"), truePeakValues[tpSel]);

   static const wxChar *eqValues[] = { nullptr, wxT("natural"), wxT("bright"), wxT("warm") };
   int eqSel = mEqStyleChoice->GetSelection();
   if (eqSel > 0)
      args += wxString(wxT(" --eq-style ")) + eqValues[eqSel];

   static const wxChar *compValues[] = { nullptr, wxT("off"), wxT("light"), wxT("medium"), wxT("heavy") };
   int compSel = mCompressionChoice->GetSelection();
   if (compSel > 0)
      args += wxString(wxT(" --compression ")) + compValues[compSel];

   static const wxChar *satValues[] = { nullptr, wxT("none"), wxT("subtle"), wxT("medium") };
   int satSel = mSaturationChoice->GetSelection();
   if (satSel > 0)
      args += wxString(wxT(" --saturation ")) + satValues[satSel];

   return args.Trim(false);
}

wxString HgeAiChat::RunAiMix(const wxString &inputWav, const wxString &prompt,
                              const wxString &settingsArgs, const wxString &skillFile,
                              wxString &outClarifyQuestion, wxString &outPlanJson)
{
   outClarifyQuestion.Clear();
   outPlanJson.Clear();

   wxString script = FindAiMixScript();
   if (script.IsEmpty())
   {
      Log(wxT("Couldn't find ai_mix.py (checked app Resources and the dev path)."));
      return wxEmptyString;
   }

   wxFileName outFn(inputWav);
   outFn.SetName(outFn.GetName() + wxT("_ai_mixed"));
   wxString outputWav = outFn.GetFullPath();

   wxString cmd = wxString::Format(
      // No --mode: ai_mix.py picks "api" if a saved key exists (client/BYOK
      // build) or "personal" otherwise (Van's own subscription build) --
      // see Settings dialog / ai_mix_config.json.
      wxT("python3 %s --in %s --out %s --prompt %s"),
      wxT("\"") + script + wxT("\""),
      wxT("\"") + inputWav + wxT("\""),
      wxT("\"") + outputWav + wxT("\""),
      wxT("\"") + prompt + wxT("\""));
   if (!settingsArgs.IsEmpty())
      cmd += wxT(" ") + settingsArgs;
   if (!skillFile.IsEmpty())
      cmd += wxT(" --skill-file \"") + skillFile + wxT("\"");

   Log(wxT("Running AI Mix Assistant (this can take up to a minute)..."));
   wxArrayString output, errors;
   long code = wxExecute(cmd, output, errors, wxEXEC_SYNC);

   // ai_mix.py prints "CLARIFY:<question>" instead of producing an output
   // file when the request was too ambiguous (SYSTEM_PROMPT's
   // "type":"question"), and "PLAN_JSON:<json>" alongside a normal
   // successful plan for "Save as Skill" to persist later. Neither is
   // meant for the human-readable status log.
   for (const auto &line : output)
   {
      if (line.StartsWith(wxT("CLARIFY:")))
         outClarifyQuestion = line.Mid(8);
      else if (line.StartsWith(wxT("PLAN_JSON:")))
         outPlanJson = line.Mid(10);
      else
         Log(line);
   }
   for (const auto &line : errors) Log(wxT("[stderr] ") + line);

   if (!outClarifyQuestion.IsEmpty())
      return wxEmptyString;

   if (code != 0 || !wxFileExists(outputWav))
   {
      Log(wxString::Format(wxT("ai_mix.py exited with code %ld — no output produced."), code));
      return wxEmptyString;
   }
   return outputWav;
}

void HgeAiChat::OnApply(wxCommandEvent &WXUNUSED(evt))
{
   wxString prompt = mPromptCtrl->GetValue().Trim();
   if (prompt.IsEmpty())
   {
      Log(wxT("Type a prompt first."));
      return;
   }

   mApplyBtn->Disable();

   wxString currentTrackName;
   int trackSel = mTrackChoice->GetSelection();
   if (trackSel != wxNOT_FOUND)
      currentTrackName = mTrackChoice->GetString(trackSel);

   // Three cases for what the AI should operate on:
   //  1. Mid-clarify (AI asked a question, this prompt is the answer) --
   //     reuse the already-exported ORIGINAL track, fold the Q&A so far in.
   //  2. Touch-up (last Apply on this same track succeeded) -- reuse that
   //     MIXED result as the new source, so this is a further edit on top
   //     of it rather than starting over from the raw track.
   //  3. Fresh (first prompt for this track, or the track selection
   //     changed since the last run) -- export the track from scratch.
   wxString inputWav;
   wxString effectivePrompt;
   bool sameTrackAsLastRun = !mLastTrackName.IsEmpty() && mLastTrackName == currentTrackName;

   if (mAwaitingClarify && sameTrackAsLastRun && !mPendingInputWav.IsEmpty() && wxFileExists(mPendingInputWav))
   {
      inputWav = mPendingInputWav;
      effectivePrompt = mHistory + wxT("\nFollow-up answer: ") + prompt;
   }
   else if (!mAwaitingClarify && sameTrackAsLastRun && !mPendingInputWav.IsEmpty() && wxFileExists(mPendingInputWav))
   {
      inputWav = mPendingInputWav;
      effectivePrompt = wxT("This audio has already been mixed/mastered once by an earlier "
                             "step in this session. Additional touch-up instruction, apply on "
                             "top of how it already sounds: ") + prompt;
      Log(wxT("(touching up the previous result, not starting over)"));
   }
   else
   {
      mHistory.Clear();
      mAwaitingClarify = false;
      inputWav = ExportSelectedTrackToWav();
      mPendingInputWav = inputWav;
      mLastTrackName = currentTrackName;
      effectivePrompt = prompt;
   }

   wxString skillFile;
   int skillSel = mSkillChoice->GetSelection();
   if (skillSel > 0)
      skillFile = SkillsDir() + wxFileName::GetPathSeparator() + mSkillChoice->GetString(skillSel) + wxT(".json");

   if (!inputWav.IsEmpty())
   {
      wxString clarifyQuestion, planJson;
      wxString outputWav = RunAiMix(inputWav, effectivePrompt, BuildSettingsArgs(), skillFile,
                                     clarifyQuestion, planJson);

      if (!clarifyQuestion.IsEmpty())
      {
         Log(wxT("AI: ") + clarifyQuestion);
         mHistory += wxT("\nYou: ") + prompt + wxT("\nAI asked: ") + clarifyQuestion;
         mAwaitingClarify = true;
         mPromptCtrl->Clear();
         mPromptCtrl->SetFocus();
      }
      else if (!outputWav.IsEmpty())
      {
         Log(wxT("Importing result as a new track..."));
         if (ProjectFileManager::Get(mProject).Import(outputWav))
            Log(wxT("Done — type another prompt to touch this up further, pick a ")
                wxT("different track to start fresh, or Save as Skill if you like this sound."));
         else
            Log(wxT("Import failed."));

         mAwaitingClarify = false;
         mHistory.Clear();
         mPendingInputWav = outputWav; // chain further touch-ups onto the result, not the raw track
         if (!planJson.IsEmpty())
            mLastPlanJson = planJson;
         mPromptCtrl->Clear();
      }
      // else: failure already logged inside RunAiMix/ExportSelectedTrackToWav.
   }

   mApplyBtn->Enable();
}

void HgeAiChat::OnSaveSkill(wxCommandEvent &WXUNUSED(evt))
{
   if (mLastPlanJson.IsEmpty())
   {
      Log(wxT("Run a mix first, then Save as Skill to name and keep that recipe."));
      return;
   }

   wxString name = wxGetTextFromUser(
      wxT("Name this skill (how this track was mixed) so you can reuse it later:"),
      wxT("Save as Skill"), wxEmptyString, this);
   name.Trim(true).Trim(false);
   if (name.IsEmpty())
      return;

   // Keep filenames sane -- skill names become JSON filenames on disk.
   wxString safeName;
   for (auto ch : name)
      safeName += (wxIsalnum(ch) || ch == '-' || ch == '_' || ch == ' ') ? ch : wxT('_');

   wxString dir = SkillsDir();
   if (!wxFileName::DirExists(dir))
      wxFileName::Mkdir(dir, wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

   wxString path = dir + wxFileName::GetPathSeparator() + safeName + wxT(".json");

   // Tag the saved plan with its own name so ai_mix.py can reference it
   // back to Claude by name ("the user's saved skill 'Trap Vocal'...").
   wxString taggedJson = mLastPlanJson;
   if (taggedJson.EndsWith(wxT("}")))
      taggedJson = taggedJson.Left(taggedJson.Len() - 1) +
         wxT(",\"_skill_name\":\"") + safeName + wxT("\"}");

   wxFile file(path, wxFile::write);
   if (file.IsOpened() && file.Write(taggedJson))
   {
      Log(wxT("Saved skill \"") + safeName + wxT("\"."));
      RefreshSkillChoice();
      mSkillChoice->SetStringSelection(safeName);
   }
   else
   {
      Log(wxT("Couldn't write skill file to ") + path);
   }
}

wxString HgeAiChat::SelectedSkillName() const
{
   int sel = mSkillChoice->GetSelection();
   if (sel <= 0)  // index 0 is always "(none)"
      return wxEmptyString;
   return mSkillChoice->GetString(sel);
}

void HgeAiChat::OnRenameSkill(wxCommandEvent &WXUNUSED(evt))
{
   wxString oldName = SelectedSkillName();
   if (oldName.IsEmpty())
   {
      Log(wxT("Pick a saved skill from the dropdown first, then Rename."));
      return;
   }

   wxString newName = wxGetTextFromUser(
      wxT("New name for skill \"") + oldName + wxT("\":"),
      wxT("Rename Skill"), oldName, this);
   newName.Trim(true).Trim(false);
   if (newName.IsEmpty() || newName == oldName)
      return;

   // Same filename sanitizing as OnSaveSkill -- names become JSON filenames.
   wxString safeName;
   for (auto ch : newName)
      safeName += (wxIsalnum(ch) || ch == '-' || ch == '_' || ch == ' ') ? ch : wxT('_');

   wxString sep = wxFileName::GetPathSeparator();
   wxString dir = SkillsDir();
   wxString oldPath = dir + sep + oldName + wxT(".json");
   wxString newPath = dir + sep + safeName + wxT(".json");

   if (wxFileExists(newPath))
   {
      Log(wxT("A skill named \"") + safeName + wxT("\" already exists — pick another name."));
      return;
   }

   if (wxRenameFile(oldPath, newPath))
   {
      Log(wxT("Renamed skill \"") + oldName + wxT("\" to \"") + safeName + wxT("\"."));
      RefreshSkillChoice();
      mSkillChoice->SetStringSelection(safeName);
   }
   else
   {
      Log(wxT("Couldn't rename the skill file."));
   }
}

void HgeAiChat::OnDeleteSkill(wxCommandEvent &WXUNUSED(evt))
{
   wxString name = SelectedSkillName();
   if (name.IsEmpty())
   {
      Log(wxT("Pick a saved skill from the dropdown first, then Remove."));
      return;
   }

   int answer = AudacityMessageBox(
      XO("Remove the saved skill \"%s\"? This can't be undone.").Format(name),
      XO("Remove Skill"),
      wxYES_NO | wxICON_QUESTION, this);
   if (answer != wxYES)
      return;

   wxString path = SkillsDir() + wxFileName::GetPathSeparator() + name + wxT(".json");
   if (wxRemoveFile(path))
   {
      Log(wxT("Removed skill \"") + name + wxT("\"."));
      RefreshSkillChoice();
      mSkillChoice->SetSelection(0);
   }
   else
   {
      Log(wxT("Couldn't remove the skill file."));
   }
}

void HgeAiChat::OnOpenSettings(wxCommandEvent &WXUNUSED(evt))
{
   HgeAiSettings dlg(this);
   dlg.ShowModal();
}

wxString HgeAiChat::SkillsDir() const
{
   wxFileName dir(wxStandardPaths::Get().GetUserDataDir(), wxEmptyString);
   dir.AppendDir(wxT("mix_skills"));
   return dir.GetPath();
}

void HgeAiChat::RefreshSkillChoice()
{
   wxString previous = mSkillChoice->GetStringSelection();
   mSkillChoice->Clear();
   mSkillChoice->Append(wxT("(none)"));

   wxString dir = SkillsDir();
   if (wxFileName::DirExists(dir))
   {
      wxDir d(dir);
      wxString filename;
      bool cont = d.GetFirst(&filename, wxT("*.json"), wxDIR_FILES);
      while (cont)
      {
         mSkillChoice->Append(filename.BeforeLast(wxT('.')));
         cont = d.GetNext(&filename);
      }
   }

   int idx = mSkillChoice->FindString(previous);
   mSkillChoice->SetSelection(idx != wxNOT_FOUND ? idx : 0);
}

void HgeAiChat::OnClose(wxCommandEvent &WXUNUSED(evt))
{
   Close();
}
