/**********************************************************************

 HGE Music Studio — Plugin Manager Panel

 Production-grade plugin management UI integrated into the
 HGE Music Studio preferences. Shows all bundled plugins,
 their status, architecture compatibility, and provides
 rescan/validation/quarantine management.

 NOTE: This panel is designed to be added to the Audacity
 Preferences dialog as a new page. Integration point:
   src/prefs/PrefsDialog.cpp — add a new PrefsPanel entry.

 **********************************************************************/

#include "PluginManagerPanel.h"

#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/listctrl.h>
#include <wx/textctrl.h>
#include <wx/choice.h>
#include <wx/msgdlg.h>
#include <wx/filedlg.h>
#include <wx/log.h>
#include <wx/menu.h>
#include <wx/notebook.h>
#include <wx/clipbrd.h>

#include "ShuttleGui.h"
#include "Prefs.h"

// Event table
BEGIN_EVENT_TABLE(PluginManagerPanel, wxPanel)
   EVT_BUTTON(wxID_ANY, PluginManagerPanel::OnRescanAll)
   EVT_LIST_ITEM_SELECTED(wxID_ANY, PluginManagerPanel::OnPluginSelected)
   EVT_LIST_ITEM_ACTIVATED(wxID_ANY, PluginManagerPanel::OnPluginActivated)
END_EVENT_TABLE()

// ─── Constructor ────────────────────────────────────────────────────────

PluginManagerPanel::PluginManagerPanel(wxWindow *parent, wxWindowID id,
                                       const wxPoint &pos, const wxSize &size)
   : wxPanel(parent, id, pos, size)
{
   BuildUi();
   PluginManagerModule::Get().RegisterUI(this);
   Populate();
}

PluginManagerPanel::~PluginManagerPanel()
{
   PluginManagerModule::Get().UnregisterUI();
}

// ─── IPluginManagerUI ──────────────────────────────────────────────────

void PluginManagerPanel::UpdatePluginList()
{
   RefreshList();
}

void PluginManagerPanel::ShowStatus(const wxString &msg)
{
   LogMessage(msg);
}

void PluginManagerPanel::ShowError(const wxString &msg)
{
   LogMessage(wxT("[ERROR] ") + msg);
}

// ─── Build UI ──────────────────────────────────────────────────────────

