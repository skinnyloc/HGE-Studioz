/**********************************************************************

 HGE Music Studio — Custom Effect Browser Implementation

 A modern, categorized plugin browser that hides technical details
 and provides one-click access to all bundled effects.

 =========================================================================
 ARCHITECTURE
 =========================================================================

 Data flow:
   1. PluginCategoryManager provides curated categories & plugin entries
   2. PluginDisplayName maps raw names to consumer-friendly names
   3. EffectManager looks up command IDs to apply effects
   4. App's PluginManager provides the actual plugin descriptors

 Category order (hardcoded for consistency):
   All, EQ, Dynamics, Pitch Correction, Reverb & Delay,
   Mastering, Utility, Favorites, Recent

 =========================================================================
 INTEGRATION NOTES
 =========================================================================

 - This panel opens as a standalone dialog from Tools → HGE Effect Browser
 - The stock Effects menu remains fully functional (fallback)
 - Effect application uses the app's CommandManager system
 - On double-click or Apply button, the effect dialog opens
 - The browser tracks favorites and recent usage via PluginCategoryManager

 =========================================================================
 STOCK MENU PATH (for reference while testing)
 =========================================================================

 If the effect doesn't apply from this browser, use the stock path:
   Effect → [Type] → [Vendor] → [Plugin Name]
 As future work, the browser will fully replace this flow.

 **********************************************************************/

#include "HgeEffectBrowser.h"

#include <algorithm>
#include <chrono>
#include <sstream>

#include <wx/artprov.h>
#include <wx/button.h>
#include <wx/colordlg.h>
#include <wx/dcclient.h>
#include <wx/font.h>
#include <wx/gbsizer.h>
#include <wx/icon.h>
#include <wx/image.h>
#include <wx/imaglist.h>
#include <wx/msgdlg.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/statline.h>
#include <wx/timer.h>
#include <wx/utils.h>

#include "EffectManager.h"
#include "PluginManager.h"
#include "CommandManager.h"
#include "PluginDescriptor.h"
#include "PluginDisplayName.h"

// ─── Color Constants ──────────────────────────────────────────────────────

const wxColour HgeEffectBrowser::kBgColor          = wxColour(30, 30, 30);
const wxColour HgeEffectBrowser::kCategoryBg        = wxColour(38, 38, 38);
const wxColour HgeEffectBrowser::kSelectedCategory  = wxColour(60, 60, 60);
const wxColour HgeEffectBrowser::kCertifiedBadge    = wxColour(212, 175, 55);  // gold
const wxColour HgeEffectBrowser::kStarColor         = wxColour(255, 200, 0);   // yellow

// ─── Event Table ──────────────────────────────────────────────────────────

BEGIN_EVENT_TABLE(HgeEffectBrowser, wxDialog)
   EVT_LIST_ITEM_SELECTED(wxID_ANY, HgeEffectBrowser::OnPluginSelected)
   EVT_LIST_ITEM_ACTIVATED(wxID_ANY, HgeEffectBrowser::OnPluginActivated)
   EVT_LISTBOX(wxID_ANY, HgeEffectBrowser::OnCategorySelected)
   EVT_BUTTON(wxID_ANY, HgeEffectBrowser::OnApplyEffect)
   EVT_SEARCHCTRL_SEARCH_BTN(wxID_ANY, HgeEffectBrowser::OnSearch)
   EVT_TEXT(wxID_ANY, HgeEffectBrowser::OnSearch)
   EVT_TIMER(wxID_ANY, HgeEffectBrowser::OnSearchTimer)
   EVT_CLOSE(HgeEffectBrowser::OnClose)
END_EVENT_TABLE()

// ─── Constructor ──────────────────────────────────────────────────────────

