/**********************************************************************

 HGE Music Studio — Custom Effect Browser

 Replaces the stock Audacity Effects menu with a modern, categorized
 browser that hides technical details (VST/AU/Nyquist) and shows
 plugins by musician-friendly categories: EQ, Dynamics, Reverb, etc.

 Features:
   - Category sidebar with plugin counts
   - Real-time search across all plugins
   - One-click effect application
   - Favorites / Recent plugins
   - HGE Certified badges for premium plugins
   - Clean UX — no vendor, format, or path info

 Non-destructive: the stock Effects menu remains fully functional
 as a fallback. This browser is additive.

 **********************************************************************/

#ifndef __HGE_EFFECT_BROWSER_H__
#define __HGE_EFFECT_BROWSER_H__

#include <memory>
#include <vector>
#include <string>
#include <map>

#include <wx/dialog.h>
#include <wx/panel.h>
#include <wx/listbox.h>
#include <wx/listctrl.h>
#include <wx/searchctrl.h>
#include <wx/splitter.h>
#include <wx/statbmp.h>
#include <wx/stattext.h>
#include <wx/button.h>
#include <wx/textctrl.h>
#include <wx/timer.h>

#include "../../../libraries/lib-plugin-curation/PluginCategoryManager.h"

// Forward declarations
class Effect;
class ShuttleGui;

// -----------------------------------------------------------------------
// HgeEffectBrowser — The main categorized plugin browser
// -----------------------------------------------------------------------
class HgeEffectBrowser final : public wxDialog
{
public:
   HgeEffectBrowser(wxWindow *parent);
   ~HgeEffectBrowser() override;

private:
   // UI construction
   void BuildUi();
   void BuildToolbar(wxWindow *parent, wxBoxSizer *topSizer);
   void BuildCategoryPanel(wxWindow *parent);
   void BuildPluginPanel(wxWindow *parent);
   void BuildDetailPanel(wxWindow *parent, wxBoxSizer *topSizer);

   // Data population
   void PopulateCategories();
   void PopulatePlugins(const wxString &category);
   void UpdateCategoryCounts();

   // Event handlers
   void OnCategorySelected(wxCommandEvent &evt);
   void OnPluginActivated(wxListEvent &evt);
   void OnApplyEffect(wxCommandEvent &evt);
   void OnToggleFavorite(wxCommandEvent &evt);
   void OnSearch(wxCommandEvent &evt);
   void OnSearchTimer(wxTimerEvent &evt);
   void OnClose(wxCommandEvent &evt);
   void OnPluginSelected(wxListEvent &evt);

   // Effect application
   bool ApplyEffectById(const wxString &pluginId);
   wxString FindPluginCommandId(const wxString &internalName) const;

   // Helpers
   struct BrowserPluginEntry {
      wxString internalName;
      wxString category;
      wxString displayName;
      wxString description;
      int sortOrder{};
      bool hgeCertified{};
   };

   void ShowPluginDetails(const BrowserPluginEntry *entry);
   void ClearDetails();
   int  GetCategoryIndex(const wxString &cat) const;

   // UI Controls
   wxSearchCtrl    *mSearchCtrl;
   wxListBox       *mCategoryList;
   wxListCtrl      *mPluginList;
   wxStaticText    *mDetailTitle;
   wxStaticText    *mDetailCategory;
   wxStaticText    *mDetailDesc;
   wxButton        *mApplyBtn;
   wxButton        *mFavBtn;
   wxPanel         *mDetailPanel;
   wxTimer          mSearchTimer;

   // Data
   std::vector<BrowserPluginEntry> mAllPlugins;
   std::vector<BrowserPluginEntry> mFilteredPlugins;
   wxString                 mCurrentCategory;
   wxString                 mSelectedPlugin;
   bool                     mIsSearching;

   // Category display names and icons
   struct CategoryInfo {
      wxString id;
      wxString label;
      wxString icon;
   };
   std::vector<CategoryInfo> mCategories;

   // Color constants
   static const wxColour kBgColor;
   static const wxColour kCategoryBg;
   static const wxColour kSelectedCategory;
   static const wxColour kCertifiedBadge;
   static const wxColour kStarColor;

   DECLARE_EVENT_TABLE()
};

// -----------------------------------------------------------------------
// PluginListRenderer — Custom rendering for the plugin list
// -----------------------------------------------------------------------
class PluginListRenderer final : public wxListCtrl
{
public:
   PluginListRenderer(wxWindow *parent, const wxWindowID id,
                      const wxPoint &pos, const wxSize &size, long style);

   void SetHgeCertified(long item, bool certified);
   void SetStarred(long item, bool starred);
   bool IsHgeCertified(long item) const;
   bool IsStarred(long item) const;

private:
   std::map<long, bool> mCertifiedMap;
   std::map<long, bool> mStarredMap;
};

#endif // __HGE_EFFECT_BROWSER_H__
