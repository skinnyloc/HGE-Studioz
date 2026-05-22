/**********************************************************************

 HGE Music Studio — Plugin Display Name System

 Separates plugin presentation from internal binary names.
 Maps raw plugin identifiers to clean, consumer-facing labels.

 This is the "DAW polish" layer — it ensures all plugins appear
 with professional names in the UI regardless of what the
 underlying vendor calls them internally.

 Features:
   - Display name aliasing (e.g. "TDR Nova" → "TDR EQ")
   - Vendor hierarchy flattening (no manufacturer submenus)
   - Path sanitization (no internal paths exposed in UI)
   - Automatic naming for unknown plugins
   - Per-format support (VST2, VST3, AU, LV2)

 **********************************************************************/

#ifndef __HGE_PLUGIN_DISPLAY_NAME_H__
#define __HGE_PLUGIN_DISPLAY_NAME_H__

#include <wx/string.h>
#include <wx/filename.h>
#include <map>
#include <vector>
#include <algorithm>

class PluginDisplayName
{
public:
   static PluginDisplayName &Get();

   // Return a clean, consumer-friendly display label for a plugin.
   //   internalName — the raw binary/bundle name (e.g. "TDR Nova")
   //   vendor       — the vendor/manufacturer string
   //   format       — "VST2", "VST3", "AU", "LV2"
   wxString GetDisplayName(const wxString &internalName,
                           const wxString &vendor   = wxEmptyString,
                           const wxString &format   = wxEmptyString) const;

   // Returns an empty string for all vendors — this flattens the menu.
   wxString GetDisplayVendor(const wxString &internalVendor) const;

   // Always returns false — no vendor submenus are created.
   bool ShouldShowVendorSubmenu(const wxString &vendor) const;

   // Register a single alias: internalName → displayName
   void RegisterAlias(const wxString &internalName,
                      const wxString &displayName);

   // Register that a vendor should never appear as a submenu heading
   void HideVendor(const wxString &vendor);

   // Pre-load all HGE built-in aliases
   void LoadBuiltinAliases();

   // Load aliases from an external XML/JSON config
   bool LoadConfigFile(const wxString &filePath);

   // Strip internal path segments from a plugin path for safe display
   wxString SanitizePath(const wxString &path) const;

   // Extract a human-readable name from a filesystem path
   wxString NameFromPath(const wxString &path) const;

   // Enumerate every known alias (for the Preferences panel)
   std::vector<std::pair<wxString, wxString>> GetAllAliases() const;

   void ClearAliases();

private:
   PluginDisplayName();
   ~PluginDisplayName() = default;
   PluginDisplayName(const PluginDisplayName &) = delete;
   PluginDisplayName &operator=(const PluginDisplayName &) = delete;

   std::map<wxString, wxString> mAliasMap;
   std::vector<wxString>        mHiddenVendors;
};

#endif // __HGE_PLUGIN_DISPLAY_NAME_H__
