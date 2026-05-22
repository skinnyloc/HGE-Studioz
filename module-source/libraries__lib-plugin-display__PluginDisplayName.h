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

// -----------------------------------------------------------------------
// PluginDisplayName — static mapping table + lookup methods
// -----------------------------------------------------------------------
class PluginDisplayName
{
public:
   // Get the singleton instance (lazy init)
   static PluginDisplayName &Get();

   // --- Core Lookup ----------------------------------------------------

   // Get the display name for a plugin given its internal identifier
   // Returns a clean, consumer-friendly name
   wxString GetDisplayName(const wxString &internalName,
                           const wxString &vendor   = wxEmptyString,
                           const wxString &format   = wxEmptyString) const;

   // Get the vendor name to display (returns empty string to flatten)
   wxString GetDisplayVendor(const wxString &internalVendor) const;

   // Check if a plugin should be grouped under a vendor submenu
   bool ShouldShowVendorSubmenu(const wxString &vendor) const;

   // --- Registration ---------------------------------------------------

   // Register a display name alias
   void RegisterAlias(const wxString &internalName,
                      const wxString &displayName);

   // Register a vendor to hide (flatten)
   void HideVendor(const wxString &vendor);

   // Register a custom display name for a specific path pattern
   void RegisterPathAlias(const wxString &pathPattern,
                          const wxString &displayName);

   // --- Bulk Registration ----------------------------------------------

   // Load all built-in HGE aliases
   void LoadBuiltinAliases();

   // Load aliases from an external config file
   bool LoadConfigFile(const wxString &filePath);

   // --- Utilities ------------------------------------------------------

   // Sanitize a plugin path for display (remove internal paths)
   wxString SanitizePath(const wxString &path) const;

   // Extract a clean plugin name from a filesystem path
   wxString NameFromPath(const wxString &path) const;

   // Get all registered display names (for UI listing)
   std::vector<std::pair<wxString, wxString>> GetAllAliases() const;

   // Clear all aliases
   void ClearAliases();

private:
   PluginDisplayName();
   ~PluginDisplayName() = default;

   // No copy
   PluginDisplayName(const PluginDisplayName &) = delete;
   PluginDisplayName &operator=(const PluginDisplayName &) = delete;

   std::map<wxString, wxString> mAliasMap;       // internal → display name
   std::map<wxString, wxString> mPathPatternMap; // path pattern → display name
   std::vector<wxString>        mHiddenVendors;  // vendors to flatten
};

#endif // __HGE_PLUGIN_DISPLAY_NAME_H__
