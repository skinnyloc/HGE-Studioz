/**********************************************************************

 HGE Music Studio — AI Mix Assistant Settings implementation

 **********************************************************************/

#include "HgeAiSettings.h"

#include <wx/filename.h>
#include <wx/stdpaths.h>
#include <wx/file.h>
#include <wx/sstream.h>

#include <algorithm>

namespace
{
   // Minimal hand-rolled JSON read/write -- the config file only ever has
   // a few flat string keys, not worth pulling in a JSON library for.
   wxString JsonEscape(const wxString &s)
   {
      wxString out;
      for (auto ch : s)
      {
         if (ch == wxT('"') || ch == wxT('\\'))
            out += wxT('\\');
         out += ch;
      }
      return out;
   }

   // Pulls "key":"value" out of a flat single-level JSON object. Good
   // enough for a config file we ourselves always write in this exact
   // shape; not a general parser. Uses wx's own Find()/wxNOT_FOUND
   // consistently throughout (not mixed with STL-style find()/npos).
   wxString JsonGetString(const wxString &json, const wxString &key)
   {
      wxString needle = wxT("\"") + key + wxT("\"");
      int pos = json.Find(needle);
      if (pos == wxNOT_FOUND)
         return wxEmptyString;

      int firstQuote = wxNOT_FOUND;
      for (int i = pos + (int)needle.Len(); i < (int)json.Len(); i++)
      {
         if (json[i] == wxT('"')) { firstQuote = i; break; }
      }
      if (firstQuote == wxNOT_FOUND)
         return wxEmptyString;

      int secondQuote = wxNOT_FOUND;
      for (int i = firstQuote + 1; i < (int)json.Len(); i++)
      {
         if (json[i] == wxT('"') && json[i - 1] != wxT('\\')) { secondQuote = i; break; }
      }
      if (secondQuote == wxNOT_FOUND)
         return wxEmptyString;

      return json.Mid(firstQuote + 1, secondQuote - firstQuote - 1);
   }
}

enum { ID_CLEAR_KEY = wxID_HIGHEST + 600 };

BEGIN_EVENT_TABLE(HgeAiSettings, wxDialog)
   EVT_BUTTON(wxID_SAVE, HgeAiSettings::OnSave)
   EVT_BUTTON(wxID_CLOSE, HgeAiSettings::OnClose)
   EVT_BUTTON(ID_CLEAR_KEY, HgeAiSettings::OnClearKey)
END_EVENT_TABLE()

HgeAiSettings::HgeAiSettings(wxWindow *parent)
   : wxDialog(parent, wxID_ANY, wxT("AI Mix Assistant Settings"),
              wxDefaultPosition, wxSize(480, 340),
              wxDEFAULT_DIALOG_STYLE)
   , mApiKeyCtrl(nullptr)
   , mThinkChoice(nullptr)
   , mModelOverrideCtrl(nullptr)
   , mStatusText(nullptr)
{
   BuildUi();
   LoadExisting();
}

HgeAiSettings::~HgeAiSettings() = default;

wxString HgeAiSettings::ConfigPath()
{
   wxFileName dir(wxStandardPaths::Get().GetUserDataDir(), wxEmptyString);
   if (!dir.DirExists())
      dir.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);
   dir.SetFullName(wxT("ai_mix_config.json"));
   return dir.GetFullPath();
}