HgeEffectBrowser::HgeEffectBrowser(wxWindow *parent)
   : wxDialog(parent, wxID_ANY, wxT("HGE Effect Browser"),
              wxDefaultPosition, wxSize(900, 650),
              wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER | wxMAXIMIZE_BOX)
   , mSearchTimer(this, wxID_ANY)
   , mIsSearching(false)
{
   // Initialize category definitions
   mCategories = {
      {"__all__",    " All Plugins",       "⚡"},
      {"EQ",         " EQ",                "🎛"},
      {"Dynamics",   " Dynamics",          "📊"},
      {"Pitch Correction", " Pitch Corr.",  "🎤"},
      {"Reverb & Delay",   " Reverb & Delay", "⏳"},
      {"Mastering",  " Mastering",         "🎚"},
      {"Utility",    " Utility",           "🔧"},
      {"__fav__",    " ★ Favorites",       "★"},
      {"__recent__", " ⏰ Recent",          "⏰"},
   };

   // Load data from PluginCategoryManager
   auto &catMgr = PluginCategoryManager::Get();
   catMgr.LoadBuiltinCategories();
   mAllPlugins = catMgr.GetAllPlugins();

   // Sort by category order
   std::sort(mAllPlugins.begin(), mAllPlugins.end(),
      [](const PluginEntry &a, const PluginEntry &b) {
         if (a.category != b.category)
            return a.sortOrder < b.sortOrder;
         return a.displayName < b.displayName;
      });

   // Build UI
   BuildUi();

   // Populate initial data
   PopulateCategories();
   PopulatePlugins("__all__");

   SetTitle(wxT("HGE Effect Browser"));
   SetMinSize(wxSize(700, 450));
   CentreOnParent();
}

HgeEffectBrowser::~HgeEffectBrowser()
{
   // Save favorites and recent state
   auto &catMgr = PluginCategoryManager::Get();
   wxString statePath = wxStandardPaths::Get().GetUserDataDir()
                       + wxFILE_SEP_PATH + wxT("plugin-state.json");
   catMgr.SaveState(statePath.ToStdString());
}

// ─── UI Construction ──────────────────────────────────────────────────────

void HgeEffectBrowser::BuildUi()
{
   auto *mainSizer = new wxBoxSizer(wxVERTICAL);

   // ── Toolbar (search + action buttons) ──────────────────────────────
   BuildToolbar(this, mainSizer);

   // ── Separator ──────────────────────────────────────────────────────
   mainSizer->Add(new wxStaticLine(this, wxID_ANY),
                  0, wxEXPAND | wxLEFT | wxRIGHT, 10);

   // ── Splitter: Categories | Plugin List ─────────────────────────────
   auto *splitter = new wxSplitterWindow(this, wxID_ANY,
                                          wxDefaultPosition, wxDefaultSize,
                                          wxSP_3DSASH | wxSP_LIVE_UPDATE);
   splitter->SetMinimumPaneSize(150);

   BuildCategoryPanel(splitter);
   BuildPluginPanel(splitter);

   splitter->SplitVertically(mCategoryList, mPluginList, 200);
   mainSizer->Add(splitter, 1, wxEXPAND | wxALL, 10);

   // ── Detail Panel (collapsible at bottom) ───────────────────────────
   BuildDetailPanel(this, mainSizer);

   // ── Bottom buttons ─────────────────────────────────────────────────
   auto *bottomSizer = new wxBoxSizer(wxHORIZONTAL);
   bottomSizer->AddStretchSpacer();
   auto *closeBtn = new wxButton(this, wxID_CANCEL, wxT("Close"));
   bottomSizer->Add(closeBtn, 0, wxRIGHT, 5);
   mainSizer->Add(bottomSizer, 0, wxEXPAND | wxALL, 10);

   SetSizer(mainSizer);
   Layout();
}

