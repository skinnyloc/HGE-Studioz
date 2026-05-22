/**********************************************************************

 HGE Music Studio — Plugin Cache Implementation

 XML-based cache for plugin metadata. Delegates to the main
 PluginManagerModule for actual save/load operations.

 **********************************************************************/

#include "PluginCache.h"
#include "PluginManagerModule.h"

PluginCache &PluginCache::Get()
{
   static PluginCache instance;
   return instance;
}

bool PluginCache::Load(std::vector<PluginBundle> &plugins,
                       std::vector<PluginBundle> &quarantine)
{
   return PluginManagerModule::Get().LoadCache();
}

bool PluginCache::Save(const std::vector<PluginBundle> &plugins,
                       const std::vector<PluginBundle> &quarantine)
{
   PluginManagerModule::Get().SaveCache();
   return true;
}

void PluginCache::Clear()
{
   PluginManagerModule::Get().ClearCache();
}

bool PluginCache::IsValid() const
{
   wxString cachePath = GetCachePath();
   return wxFile::Exists(cachePath);
}

bool PluginCache::NeedsRescan(const std::vector<PluginBundle> &plugins) const
{
   // Check if any cached plugins have changed on disk
   for (const auto &p : plugins)
   {
      if (!CheckPluginStillValid(p))
         return true;
   }
   return false;
}

wxString PluginCache::GetCachePath() const
{
   return PluginManagerModule::Get().GetCacheFilePath();
}

bool PluginCache::CheckPluginStillValid(const PluginBundle &plugin) const
{
   if (!wxFile::Exists(plugin.path))
      return false;

   struct stat st;
   if (stat(plugin.path.utf8_str(), &st) != 0)
      return false;

   // Check if file was modified
   if (st.st_mtime != plugin.lastModified)
      return false;

   return true;
}
