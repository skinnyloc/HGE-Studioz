/**********************************************************************

 HGE Music Studio — Plugin Category Manager

 Categorizes plugins into curated groups for a modern DAW experience.

 Categories:
   EQ / Dynamics / Pitch Correction / Reverb & Delay
   Mastering / Utility / Legacy (hidden)

 Plugins can belong to multiple categories. Legacy/internal plugins
 are hidden from the main menu but remain loaded for compatibility.

 **********************************************************************/

#ifndef __HGE_PLUGIN_CATEGORY_MANAGER_H__
#define __HGE_PLUGIN_CATEGORY_MANAGER_H__

#include <wx/string.h>
#include <wx/filename.h>
#include <map>
#include <vector>
#include <set>

struct PluginCategoryInfo
{
   wxString category;       // "EQ", "Dynamics", etc.
   wxString displayName;    // Clean display name
   int      sortOrder;      // Position within category
   bool     isVisible;      // Shown in main menu?
   bool     isHgeCertified; // HGE Music Studio branded
   wxString iconName;       // Icon resource name (future)
};

class PluginCategoryManager
{
public:
   static PluginCategoryManager &Get();

   // ── Category Assignment ──────────────────────────────────────────

   // Get category for a plugin by its internal name or path
   PluginCategoryInfo GetCategory(const wxString &internalName,
                                  const wxString &path = wxEmptyString) const;

   // Get all plugins in a given category
   std::vector<wxString> GetPluginsInCategory(const wxString &category) const;

   // ── Visibility ───────────────────────────────────────────────────

   bool IsPluginVisible(const wxString &internalName) const;
   bool IsPluginHidden(const wxString &internalName) const;

   // Hide a plugin from menus (disables but keeps loaded)
   void HidePlugin(const wxString &internalName);

   // Unhide a previously hidden plugin
   void UnhidePlugin(const wxString &internalName);

   // ── Category Listing ─────────────────────────────────────────────

   // All category names in display order
   std::vector<wxString> GetCategoryNames() const;

   // All visible categories (excludes "Legacy" and "Hidden")
   std::vector<wxString> GetVisibleCategoryNames() const;

   // ── Starred / Favorites ──────────────────────────────────────────

   void ToggleStar(const wxString &internalName);
   bool IsStarred(const wxString &internalName) const;
   std::vector<wxString> GetStarredPlugins() const;

   // ── Recent ───────────────────────────────────────────────────────

   void RecordUse(const wxString &internalName);
   std::vector<wxString> GetRecentPlugins(int count = 10) const;

   // ── HGE Certified ────────────────────────────────────────────────

   bool IsHgeCertified(const wxString &internalName) const;
   std::vector<wxString> GetHgeCertifiedPlugins() const;

   // ── Search ───────────────────────────────────────────────────────

   // Search all plugins by name/category, returns matching names
   std::vector<wxString> Search(const wxString &query) const;

   // ── Persistence ──────────────────────────────────────────────────

   void SaveState();
   void LoadState();

   // ── Built-in Config ──────────────────────────────────────────────

   // Load the default category mappings
   void LoadBuiltinCategories();

private:
   PluginCategoryManager();
   ~PluginCategoryManager() = default;

   struct PluginEntry
   {
      wxString   internalName;
      wxString   category;
      wxString   displayName;
      int        sortOrder;
      bool       visible;
      bool       hgeCertified;
      bool       starred;
      wxString   icon;
   };

   std::map<wxString, PluginEntry> mPluginMap;
   std::vector<wxString> mRecent; // recent plugin names, newest first

   static constexpr int MAX_RECENT = 25;
};

#endif // __HGE_PLUGIN_CATEGORY_MANAGER_H__