void HgeAiSettings::BuildUi()
{
   auto *topSizer = new wxBoxSizer(wxVERTICAL);

   topSizer->Add(new wxStaticText(this, wxID_ANY,
      wxT("Leave the API key blank to use Van's personal Claude Code subscription.\n")
      wxT("For the sold/client version: paste your own Anthropic API key below --\n")
      wxT("every request is billed to that key, not to Van.")),
      0, wxALL, 10);

   topSizer->Add(new wxStaticText(this, wxID_ANY, wxT("Anthropic API Key:")),
                 0, wxLEFT | wxTOP, 10);
   mApiKeyCtrl = new wxTextCtrl(this, wxID_ANY, wxEmptyString,
      wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
   topSizer->Add(mApiKeyCtrl, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);

   auto *clearRow = new wxBoxSizer(wxHORIZONTAL);
   clearRow->AddStretchSpacer();
   clearRow->Add(new wxButton(this, ID_CLEAR_KEY, wxT("Clear Saved Key")), 0);
   topSizer->Add(clearRow, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 5);

   topSizer->Add(new wxStaticText(this, wxID_ANY, wxT("Thinking effort (api mode only):")),
                 0, wxLEFT | wxTOP, 10);
   mThinkChoice = new wxChoice(this, wxID_ANY);
   mThinkChoice->Append(wxT("Fast (cheapest, quickest)"));
   mThinkChoice->Append(wxT("Balanced (recommended)"));
   mThinkChoice->Append(wxT("Thinking (slower, most careful)"));
   mThinkChoice->SetSelection(1);
   topSizer->Add(mThinkChoice, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);

   topSizer->Add(new wxStaticText(this, wxID_ANY,
      wxT("Advanced: override model ID (blank = sensible default for the effort above):")),
      0, wxLEFT | wxTOP, 10);
   mModelOverrideCtrl = new wxTextCtrl(this, wxID_ANY, wxEmptyString);
   topSizer->Add(mModelOverrideCtrl, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, 10);

   mStatusText = new wxStaticText(this, wxID_ANY, wxEmptyString);
   topSizer->Add(mStatusText, 0, wxLEFT | wxRIGHT | wxTOP, 10);

   topSizer->AddStretchSpacer();

   auto *btnRow = new wxBoxSizer(wxHORIZONTAL);
   btnRow->Add(new wxButton(this, wxID_SAVE, wxT("Save")), 0, wxALL, 5);
   btnRow->AddStretchSpacer();
   btnRow->Add(new wxButton(this, wxID_CLOSE, wxT("Close")), 0, wxALL, 5);
   topSizer->Add(btnRow, 0, wxEXPAND | wxALL, 5);

   SetSizer(topSizer);
}

void HgeAiSettings::LoadExisting()
{
   wxString path = ConfigPath();
   if (!wxFileExists(path))
      return;

   wxFile file(path, wxFile::read);
   if (!file.IsOpened())
      return;

   wxString json;
   file.ReadAll(&json);

   wxString key = JsonGetString(json, wxT("anthropic_api_key"));
   if (!key.IsEmpty())
   {
      mApiKeyCtrl->SetValue(key);
      mStatusText->SetLabel(wxT("A key is currently saved (client/BYOK mode is active)."));
   }
   else
   {
      mStatusText->SetLabel(wxT("No key saved -- using Van's personal subscription."));
   }

   wxString think = JsonGetString(json, wxT("think"));
   if (think == wxT("fast")) mThinkChoice->SetSelection(0);
   else if (think == wxT("thinking")) mThinkChoice->SetSelection(2);
   else mThinkChoice->SetSelection(1);

   wxString model = JsonGetString(json, wxT("model"));
   if (!model.IsEmpty())
      mModelOverrideCtrl->SetValue(model);
}

void HgeAiSettings::OnSave(wxCommandEvent &WXUNUSED(evt))
{
   static const wxChar *thinkValues[] = { wxT("fast"), wxT("balanced"), wxT("thinking") };
   wxString think = thinkValues[std::max(0, mThinkChoice->GetSelection())];

   wxString key = mApiKeyCtrl->GetValue().Trim().Trim(false);
   wxString model = mModelOverrideCtrl->GetValue().Trim().Trim(false);

   wxString json = wxT("{");
   bool first = true;
   auto addField = [&](const wxString &k, const wxString &v)
   {
      if (v.IsEmpty()) return;
      if (!first) json += wxT(",");
      json += wxT("\"") + k + wxT("\":\"") + JsonEscape(v) + wxT("\"");
      first = false;
   };
   addField(wxT("anthropic_api_key"), key);
   addField(wxT("model"), model);
   addField(wxT("think"), think);
   json += wxT("}");

   wxFile file(ConfigPath(), wxFile::write);
   if (file.IsOpened() && file.Write(json))
   {
      mStatusText->SetLabel(key.IsEmpty()
         ? wxT("Saved -- no key, so this stays on Van's personal subscription.")
         : wxT("Saved -- this build will now use the API key (billed to that key)."));
   }
   else
   {
      mStatusText->SetLabel(wxT("Couldn't write the settings file."));
   }
}

void HgeAiSettings::OnClearKey(wxCommandEvent &WXUNUSED(evt))
{
   mApiKeyCtrl->Clear();
   mStatusText->SetLabel(wxT("Key field cleared -- click Save to make it permanent."));
}

void HgeAiSettings::OnClose(wxCommandEvent &WXUNUSED(evt))
{
   Close();
}