void PluginManagerPanel::BuildUi()
{
   auto *outerSizer = new wxBoxSizer(wxVERTICAL);

   // ── Header ──────────────────────────────────────────────────────────
   auto *headerText = new wxStaticText(this, wxID_ANY,
      wxT("Bundled Plugin Management\n")
      wxT("All plugins are loaded from inside the app bundle — ")
      wxT("no system install required."));
   headerText->Wrap(500);
   outerSizer->Add(headerText, 0, wxALL | wxEXPAND, 10);

   // ── Toolbar ─────────────────────────────────────────────────────────
   auto *toolbarSizer = new wxBoxSizer(wxHORIZONTAL);

   mFormatFilter = new wxChoice(this, wxID_ANY);
   mFormatFilter->Append(wxT("All Formats"));
   mFormatFilter->Append(wxT("VST2"));
   mFormatFilter->Append(wxT("VST3"));
   mFormatFilter->Append(wxT("LV2"));
   mFormatFilter->Append(wxT("Audio Units"));
   mFormatFilter->Append(wxT("Nyquist"));
   mFormatFilter->SetSelection(0);
   toolbarSizer->Add(new wxStaticText(this, wxID_ANY, wxT("Filter:")),
                     0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);
   toolbarSizer->Add(mFormatFilter, 0, wxRIGHT, 10);

   mRescanAllBtn = new wxButton(this, wxID_ANY, wxT("Rescan All"));
   toolbarSizer->Add(mRescanAllBtn, 0, wxRIGHT, 5);

   mRescanFormatBtn = new wxButton(this, wxID_ANY, wxT("Rescan Format"));
   toolbarSizer->Add(mRescanFormatBtn, 0, wxRIGHT, 5);

   mValidateBtn = new wxButton(this, wxID_ANY, wxT("Validate Selected"));
   toolbarSizer->Add(mValidateBtn, 0, wxRIGHT, 5);

   mShowInFinderBtn = new wxButton(this, wxID_ANY, wxT("Show in Finder"));
   toolbarSizer->Add(mShowInFinderBtn, 0, wxRIGHT, 5);

   outerSizer->Add(toolbarSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 10);

   // ── Plugin List ─────────────────────────────────────────────────────
   mPluginList = new wxListCtrl(this, wxID_ANY,
                                 wxDefaultPosition, wxSize(-1, 250),
                                 wxLC_REPORT | wxLC_SINGLE_SEL | wxLC_SORT_ASCENDING);

   mPluginList->AppendColumn(wxT("Plugin Name"), wxLIST_FORMAT_LEFT, 220);
   mPluginList->AppendColumn(wxT("Type"),        wxLIST_FORMAT_LEFT, 70);
   mPluginList->AppendColumn(wxT("Architecture"), wxLIST_FORMAT_LEFT, 110);
   mPluginList->AppendColumn(wxT("Version"),     wxLIST_FORMAT_LEFT, 80);
   mPluginList->AppendColumn(wxT("Status"),      wxLIST_FORMAT_LEFT, 100);

   outerSizer->Add(mPluginList, 1, wxEXPAND | wxLEFT | wxRIGHT, 10);

   // ── Detail Panel ────────────────────────────────────────────────────
   auto *detailBox = new wxStaticBox(this, wxID_ANY, wxT("Plugin Details"));
   auto *detailSizer = new wxStaticBoxSizer(detailBox, wxVERTICAL);

   auto *detailGrid = new wxFlexGridSizer(2, 5, 5);
   detailGrid->AddGrowableCol(1);

   detailGrid->Add(new wxStaticText(this, wxID_ANY, wxT("Name:")), 0, wxALIGN_RIGHT);
   mDetailName = new wxStaticText(this, wxID_ANY, wxT("-"));
   detailGrid->Add(mDetailName, 0, wxEXPAND);

   detailGrid->Add(new wxStaticText(this, wxID_ANY, wxT("Type:")), 0, wxALIGN_RIGHT);
   mDetailType = new wxStaticText(this, wxID_ANY, wxT("-"));
   detailGrid->Add(mDetailType, 0, wxEXPAND);

   detailGrid->Add(new wxStaticText(this, wxID_ANY, wxT("Architecture:")), 0, wxALIGN_RIGHT);
   mDetailArch = new wxStaticText(this, wxID_ANY, wxT("-"));
   detailGrid->Add(mDetailArch, 0, wxEXPAND);

   detailGrid->Add(new wxStaticText(this, wxID_ANY, wxT("Version:")), 0, wxALIGN_RIGHT);
   mDetailVersion = new wxStaticText(this, wxID_ANY, wxT("-"));
   detailGrid->Add(mDetailVersion, 0, wxEXPAND);

   detailGrid->Add(new wxStaticText(this, wxID_ANY, wxT("Vendor:")), 0, wxALIGN_RIGHT);
   mDetailVendor = new wxStaticText(this, wxID_ANY, wxT("-"));
   detailGrid->Add(mDetailVendor, 0, wxEXPAND);

   detailGrid->Add(new wxStaticText(this, wxID_ANY, wxT("Status:")), 0, wxALIGN_RIGHT);
   mDetailStatus = new wxStaticText(this, wxID_ANY, wxT("-"));
   detailGrid->Add(mDetailStatus, 0, wxEXPAND);

   detailGrid->Add(new wxStaticText(this, wxID_ANY, wxT("Path:")), 0, wxALIGN_RIGHT);
   mDetailPath = new wxStaticText(this, wxID_ANY, wxT("-"));
   mDetailPath->Wrap(400);
   detailGrid->Add(mDetailPath, 0, wxEXPAND);

   detailSizer->Add(detailGrid, 0, wxEXPAND | wxALL, 5);

   // Quarantine buttons
   auto *quarantineSizer = new wxBoxSizer(wxHORIZONTAL);
   mUnquarantineBtn = new wxButton(this, wxID_ANY, wxT("Release from Quarantine"));
   mUnquarantineBtn->Enable(false);
   quarantineSizer->Add(mUnquarantineBtn, 0, wxRIGHT, 5);

   mClearQuarantineBtn = new wxButton(this, wxID_ANY, wxT("Clear All Quarantine"));
   quarantineSizer->Add(mClearQuarantineBtn, 0);
   detailSizer->Add(quarantineSizer, 0, wxALL, 5);

   outerSizer->Add(detailBox, 0, wxEXPAND | wxALL, 10);

   // ── Log ─────────────────────────────────────────────────────────────
   auto *logBox = new wxStaticBox(this, wxID_ANY, wxT("Plugin Log"));
   auto *logSizer = new wxStaticBoxSizer(logBox, wxVERTICAL);

   mLogView = new wxTextCtrl(this, wxID_ANY, wxEmptyString,
                              wxDefaultPosition, wxSize(-1, 120),
                              wxTE_MULTILINE | wxTE_READONLY | wxTE_DONTWRAP);
   logSizer->Add(mLogView, 1, wxEXPAND);
   outerSizer->Add(logBox, 0, wxEXPAND | wxALL, 10);

   SetSizer(outerSizer);
   outerSizer->SetSizeHints(this);
   Layout();
}

