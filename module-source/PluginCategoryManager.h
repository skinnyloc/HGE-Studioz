/**********************************************************************

 HGE Music Studio — Plugin Category Manager

 Curates all bundled plugins into 6 professional categories:
   EQ, Pitch Correction, Dynamics, Reverb & Delay, Mastering, Utility

 Supports favorites, recent plugins, search, and HGE Certified badges.
 Designed for the modern effect browser UI.

 **********************************************************************/

#ifndef __HGE_PLUGIN_CATEGORY_MANAGER_H__
#define __HGE_PLUGIN_CATEGORY_MANAGER_H__

#include <string>
#include <vector>
#include <map>
#include <set>
#include <functional>

// -----------------------------------------------------------------------
// PluginEntry — metadata for a categorized plugin
// -----------------------------------------------------------------------
struct PluginEntry
{
   std::string internalName;   // raw plugin ID (e.g., "TDR Nova")
   std::string category;       // "EQ", "Dynamics", "Pitch Correction", etc.
   std::string displayName;    // cleaned name (e.g., "TDR EQ")
   int         sortOrder;      // position within category
   bool        visible;        // show in menu (false = legacy/hidden)
   bool        hgeCertified;   // shows HGE badge in UI
   bool        starred;        // user favorite
   std::string icon;           // icon resource path (future)
};

// -----------------------------------------------------------------------
// PluginCategoryManager — singleton for plugin curation
// -----------------------------------------------------------------------
class PluginCategoryManager
{
public:
   static PluginCategoryManager &Get();

   // --- Built-in categories -------------------------------------------
   void LoadBuiltinCategories();

   // --- Access --------------------------------------------------------
   std::vector<PluginEntry> GetAllPlugins() const;
   std::vector<PluginEntry> GetPluginsByCategory(const std::string &cat) const;
   std::vector<std::string> GetCategories() const;
   PluginEntry              GetPlugin(const std::string &internalName) const;
   bool                     HasPlugin(const std::string &internalName) const;

   // --- Favorites -----------------------------------------------------
   void ToggleStar(const std::string &internalName);
   bool IsStarred(const std::string &internalName) const;
   std::vector<PluginEntry> GetStarredPlugins() const;

   // --- Recent --------------------------------------------------------
   void RecordUse(const std::string &internalName);
   std::vector<PluginEntry> GetRecentPlugins(int maxCount = 10) const;

   // --- Search --------------------------------------------------------
   std::vector<PluginEntry> Search(const std::string &query) const;

   // --- HGE Certified -------------------------------------------------
   bool IsHgeCertified(const std::string &internalName) const;
   std::vector<PluginEntry> GetHgeCertifiedPlugins() const;

   // --- Persistence ---------------------------------------------------
   void SaveState(const std::string &path);
   void LoadState(const std::string &path);

private:
   PluginCategoryManager() = default;
   ~PluginCategoryManager() = default;
   PluginCategoryManager(const PluginCategoryManager &) = delete;
   PluginCategoryManager &operator=(const PluginCategoryManager &) = delete;

   std::vector<PluginEntry>    mPlugins;
   std::set<std::string>       mStarred;
   std::vector<std::string>    mRecent;  // ordered list of internal names
   std::map<std::string, int>  mCategoryOrder;
};

#endif // __HGE_PLUGIN_CATEGORY_MANAGER_H__
