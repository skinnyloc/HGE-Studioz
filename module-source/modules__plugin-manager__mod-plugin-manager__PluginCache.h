/**********************************************************************

 HGE Music Studio — Plugin Cache

 Fast on-disk cache for plugin metadata. Avoids re-scanning the
 entire Plug-Ins directory on every launch. Uses SHA-256 hashes
 to detect plugin binary changes.

 Cache location:
   ~/Library/Application Support/HgeMusicStudio/plugin-cache.xml

 **********************************************************************/

#ifndef __HGE_PLUGIN_CACHE_H__
#define __HGE_PLUGIN_CACHE_H__

#include "PluginManagerModule.h"
#include <wx/string.h>
#include <wx/filename.h>

class PluginCache
{
public:
   static PluginCache &Get();

   bool Load(std::vector<PluginBundle> &plugins,
             std::vector<PluginBundle> &quarantine);

   bool Save(const std::vector<PluginBundle> &plugins,
             const std::vector<PluginBundle> &quarantine);

   void Clear();
   bool IsValid() const;
   bool NeedsRescan(const std::vector<PluginBundle> &plugins) const;

   wxString GetCachePath() const;

private:
   PluginCache() = default;
   bool CheckPluginStillValid(const PluginBundle &plugin) const;

   static constexpr int CURRENT_VERSION = 1;
};

#endif // __HGE_PLUGIN_CACHE_H__