// ─── Populate ──────────────────────────────────────────────────────────

void PluginManagerPanel::Populate()
{
   PluginManagerModule::Get().Initialize();
   RefreshList();
   LogMessage(wxT("Plugin Manager ready. Bundled plugins: ")
            + wxString::Format(wxT("%zu"), PluginManagerModule::Get().GetAllPlugins().size()));
}

// ─── Refresh List ──────────────────────────────────────────────────────

void PluginManagerPanel::RefreshList()
{
   mPluginList->DeleteAllItems();

   auto &pm = PluginManagerModule::Get();
   auto plugins = pm.GetAllPlugins();

   wxString filter = mFormatFilter->GetStringSelection();

   for (const auto &p : plugins)
   {
      // Apply filter
      if (filter != wxT("All Formats"))
      {
         wxString filterType;
         if (filter == wxT("VST2")) filterType = wxT("VST2");
         else if (filter == wxT("VST3")) filterType = wxT("VST3");
         else if (filter == wxT("LV2")) filterType = wxT("LV2");
         else if (filter == wxT("Audio Units")) filterType = wxT("AU");
         else if (filter == wxT("Nyquist")) filterType = wxT("Nyquist");

         if (p.type != filterType) continue;
      }

      long idx = mPluginList->InsertItem(mPluginList->GetItemCount(), p.name);
      mPluginList->SetItem(idx, 1, p.type);
      mPluginList->SetItem(idx, 2, p.arch);
      mPluginList->SetItem(idx, 3, p.version);
      mPluginList->SetItem(idx, 4, p.isValid ? wxT("Active") : wxT("Invalid"));
      mPluginList->SetItemData(idx, reinterpret_cast<long>(&p));
   }

   // Update status display
   size_t active = 0, quarantined = 0;
   for (const auto &p : plugins)
   {
      if (p.isValid) active++;
      if (p.isQuarantined) quarantined++;
   }

   LogMessage(wxString::Format(wxT("Display: %zu active, %zu quarantined"),
              active, quarantined));
}

// ─── Update Details ────────────────────────────────────────────────────

void PluginManagerPanel::UpdateDetails(const PluginBundle *bundle)
{
   if (!bundle)
   {
      mDetailName->SetLabel(wxT("-"));
      mDetailType->SetLabel(wxT("-"));
      mDetailArch->SetLabel(wxT("-"));
      mDetailVersion->SetLabel(wxT("-"));
      mDetailVendor->SetLabel(wxT("-"));
      mDetailPath->SetLabel(wxT("-"));
      mDetailStatus->SetLabel(wxT("-"));
      mSelectedPluginPath.Clear();
      mValidateBtn->Enable(false);
      mShowInFinderBtn->Enable(false);
      return;
   }

   mDetailName->SetLabel(bundle->name);
   mDetailType->SetLabel(bundle->type);
   mDetailArch->SetLabel(bundle->arch);
   mDetailVersion->SetLabel(bundle->version);
   mDetailVendor->SetLabel(bundle->vendor);
   mDetailPath->SetLabel(bundle->path);

   if (bundle->isQuarantined)
      mDetailStatus->SetLabel(wxT("⚠ QUARANTINED: ") + bundle->quarantineReason);
   else if (bundle->isValid)
      mDetailStatus->SetLabel(wxT("✓ Active"));
   else
      mDetailStatus->SetLabel(wxT("✗ Invalid"));

   mSelectedPluginPath = bundle->path;
   mValidateBtn->Enable(true);
   mShowInFinderBtn->Enable(true);
}