void HgeEffectBrowser::BuildToolbar(wxWindow *parent, wxBoxSizer *topSizer)
{
   auto *toolbar = new wxPanel(parent, wxID_ANY);
   auto *toolSizer = new wxBoxSizer(wxHORIZONTAL);

   // Search
   mSearchCtrl = new wxSearchCtrl(toolbar, wxID_ANY, wxEmptyString,
                                   wxDefaultPosition, wxSize(300, -1),
                                   wxTE_PROCESS_ENTER);
   mSearchCtrl->SetDescriptiveText(wxT("Search effects..."));
   mSearchCtrl->ShowSearchButton(true);
   mSearchCtrl->ShowCancelButton(true);
   toolSizer->Add(mSearchCtrl, 0, wxALIGN_CENTER_VERTICAL | wxALL, 8);

   toolSizer->AddStretchSpacer();

   // Plugin count
   auto *countText = new wxStaticText(toolbar, wxID_ANY,
      wxString::Format(wxT("%zu plugins available"), mAllPlugins.size()));
   toolSizer->Add(countText, 0, wxALIGN_CENTER_VERTICAL | wxALL, 8);

   toolbar->SetSizer(toolSizer);
   topSizer->Add(toolbar, 0, wxEXPAND);
}

void HgeEffectBrowser::BuildCategoryPanel(wxWindow *parent)
{
   mCategoryList = new wxListBox(parent, wxID_ANY,
                                  wxDefaultPosition, wxDefaultSize,
                                  0, nullptr,
                                  wxLB_SINGLE | wxNO_BORDER);

   // Style the listbox for dark theme
   mCategoryList->SetBackgroundColour(kCategoryBg);
   mCategoryList->SetForegroundColour(*wxWHITE);
   mCategoryList->SetFont(wxFont(12, wxFONTFAMILY_DEFAULT,
                                 wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
}

void HgeEffectBrowser::BuildPluginPanel(wxWindow *parent)
{
   // Plugin list in report mode with custom columns
   mPluginList = new wxListCtrl(parent, wxID_ANY,
                                 wxDefaultPosition, wxDefaultSize,
                                 wxLC_REPORT | wxLC_SINGLE_SEL |
                                 wxLC_NO_HEADER | wxBORDER_NONE);

   // Columns: Icon/Star | Name | Badge | Apply
   mPluginList->AppendColumn(wxT(""), wxLIST_FORMAT_LEFT, 40);   // star icon
   mPluginList->AppendColumn(wxT("Plugin"), wxLIST_FORMAT_LEFT, 300); // name
   mPluginList->AppendColumn(wxT(""), wxLIST_FORMAT_CENTER, 80); // HGE badge
   mPluginList->AppendColumn(wxT(""), wxLIST_FORMAT_CENTER, 80); // action

   mPluginList->SetBackgroundColour(kBgColor);
   mPluginList->SetForegroundColour(*wxWHITE);
   mPluginList->SetFont(wxFont(11, wxFONTFAMILY_DEFAULT,
                               wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
}

void HgeEffectBrowser::BuildDetailPanel(wxWindow *parent, wxBoxSizer *topSizer)
{
   mDetailPanel = new wxPanel(parent, wxID_ANY);
   auto *detailSizer = new wxBoxSizer(wxHORIZONTAL);

   // Detail text
   auto *textSizer = new wxBoxSizer(wxVERTICAL);
   mDetailTitle = new wxStaticText(mDetailPanel, wxID_ANY, wxT(""),
                                    wxDefaultPosition, wxDefaultSize,
                                    wxST_ELLIPSIZE_END);
   mDetailTitle->SetFont(wxFont(11, wxFONTFAMILY_DEFAULT,
                                wxFONTSTYLE_NORMAL, wxFONTWEIGHT_BOLD));
   mDetailTitle->SetForegroundColour(*wxWHITE);

   mDetailCategory = new wxStaticText(mDetailPanel, wxID_ANY, wxT(""));
   mDetailCategory->SetFont(wxFont(9, wxFONTFAMILY_DEFAULT,
                                   wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL));
   mDetailCategory->SetForegroundColour(wxColour(180, 180, 180));

   textSizer->Add(mDetailTitle, 0, wxBOTTOM, 2);
   textSizer->Add(mDetailCategory, 0);
   detailSizer->Add(textSizer, 1, wxALIGN_CENTER_VERTICAL | wxALL, 8);

   // Action buttons
   mApplyBtn = new wxButton(mDetailPanel, wxID_ANY, wxT("Apply Effect"));
   mApplyBtn->SetDefault();
   mApplyBtn->Enable(false);
   detailSizer->Add(mApplyBtn, 0, wxALIGN_CENTER_VERTICAL | wxRIGHT, 5);

   mFavBtn = new wxButton(mDetailPanel, wxID_ANY, wxT("☆ Favorite"));
   mFavBtn->Enable(false);
   detailSizer->Add(mFavBtn, 0, wxALIGN_CENTER_VERTICAL);

   mDetailPanel->SetSizer(detailSizer);
   mDetailPanel->SetBackgroundColour(wxColour(45, 45, 45));
   mDetailPanel->Hide(); // Hidden until a plugin is selected
   topSizer->Add(mDetailPanel, 0, wxEXPAND | wxLEFT | wxRIGHT, 10);
}

// ─── Data Population ──────────────────────────────────────────────────────

void HgeEffectBrowser::PopulateCategories()
{
   mCategoryList->Clear();

   for (const auto &cat : mCategories)
   {
      int count = 0;
      if (cat.id == "__all__")
      {
         count = mAllPlugins.size();
      }
      else if (cat.id == "__fav__")
      {
         count = PluginCategoryManager::Get().GetStarredPlugins().size();
      }
      else if (cat.id == "__recent__")
      {
         count = PluginCategoryManager::Get().GetRecentPlugins(10).size();
      }
      else
      {
         auto plugins = PluginCategoryManager::Get().GetPluginsByCategory(cat.id);
         count = plugins.size();
      }

      wxString label;
      if (cat.id == "__all__" || cat.id == "__fav__" || cat.id == "__recent__")
         label = wxString::FromUTF8(cat.icon + " " + cat.label);
      else
         label = wxString::FromUTF8(cat.icon + cat.label);

      label += wxString::Format(wxT("  (%d)"), count);
      mCategoryList->Append(label);
   }

   // Select first category (All Plugins)
   mCategoryList->SetSelection(0);
}

void HgeEffectBrowser::PopulatePlugins(const std::string &category)
{
   mPluginList->DeleteAllItems();
   mFilteredPlugins.clear();
   mCurrentCategory = category;

   std::vector<PluginEntry> plugins;

   if (category == "__all__")
   {
      plugins = mAllPlugins;
   }
   else if (category == "__fav__")
   {
      plugins = PluginCategoryManager::Get().GetStarredPlugins();
   }
   else if (category == "__recent__")
   {
      plugins = PluginCategoryManager::Get().GetRecentPlugins(25);
   }
   else
   {
      plugins = PluginCategoryManager::Get().GetPluginsByCategory(category);
   }

   // Apply search filter if active
   if (mIsSearching)
   {
      wxString query = mSearchCtrl->GetValue();
      if (!query.IsEmpty())
      {
         plugins = PluginCategoryManager::Get().Search(query.ToStdString());
      }
   }

   // Populate the list
   for (const auto &p : plugins)
   {
      long idx = mPluginList->InsertItem(mPluginList->GetItemCount(), wxT(""));
      
      // Star column
      bool starred = PluginCategoryManager::Get().IsStarred(p.internalName);
      mPluginList->SetItem(idx, 0, starred ? wxT("★") : wxT("☆"));
      
      // Name column
      mPluginList->SetItem(idx, 1, wxString::FromUTF8(p.displayName));
      
      // HGE Certified badge
      mPluginList->SetItem(idx, 2,
         p.hgeCertified ? wxT("HGE") : wxT(""));
      
      // Action column
      mPluginList->SetItem(idx, 3, wxT("Apply"));

      // Store pointer to plugin entry for lookup
      PluginEntry *entry = new PluginEntry(p);
      mPluginList->SetItemData(idx, reinterpret_cast<long>(entry));
      mFilteredPlugins.push_back(p);
   }

   // Update detail panel
   ClearDetails();

   // Update category counts in the sidebar
   UpdateCategoryCounts();
}

void HgeEffectBrowser::UpdateCategoryCounts()
{
   // This would require rebuilding the listbox, which is expensive.
   // For simplicity, counts are set once in PopulateCategories.
   // A full refresh could recalculate here.
}

// ─── Event Handlers ───────────────────────────────────────────────────────

void HgeEffectBrowser::OnCategorySelected(wxCommandEvent &WXUNUSED(evt))
{
   int selection = mCategoryList->GetSelection();
   if (selection < 0 || selection >= (int)mCategories.size()) return;

   const auto &cat = mCategories[selection];
   PopulatePlugins(cat.id);
}

void HgeEffectBrowser::OnPluginActivated(wxListEvent &evt)
{
   long idx = evt.GetIndex();
   if (idx < 0) return;

   PluginEntry *entry = reinterpret_cast<PluginEntry *>(
      mPluginList->GetItemData(idx));
   if (!entry) return;

   // Record usage
   PluginCategoryManager::Get().RecordUse(entry->internalName);

   // Apply the effect
   ApplyEffectById(entry->internalName);
}

void HgeEffectBrowser::OnPluginSelected(wxListEvent &evt)
{
   long idx = evt.GetIndex();
   if (idx < 0) { ClearDetails(); return; }

   PluginEntry *entry = reinterpret_cast<PluginEntry *>(
      mPluginList->GetItemData(idx));
   if (entry)
   {
      ShowPluginDetails(entry);
   }
}

void HgeEffectBrowser::OnApplyEffect(wxCommandEvent &WXUNUSED(evt))
{
   if (mSelectedPlugin.empty()) return;

   // Record usage
   PluginCategoryManager::Get().RecordUse(mSelectedPlugin);

   // Apply the effect
   ApplyEffectById(mSelectedPlugin);
}

void HgeEffectBrowser::OnToggleFavorite(wxCommandEvent &WXUNUSED(evt))
{
   if (mSelectedPlugin.empty()) return;

   PluginCategoryManager::Get().ToggleStar(mSelectedPlugin);

   // Update star display in the list
   bool starred = PluginCategoryManager::Get().IsStarred(mSelectedPlugin);
   mFavBtn->SetLabel(starred ? wxT("★ Favorited") : wxT("☆ Favorite"));

   // Refresh current view to update stars
   PopulatePlugins(mCurrentCategory);
}

void HgeEffectBrowser::OnSearch(wxCommandEvent &WXUNUSED(evt))
{
   // Debounce search with a 300ms timer
   mIsSearching = true;
   mSearchTimer.Start(300, wxTIMER_ONE_SHOT);
}

void HgeEffectBrowser::OnSearchTimer(wxTimerEvent &WXUNUSED(evt))
{
   wxString query = mSearchCtrl->GetValue();
   if (query.IsEmpty())
   {
      mIsSearching = false;
      PopulatePlugins(mCurrentCategory);
   }
   else
   {
      PopulatePlugins("__all__");
   }
}

void HgeEffectBrowser::OnClose(wxCommandEvent &WXUNUSED(evt))
{
   Close();
}

// ─── Effect Application ───────────────────────────────────────────────────

bool HgeEffectBrowser::ApplyEffectById(const std::string &pluginId)
{
   // Look up the command ID for this plugin
   std::string commandId = FindPluginCommandId(pluginId);
   if (commandId.empty())
   {
      wxMessageBox(
         wxString::Format(wxT("Could not find effect: %s\n\n")
                          wxT("The plugin may need to be rescanned.\n")
                          wxT("Use the stock Effects menu as fallback:\n")
                          wxT("  Effect → AU → %s"),
                          pluginId, pluginId),
         wxT("Effect Not Found"),
         wxOK | wxICON_INFORMATION, this);
      return false;
   }

   // Try to apply the effect through the EffectManager
   auto &effectManager = EffectManager::Get();

   // Find the effect
   auto effect = effectManager.GetEffect(commandId);
   if (!effect)
   {
      wxLogWarning("HGE Browser: Effect not found via EffectManager: %s",
                    commandId);
      return false;
   }

   // Show the effect interface (opens the effect dialog)
   effect->ShowInterface();

   wxLogMessage("HGE Browser: Applied effect: %s (command: %s)",
                pluginId, commandId);
   return true;
}

std::string HgeEffectBrowser::FindPluginCommandId(const std::string &internalName) const
{
   // Search the app's plugin registry for a matching plugin
   // The registry stores plugins with command IDs that follow patterns:
   //
   //   AU:          AudioUnit Effect {manufacturer}_{name}
   //   VST2:        VST Effect {name}
   //   VST3:        VST3 Effect {name}
   //   Nyquist:     Nyquist Effect {name}
   //   Builtin:     {name}
   //
   // We search by internal name across all registered plugins

   auto &pluginManager = PluginManager::Get();
   const auto &reg = pluginManager.GetRegistry();

   // Search all registered plugins
   for (const auto &[id, desc] : reg)
   {
      // Check if the plugin's name matches
      wxString pluginName = desc.GetSymbol().Internal();
      if (pluginName.Lower() == wxString(internalName).Lower())
      {
         return id;
      }

      // Also check display name
      pluginName = desc.GetName();
      if (pluginName.Lower() == wxString(internalName).Lower())
      {
         return id;
      }

      // Check vendor-prefixed name (e.g., "Tokyo Dawn Labs: TDR Nova")
      wxString vendorName = desc.GetVendor();
      if (!vendorName.IsEmpty())
      {
         wxString fullName = vendorName + wxT(": ") + desc.GetName();
         if (fullName.Lower() == wxString(internalName).Lower())
         {
            return id;
         }
      }
   }

   // If not found by exact match, try to match just the name part
   // (after stripping vendor prefix)
   for (const auto &[id, desc] : reg)
   {
      wxString name = desc.GetName();
      if (name.Lower().find(wxString(internalName).Lower()) != wxString::npos)
      {
         return id;
      }
   }

   return "";
}

// ─── Helpers ──────────────────────────────────────────────────────────────

void HgeEffectBrowser::ShowPluginDetails(const PluginEntry *entry)
{
   if (!entry) { ClearDetails(); return; }

   mSelectedPlugin = entry->internalName;

   mDetailTitle->SetLabel(wxString::FromUTF8(entry->displayName));
   mDetailCategory->SetLabel(wxString::FromUTF8(entry->category));

   // Update apply button
   mApplyBtn->Enable(true);

   // Update favorite button
   bool starred = PluginCategoryManager::Get().IsStarred(entry->internalName);
   mFavBtn->SetLabel(starred ? wxT("★ Favorited") : wxT("☆ Favorite"));
   mFavBtn->Enable(true);

   // Show detail panel
   if (!mDetailPanel->IsShown())
      mDetailPanel->Show();
   Layout();
}

void HgeEffectBrowser::ClearDetails()
{
   mSelectedPlugin.clear();
   mDetailTitle->SetLabel(wxT(""));
   mDetailCategory->SetLabel(wxT(""));
   mApplyBtn->Enable(false);
   mFavBtn->Enable(false);
}

int HgeEffectBrowser::GetCategoryIndex(const std::string &cat) const
{
   for (size_t i = 0; i < mCategories.size(); i++)
   {
      if (mCategories[i].id == cat)
         return (int)i;
   }
   return -1;
}

// ─── PluginListRenderer ──────────────────────────────────────────────────

PluginListRenderer::PluginListRenderer(wxWindow *parent, const wxWindowID id,
                                       const wxPoint &pos, const wxSize &size,
                                       long style)
   : wxListCtrl(parent, id, pos, size, style)
{
}

void PluginListRenderer::SetHgeCertified(long item, bool certified)
{
   mCertifiedMap[item] = certified;
}

void PluginListRenderer::SetStarred(long item, bool starred)
{
   mStarredMap[item] = starred;
}

bool PluginListRenderer::IsHgeCertified(long item) const
{
   auto it = mCertifiedMap.find(item);
   return it != mCertifiedMap.end() && it->second;
}

bool PluginListRenderer::IsStarred(long item) const
{
   auto it = mStarredMap.find(item);
   return it != mStarredMap.end() && it->second;
}
