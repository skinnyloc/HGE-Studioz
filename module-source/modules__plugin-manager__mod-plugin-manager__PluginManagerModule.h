/**********************************************************************

 HGE Music Studio — Plugin Manager Module

 Self-contained plugin discovery, caching, validation, and quarantine.
 Designed for production DMG distribution — no symlinks, no system
 dependencies, no user configuration required.

 All bundled plugins (VST2, VST3, LV2, AU) are auto-detected at
 startup from inside the app bundle. Plugin metadata is cached for
 fast subsequent launches. Problematic plugins are quarantined to
 prevent repeated scan failures.

 **********************************************************************/

#ifndef __HGE_PLUGINMANAGER_MODULE_H__
#define __HGE_PLUGINMANAGER_MODULE_H__

#include <memory>
#include <vector>
#include <map>
#include <functional>

#include <wx/string.h>
#include <wx/filename.h>

#include "ComponentInterface.h"
#include "PluginProvider.h"

// -----------------------------------------------------------------------
// PluginBundle — metadata for a single bundled plugin
// -----------------------------------------------------------------------
struct PluginBundle
{
   wxString name;             // display name from Info.plist
   wxString path;             // full path to plugin bundle
   wxString type;             // "VST2", "VST3", "LV2", "AU", "Nyquist"
   wxString arch;             // "x86_64", "arm64", "universal"
   wxString version;          // version string
   wxString vendor;           // manufacturer/vendor
   bool     isValid;          // passed validation
   bool     isQuarantined;    // previously failed to load
   wxString quarantineReason; // why it was quarantined
   int64_t  fileSize;         // size in bytes
   wxString fileHash;         // SHA-256 of binary for change detection
   time_t   lastModified;     // last modification time
};

// -----------------------------------------------------------------------
// PluginScanResult — result of a full scan pass
// -----------------------------------------------------------------------
struct PluginScanResult
{
   std::vector<PluginBundle> discovered;
   std::vector<PluginBundle> validated;
   std::vector<PluginBundle> quarantined;
   int                       totalTimeMs;
   wxString                  errorLog;
};

// -----------------------------------------------------------------------
// IPluginManagerUI — interface for the manager panel
// -----------------------------------------------------------------------
class IPluginManagerUI
{
public:
   virtual ~IPluginManagerUI() = default;
   virtual void UpdatePluginList() = 0;
   virtual void ShowStatus(const wxString &msg) = 0;
   virtual void ShowError(const wxString &msg) = 0;
};

// -----------------------------------------------------------------------
// PluginManagerModule — main plugin management service
// -----------------------------------------------------------------------
class PluginManagerModule final
{
public:
   static PluginManagerModule &Get();

   // --- Lifecycle ------------------------------------------------------
   void Initialize();   // called at app startup
   void Shutdown();     // called at app shutdown

   // --- Scanning -------------------------------------------------------
   PluginScanResult ScanBundledPlugins(bool forceRescan = false);
   void             RescanAll();            // forced full rescan
   void             RescanFormat(const wxString &format); // rescan one type

   // --- Access ---------------------------------------------------------
   const std::vector<PluginBundle> &GetAllPlugins() const;
   std::vector<PluginBundle>        GetPluginsByFormat(const wxString &fmt) const;
   PluginBundle                     GetPlugin(const wxString &path) const;
   bool                             HasPlugin(const wxString &path) const;

   // --- Quarantine -----------------------------------------------------
   void QuarantinePlugin(const wxString &path, const wxString &reason);
   void UnquarantinePlugin(const wxString &path);
   bool IsQuarantined(const wxString &path) const;
   void ClearQuarantine();

   // --- Cache ----------------------------------------------------------
   void SaveCache();
   bool LoadCache();
   void ClearCache();

   // --- Paths ----------------------------------------------------------
   wxString GetBundledPluginsPath() const;
   wxString GetBundledVST3Path() const;
   wxString GetCacheFilePath() const;
   wxString GetAppSupportDir() const;

   // --- Registration ---------------------------------------------------
   void RegisterUI(IPluginManagerUI *ui);
   void UnregisterUI();

private:
   PluginManagerModule() = default;
   ~PluginManagerModule() = default;
   PluginManagerModule(const PluginManagerModule &) = delete;
   PluginManagerModule &operator=(const PluginManagerModule &) = delete;

   // Internal helpers
   void            ScanDirectory(const wxString &dirPath, std::vector<PluginBundle> &results);
   PluginBundle    InspectVSTBundle(const wxString &path);
   PluginBundle    InspectVST3Bundle(const wxString &path);
   PluginBundle    InspectLV2Bundle(const wxString &path);
   PluginBundle    InspectAUBundle(const wxString &path);
   PluginBundle    InspectNyquistScript(const wxString &path);

   wxString        GetBinaryArch(const wxString &binaryPath);
   wxString        CalculateFileHash(const wxString &filePath);
   bool            ValidateBinary(const wxString &path, wxString &errorOut);
   wxString        ReadInfoPlistValue(const wxString &plistPath, const wxString &key);

   // Data
   std::vector<PluginBundle> mPlugins;
   std::vector<PluginBundle> mQuarantine;
   IPluginManagerUI         *mUI = nullptr;
   bool                      mInitialized = false;

   // Constants
   static constexpr int CACHE_VERSION = 1;
};

#endif // __HGE_PLUGINMANAGER_MODULE_H__