// ─── Event Handlers ────────────────────────────────────────────────────

void PluginManagerPanel::OnRescanAll(wxCommandEvent &WXUNUSED(evt))
{
   LogMessage(wxT("Starting full rescan..."));
   PluginManagerModule::Get().RescanAll();
   LogMessage(wxT("Full rescan complete."));
}

void PluginManagerPanel::OnRescanFormat(wxCommandEvent &WXUNUSED(evt))
{
   wxString format = mFormatFilter->GetStringSelection();
   if (format == wxT("All Formats"))
   {
      OnRescanAll(evt);
      return;
   }

   LogMessage(wxString::Format(wxT("Rescanning format: %s..."), format));
   PluginManagerModule::Get().RescanFormat(format);
   LogMessage(wxT("Format rescan complete."));
}

void PluginManagerPanel::OnPluginSelected(wxListEvent &evt)
{
   long idx = evt.GetIndex();
   if (idx < 0) return;

   PluginBundle *bundle = reinterpret_cast<PluginBundle *>(
      mPluginList->GetItemData(idx));
   if (bundle)
      UpdateDetails(bundle);
}

void PluginManagerPanel::OnPluginActivated(wxListEvent &WXUNUSED(evt))
{
   // Double-click: validate and show details
   if (!mSelectedPluginPath.IsEmpty())
   {
      auto result = PluginValidator::Get().ValidateBinary(mSelectedPluginPath);
      if (result.valid)
         wxMessageBox(wxT("Plugin validated successfully.\n\n")
                     wxT("Architecture: ") + result.arch + wxT("\n")
                     wxT("Signed: ") + (result.isSigned ? wxT("Yes") : wxT("No")),
                     wxT("Validation Result"),
                     wxOK | wxICON_INFORMATION, this);
      else
         wxMessageBox(wxT("Plugin validation FAILED:\n\n") + result.error,
                     wxT("Validation Failed"),
                     wxOK | wxICON_ERROR, this);
   }
}

void PluginManagerPanel::OnUnquarantine(wxCommandEvent &WXUNUSED(evt))
{
   if (mSelectedPluginPath.IsEmpty()) return;
   PluginManagerModule::Get().UnquarantinePlugin(mSelectedPluginPath);
   RefreshList();
   LogMessage(wxT("Released from quarantine: ") + mSelectedPluginPath);
}

void PluginManagerPanel::OnClearQuarantine(wxCommandEvent &WXUNUSED(evt))
{
   PluginManagerModule::Get().ClearQuarantine();
   RefreshList();
   LogMessage(wxT("All quarantine records cleared."));
}

void PluginManagerPanel::OnValidateSelected(wxCommandEvent &WXUNUSED(evt))
{
   if (mSelectedPluginPath.IsEmpty()) return;

   LogMessage(wxT("Validating: ") + mSelectedPluginPath);
   auto result = PluginValidator::Get().ValidateBinary(mSelectedPluginPath);

   if (result.valid)
   {
      LogMessage(wxT("  ✓ Valid — Architecture: ") + result.arch
               + wxT(", Signed: ") + (result.isSigned ? wxT("Yes") : wxT("No")));
      if (result.warningCount > 0)
         LogMessage(wxT("  ⚠ Warnings:\n") + result.warnings);
   }
   else
   {
      LogMessage(wxT("  ✗ FAILED: ") + result.error);
   }
}

void PluginManagerPanel::OnShowInFinder(wxCommandEvent &WXUNUSED(evt))
{
   if (mSelectedPluginPath.IsEmpty()) return;

   wxString path = mSelectedPluginPath;
   // Reveal in Finder
   wxString cmd = wxT("open -R \"") + path + wxT("\"");
   wxExecute(cmd, wxEXEC_ASYNC, nullptr);
}

// ─── Log ───────────────────────────────────────────────────────────────

void PluginManagerPanel::LogMessage(const wxString &msg)
{
   if (!mLogView) return;

   wxDateTime now = wxDateTime::Now();
   mLogView->AppendText(now.FormatTime() + wxT("  ") + msg + wxT("\n"));

   // Auto-scroll to bottom
   mLogView->SetInsertionPointEnd();
}
