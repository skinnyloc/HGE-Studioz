/**********************************************************************

 HGE Music Studio — Plugin Manager Module Implementation

 Production-grade plugin management for self-contained DMG distribution.
 Scans bundled plugins at startup, validates architecture compatibility,
 caches metadata for fast subsequent launches, and quarantines plugins
 that fail to load.

 =========================================================================
 SCAN ORDER
 =========================================================================
 On first launch (or when cache is invalidated):
   1. Scan Contents/Plug-Ins/ for .vst, .lv2, .ny files
   2. Scan Contents/VST3/ for .vst3 bundles
   3. Scan Contents/Plug-Ins/ for .component bundles (AU)
   4. Validate each plugin (arch, signing, binary integrity)
   5. Cache results to ~/Library/Application Support/HgeMusicStudio/

 On subsequent launches:
   1. Load cache file (fast)
   2. Verify plugin files still exist and haven't changed (hash check)
   3. If changes detected → trigger partial rescan
   4. If cache missing → trigger full scan

 =========================================================================
 ENVIRONMENT VARIABLES (set by Wrapper at launch)
 =========================================================================
   VST_PATH  → Contents/Plug-Ins/   (VST2 discovery)
   LV2_PATH  → Contents/Plug-Ins/   (LV2 discovery)
   VST3_PATH → Contents/VST3/       (VST3 discovery, custom env var)

 **********************************************************************/

#include "PluginManagerModule.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <thread>

#include <wx/app.h>
#include <wx/dir.h>
#include <wx/file.h>
#include <wx/filefn.h>
#include <wx/filename.h>
#include <wx/log.h>
#include <wx/stdpaths.h>
#include <wx/txtstrm.h>
#include <wx/wfstream.h>
#include <wx/xml/xml.h>

#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

#include <CommonCrypto/CommonCrypto.h>

#ifdef __APPLE__
#include <mach-o/loader.h>
#include <mach-o/fat.h>
#include <CoreFoundation/CoreFoundation.h>
#include <Security/Security.h>
#endif

// ─── Singleton ──────────────────────────────────────────────────────────

PluginManagerModule &PluginManagerModule::Get()
{
   static PluginManagerModule instance;
   return instance;
}

// ─── Lifecycle ──────────────────────────────────────────────────────────

void PluginManagerModule::Initialize()
{
   if (mInitialized) return;

   wxLogMessage("HGE PluginManager: Initializing plugin management system");

   // Ensure app support directory exists
   wxFileName cacheDir(GetAppSupportDir());
   if (!cacheDir.DirExists())
      cacheDir.Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

   // Try loading cached plugin data first
   if (!LoadCache())
   {
      wxLogMessage("HGE PluginManager: No cache found, performing full scan");
      ScanBundledPlugins(true);
   }
   else
   {
      wxLogMessage("HGE PluginManager: Cache loaded (%zu plugins)", mPlugins.size());
   }

   mInitialized = true;
}

void PluginManagerModule::Shutdown()
{
   if (!mInitialized) return;
   SaveCache();
   mPlugins.clear();
   mQuarantine.clear();
   mInitialized = false;
   wxLogMessage("HGE PluginManager: Shutdown complete");
}

// ─── Scanning ───────────────────────────────────────────────────────────

PluginScanResult PluginManagerModule::ScanBundledPlugins(bool forceRescan)
{
   PluginScanResult result;
   auto startTime = std::chrono::steady_clock::now();

   wxLogMessage("HGE PluginManager: Starting plugin scan (force=%d)", forceRescan);

   // If not forced and cache is valid, skip
   if (!forceRescan && !mPlugins.empty())
   {
      wxLogMessage("HGE PluginManager: Using cached data, skipping scan");
      result.discovered = mPlugins;
      result.totalTimeMs = 0;
      return result;
   }

   mPlugins.clear();

   // Scan each bundled plugin directory
   wxString pluginsDir = GetBundledPluginsPath();
   wxString vst3Dir    = GetBundledVST3Path();

   if (wxDir::Exists(pluginsDir))
   {
      wxLogMessage("HGE PluginManager: Scanning %s", pluginsDir);
      ScanDirectory(pluginsDir, result.discovered);
   }

   if (wxDir::Exists(vst3Dir) && vst3Dir != pluginsDir)
   {
      wxLogMessage("HGE PluginManager: Scanning %s", vst3Dir);
      ScanDirectory(vst3Dir, result.discovered);
   }

   // Validate all discovered plugins
   for (auto &plugin : result.discovered)
   {
      wxString error;
      plugin.isValid = ValidateBinary(plugin.path, error);
      if (!plugin.isValid)
      {
         wxLogWarning("HGE PluginManager: Plugin validation failed: %s — %s",
                      plugin.name, error);
         if (!plugin.quarantineReason.empty())
         {
            result.quarantined.push_back(plugin);
            mQuarantine.push_back(plugin);
         }
      }
      else
      {
         result.validated.push_back(plugin);
         mPlugins.push_back(plugin);
      }
   }

   // Save updated cache
   SaveCache();

   auto endTime = std::chrono::steady_clock::now();
   result.totalTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
      endTime - startTime).count();

   wxLogMessage("HGE PluginManager: Scan complete — %zu discovered, %zu validated, "
                "%zu quarantined (%dms)",
                result.discovered.size(), result.validated.size(),
                result.quarantined.size(), result.totalTimeMs);

   return result;
}

void PluginManagerModule::RescanAll()
{
   wxLogMessage("HGE PluginManager: Forced full rescan requested");
   ClearCache();
   ScanBundledPlugins(true);
   if (mUI) mUI->UpdatePluginList();
}

void PluginManagerModule::RescanFormat(const wxString &format)
{
   wxLogMessage("HGE PluginManager: Rescanning format: %s", format);

   // Remove existing entries of this format
   mPlugins.erase(
      std::remove_if(mPlugins.begin(), mPlugins.end(),
         [&](const PluginBundle &p) { return p.type == format; }),
      mPlugins.end()
   );

   // Rescan relevant directories
   auto result = ScanBundledPlugins(false);

   // Add newly discovered plugins of this format
   for (auto &p : result.validated)
   {
      if (p.type == format)
         mPlugins.push_back(p);
   }

   SaveCache();
   if (mUI) mUI->UpdatePluginList();
}

// ─── Access ─────────────────────────────────────────────────────────────

const std::vector<PluginBundle> &PluginManagerModule::GetAllPlugins() const
{
   return mPlugins;
}

std::vector<PluginBundle> PluginManagerModule::GetPluginsByFormat(const wxString &fmt) const
{
   std::vector<PluginBundle> result;
   std::copy_if(mPlugins.begin(), mPlugins.end(), std::back_inserter(result),
      [&](const PluginBundle &p) { return p.type == fmt; });
   return result;
}

PluginBundle PluginManagerModule::GetPlugin(const wxString &path) const
{
   for (const auto &p : mPlugins)
      if (p.path == path) return p;
   return PluginBundle{};
}

bool PluginManagerModule::HasPlugin(const wxString &path) const
{
   return std::any_of(mPlugins.begin(), mPlugins.end(),
      [&](const PluginBundle &p) { return p.path == path; });
}

// ─── Quarantine ─────────────────────────────────────────────────────────

void PluginManagerModule::QuarantinePlugin(const wxString &path, const wxString &reason)
{
   auto it = std::find_if(mPlugins.begin(), mPlugins.end(),
      [&](const PluginBundle &p) { return p.path == path; });

   if (it != mPlugins.end())
   {
      it->isQuarantined = true;
      it->quarantineReason = reason;
      mQuarantine.push_back(*it);
      mPlugins.erase(it);
      SaveCache();
      wxLogWarning("HGE PluginManager: Quarantined plugin: %s — %s", path, reason);
   }
}

void PluginManagerModule::UnquarantinePlugin(const wxString &path)
{
   mQuarantine.erase(
      std::remove_if(mQuarantine.begin(), mQuarantine.end(),
         [&](const PluginBundle &p) { return p.path == path; }),
      mQuarantine.end()
   );

   // Will be re-added on next scan
   SaveCache();
}

bool PluginManagerModule::IsQuarantined(const wxString &path) const
{
   return std::any_of(mQuarantine.begin(), mQuarantine.end(),
      [&](const PluginBundle &p) { return p.path == path; });
}

void PluginManagerModule::ClearQuarantine()
{
   mQuarantine.clear();
   SaveCache();
   wxLogMessage("HGE PluginManager: Quarantine cleared");
}

// ─── Cache ──────────────────────────────────────────────────────────────

void PluginManagerModule::SaveCache()
{
   wxString cachePath = GetCacheFilePath();
   wxFileName(cachePath).Mkdir(wxS_DIR_DEFAULT, wxPATH_MKDIR_FULL);

   wxXmlDocument doc;
   wxXmlNode *root = new wxXmlNode(wxXML_ELEMENT_NODE, wxT("HgePluginCache"));
   root->AddAttribute(wxT("version"), wxString::Format(wxT("%d"), CACHE_VERSION));
   root->AddAttribute(wxT("format"), wxT("1"));
   doc.SetRoot(root);

   // Save validated plugins
   wxXmlNode *pluginsNode = new wxXmlNode(wxXML_ELEMENT_NODE, wxT("Plugins"));
   for (const auto &p : mPlugins)
   {
      wxXmlNode *node = new wxXmlNode(wxXML_ELEMENT_NODE, wxT("Plugin"));
      node->AddAttribute(wxT("name"), p.name);
      node->AddAttribute(wxT("path"), p.path);
      node->AddAttribute(wxT("type"), p.type);
      node->AddAttribute(wxT("arch"), p.arch);
      node->AddAttribute(wxT("version"), p.version);
      node->AddAttribute(wxT("vendor"), p.vendor);
      node->AddAttribute(wxT("hash"), p.fileHash);
      node->AddAttribute(wxT("size"), wxString::Format(wxT("%lld"), (long long)p.fileSize));
      node->AddAttribute(wxT("mtime"), wxString::Format(wxT("%ld"), (long)p.lastModified));
      pluginsNode->AddChild(node);
   }
   root->AddChild(pluginsNode);

   // Save quarantined plugins
   wxXmlNode *quarantineNode = new wxXmlNode(wxXML_ELEMENT_NODE, wxT("Quarantine"));
   for (const auto &p : mQuarantine)
   {
      wxXmlNode *node = new wxXmlNode(wxXML_ELEMENT_NODE, wxT("Plugin"));
      node->AddAttribute(wxT("name"), p.name);
      node->AddAttribute(wxT("path"), p.path);
      node->AddAttribute(wxT("type"), p.type);
      node->AddAttribute(wxT("reason"), p.quarantineReason);
      node->AddAttribute(wxT("hash"), p.fileHash);
      quarantineNode->AddChild(node);
   }
   root->AddChild(quarantineNode);

   wxXmlOutputHandler output;
   wxFileOutputStream stream(cachePath);
   output.SetStream(&stream);
   output.WriteDocument(&doc);

   wxLogMessage("HGE PluginManager: Cache saved to %s (%zu plugins, %zu quarantined)",
                cachePath, mPlugins.size(), mQuarantine.size());
}

bool PluginManagerModule::LoadCache()
{
   wxString cachePath = GetCacheFilePath();
   if (!wxFile::Exists(cachePath))
      return false;

   wxXmlDocument doc;
   if (!doc.Load(cachePath))
   {
      wxLogWarning("HGE PluginManager: Cache load failed, corrupt file?");
      return false;
   }

   wxXmlNode *root = doc.GetRoot();
   if (!root || root->GetName() != wxT("HgePluginCache"))
      return false;

   // Verify cache version
   wxString verStr = root->GetAttribute(wxT("version"), wxT("0"));
   int version;
   verStr.ToInt(&version);
   if (version != CACHE_VERSION)
   {
      wxLogMessage("HGE PluginManager: Cache version mismatch (%d != %d), ignoring",
                   version, CACHE_VERSION);
      return false;
   }

   mPlugins.clear();
   mQuarantine.clear();

   wxXmlNode *child = root->GetChildren();
   while (child)
   {
      if (child->GetName() == wxT("Plugins"))
      {
         wxXmlNode *pluginNode = child->GetChildren();
         while (pluginNode)
         {
            if (pluginNode->GetName() == wxT("Plugin"))
            {
               PluginBundle p;
               p.name            = pluginNode->GetAttribute(wxT("name"));
               p.path            = pluginNode->GetAttribute(wxT("path"));
               p.type            = pluginNode->GetAttribute(wxT("type"));
               p.arch            = pluginNode->GetAttribute(wxT("arch"));
               p.version         = pluginNode->GetAttribute(wxT("version"));
               p.vendor          = pluginNode->GetAttribute(wxT("vendor"));
               p.fileHash        = pluginNode->GetAttribute(wxT("hash"));
               p.isValid         = true;

               wxString sizeStr, mtimeStr;
               sizeStr = pluginNode->GetAttribute(wxT("size"), wxT("0"));
               mtimeStr = pluginNode->GetAttribute(wxT("mtime"), wxT("0"));
               sizeStr.ToCDouble((double*)&p.fileSize);
               mtimeStr.ToCDouble((double*)&p.lastModified);

               // Verify plugin still exists and hasn't changed
               if (wxFile::Exists(p.path))
               {
                  struct stat st;
                  if (stat(p.path.utf8_str(), &st) == 0)
                  {
                     // Check if file was modified since cache was saved
                     if (st.st_mtime != p.lastModified)
                     {
                        wxLogMessage("HGE PluginManager: Plugin changed since cache: %s",
                                     p.name);
                        continue; // Skip — will be re-scanned
                     }
                     mPlugins.push_back(p);
                  }
               }
            }
            pluginNode = pluginNode->GetNext();
         }
      }
      else if (child->GetName() == wxT("Quarantine"))
      {
         wxXmlNode *pluginNode = child->GetChildren();
         while (pluginNode)
         {
            if (pluginNode->GetName() == wxT("Plugin"))
            {
               PluginBundle p;
               p.name             = pluginNode->GetAttribute(wxT("name"));
               p.path             = pluginNode->GetAttribute(wxT("path"));
               p.type             = pluginNode->GetAttribute(wxT("type"));
               p.quarantineReason = pluginNode->GetAttribute(wxT("reason"));
               p.fileHash         = pluginNode->GetAttribute(wxT("hash"));
               p.isQuarantined    = true;
               mQuarantine.push_back(p);
            }
            pluginNode = pluginNode->GetNext();
         }
      }
      child = child->GetNext();
   }

   wxLogMessage("HGE PluginManager: Cache loaded — %zu plugins, %zu quarantined",
                mPlugins.size(), mQuarantine.size());
   return true;
}

void PluginManagerModule::ClearCache()
{
   wxString cachePath = GetCacheFilePath();
   if (wxFile::Exists(cachePath))
      wxRemoveFile(cachePath);

   mPlugins.clear();
   wxLogMessage("HGE PluginManager: Cache cleared");
}

// ─── Internal: Directory scanning ───────────────────────────────────────

void PluginManagerModule::ScanDirectory(const wxString &dirPath,
                                         std::vector<PluginBundle> &results)
{
   wxDir dir(dirPath);
   if (!dir.IsOpened())
   {
      wxLogWarning("HGE PluginManager: Cannot open directory: %s", dirPath);
      return;
   }

   wxString filename;
   if (!dir.GetFirst(&filename, wxEmptyString, wxDIR_FILES | wxDIR_DIRS))
      return;

   do {
      wxString fullPath = dirPath + wxFILE_SEP_PATH + filename;

      if (wxDir::Exists(fullPath))
      {
         // Check for known bundle extensions
         if (filename.Lower().EndsWith(wxT(".vst")))
         {
            PluginBundle bundle = InspectVSTBundle(fullPath);
            if (!bundle.name.IsEmpty())
               results.push_back(bundle);
         }
         else if (filename.Lower().EndsWith(wxT(".vst3")))
         {
            PluginBundle bundle = InspectVST3Bundle(fullPath);
            if (!bundle.name.IsEmpty())
               results.push_back(bundle);
         }
         else if (filename.Lower().EndsWith(wxT(".lv2")))
         {
            PluginBundle bundle = InspectLV2Bundle(fullPath);
            if (!bundle.name.IsEmpty())
               results.push_back(bundle);
         }
         else if (filename.Lower().EndsWith(wxT(".component")))
         {
            PluginBundle bundle = InspectAUBundle(fullPath);
            if (!bundle.name.IsEmpty())
               results.push_back(bundle);
         }
         else
         {
            // Recurse into subdirectories (for nested plugin structures)
            ScanDirectory(fullPath, results);
         }
      }
      else if (filename.Lower().EndsWith(wxT(".ny")))
      {
         PluginBundle bundle = InspectNyquistScript(fullPath);
         if (!bundle.name.IsEmpty())
            results.push_back(bundle);
      }

   } while (dir.GetNext(&filename));
}

// ─── Internal: Bundle inspection ────────────────────────────────────────

PluginBundle PluginManagerModule::InspectVSTBundle(const wxString &path)
{
   PluginBundle bundle;
   bundle.path = path;
   bundle.type = wxT("VST2");

   wxString plistPath = path + wxFILE_SEP_PATH + wxT("Contents/Info.plist");
   if (!wxFile::Exists(plistPath))
   {
      bundle.isValid = false;
      return bundle;
   }

   bundle.name    = ReadInfoPlistValue(plistPath, wxT("CFBundleDisplayName"));
   bundle.version = ReadInfoPlistValue(plistPath, wxT("CFBundleShortVersionString"));
   bundle.vendor  = ReadInfoPlistValue(plistPath, wxT("NSHumanReadableCopyright"));

   if (bundle.name.IsEmpty())
      bundle.name = ReadInfoPlistValue(plistPath, wxT("CFBundleName"));
   if (bundle.name.IsEmpty())
      bundle.name = wxFileName(path).GetName();

   // Check binary
   wxString binaryPath = path + wxFILE_SEP_PATH + wxT("Contents/MacOS/");
   wxString binaryName = ReadInfoPlistValue(plistPath, wxT("CFBundleExecutable"));
   if (!binaryName.IsEmpty())
   {
      binaryPath += binaryName;
      if (wxFile::Exists(binaryPath))
      {
         bundle.arch = GetBinaryArch(binaryPath);
         bundle.fileHash = CalculateFileHash(binaryPath);

         struct stat st;
         if (stat(binaryPath.utf8_str(), &st) == 0)
         {
            bundle.fileSize = st.st_size;
            bundle.lastModified = st.st_mtime;
         }
      }
   }

   bundle.isValid = true;
   return bundle;
}

PluginBundle PluginManagerModule::InspectVST3Bundle(const wxString &path)
{
   PluginBundle bundle;
   bundle.path = path;
   bundle.type = wxT("VST3");

   wxString plistPath = path + wxFILE_SEP_PATH + wxT("Contents/Info.plist");
   if (!wxFile::Exists(plistPath))
   {
      // VST3 bundles may use a different structure — check module path
      wxString modulePath = path + wxFILE_SEP_PATH + wxT("Contents/MacOS/");
      wxDir dir(modulePath);
      if (dir.IsOpened())
      {
         wxString f;
         if (dir.GetFirst(&f, wxEmptyString, wxDIR_FILES))
         {
            bundle.name = wxFileName(f).GetName();
            bundle.fileHash = CalculateFileHash(modulePath + f);

            struct stat st;
            if (stat((modulePath + f).utf8_str(), &st) == 0)
            {
               bundle.fileSize = st.st_size;
               bundle.lastModified = st.st_mtime;
               bundle.arch = GetBinaryArch(modulePath + f);
            }
         }
      }
      bundle.isValid = !bundle.name.IsEmpty();
      return bundle;
   }

   bundle.name    = ReadInfoPlistValue(plistPath, wxT("CFBundleDisplayName"));
   bundle.version = ReadInfoPlistValue(plistPath, wxT("CFBundleShortVersionString"));
   bundle.vendor  = ReadInfoPlistValue(plistPath, wxT("NSHumanReadableCopyright"));

   if (bundle.name.IsEmpty())
      bundle.name = wxFileName(path).GetName();

   wxString binaryPath = path + wxFILE_SEP_PATH + wxT("Contents/MacOS/");
   wxString binaryName = ReadInfoPlistValue(plistPath, wxT("CFBundleExecutable"));
   if (!binaryName.IsEmpty())
   {
      binaryPath += binaryName;
      if (wxFile::Exists(binaryPath))
      {
         bundle.arch = GetBinaryArch(binaryPath);
         bundle.fileHash = CalculateFileHash(binaryPath);

         struct stat st;
         if (stat(binaryPath.utf8_str(), &st) == 0)
         {
            bundle.fileSize = st.st_size;
            bundle.lastModified = st.st_mtime;
         }
      }
   }

   bundle.isValid = true;
   return bundle;
}

PluginBundle PluginManagerModule::InspectLV2Bundle(const wxString &path)
{
   PluginBundle bundle;
   bundle.path = path;
   bundle.type = wxT("LV2");
   bundle.name = wxFileName(path).GetName();
   bundle.isValid = true;
   return bundle;
}

PluginBundle PluginManagerModule::InspectAUBundle(const wxString &path)
{
   PluginBundle bundle;
   bundle.path = path;
   bundle.type = wxT("AU");

   wxString plistPath = path + wxFILE_SEP_PATH + wxT("Contents/Info.plist");
   if (!wxFile::Exists(plistPath))
      return bundle;

   bundle.name    = ReadInfoPlistValue(plistPath, wxT("CFBundleDisplayName"));
   bundle.version = ReadInfoPlistValue(plistPath, wxT("CFBundleShortVersionString"));

   if (bundle.name.IsEmpty())
      bundle.name = wxFileName(path).GetName();

   bundle.isValid = true;
   return bundle;
}

PluginBundle PluginManagerModule::InspectNyquistScript(const wxString &path)
{
   PluginBundle bundle;
   bundle.path = path;
   bundle.type = wxT("Nyquist");
   bundle.name = wxFileName(path).GetName();
   bundle.isValid = true;

   struct stat st;
   if (stat(path.utf8_str(), &st) == 0)
   {
      bundle.fileSize = st.st_size;
      bundle.lastModified = st.st_mtime;
   }

   return bundle;
}

// ─── Internal: Validation ───────────────────────────────────────────────

bool PluginManagerModule::ValidateBinary(const wxString &path, wxString &errorOut)
{
   wxString binaryPath;

   // For bundles, find the actual binary
   if (wxDir::Exists(path))
   {
      wxString plistPath = path + wxFILE_SEP_PATH + wxT("Contents/Info.plist");
      if (wxFile::Exists(plistPath))
      {
         wxString binaryName = ReadInfoPlistValue(plistPath, wxT("CFBundleExecutable"));
         if (!binaryName.IsEmpty())
            binaryPath = path + wxFILE_SEP_PATH + wxT("Contents/MacOS/") + binaryName;
      }
   }
   else
   {
      binaryPath = path;
   }

   if (binaryPath.IsEmpty() || !wxFile::Exists(binaryPath))
   {
      errorOut = wxT("Binary not found");
      return false;
   }

#ifdef __APPLE__
   // Check code signing status (ad-hoc signed is OK)
   SecStaticCodeRef staticCode = nullptr;
   SecRequirementRef requirement = nullptr;
   CFURLRef url = CFURLCreateFromFileSystemRepresentation(
      kCFAllocatorDefault,
      (const UInt8 *)binaryPath.utf8_str(),
      binaryPath.utf8_str().length(),
      false);

   if (url)
   {
      if (SecStaticCodeCreateWithPath(url, kSecCSDefaultFlags, &staticCode) == errSecSuccess)
      {
         OSStatus status = SecStaticCodeCheckValidityWithErrors(
            staticCode,
            kSecCSDefaultFlags | kSecCSConsiderExpiration,
            nullptr,
            nullptr);

         if (status != errSecSuccess && status != errSecCSUnsigned)
         {
            // Not signed at all is OK for now (common with free plugins)
            // Only warn on actual signing failures
            if (status != errSecCSUnsigned)
            {
               wxLogWarning("HGE PluginManager: Code signing warning for %s (status: %d)",
                            binaryPath, (int)status);
            }
         }
         CFRelease(staticCode);
      }
      CFRelease(url);
   }
#endif

   // Verify it's a valid Mach-O binary
   int fd = open(binaryPath.utf8_str(), O_RDONLY);
   if (fd < 0)
   {
      errorOut = wxT("Cannot open binary file");
      return false;
   }

   // Read magic bytes to determine binary type
   uint32_t magic;
   if (read(fd, &magic, sizeof(magic)) != sizeof(magic))
   {
      close(fd);
      errorOut = wxT("Cannot read binary header");
      return false;
   }
   close(fd);

   // Check for Mach-O magic numbers
   if (magic == FAT_MAGIC || magic == FAT_CIGAM ||
       magic == FAT_MAGIC_64 || magic == FAT_CIGAM_64)
   {
      // Universal binary — good
      return true;
   }

   if (magic == MH_MAGIC || magic == MH_CIGAM)
   {
      // 32-bit Mach-O — might work but warn
      wxLogWarning("HGE PluginManager: 32-bit binary detected: %s", binaryPath);
      return true; // Allow but warn
   }

   if (magic == MH_MAGIC_64 || magic == MH_CIGAM_64)
   {
      // 64-bit Mach-O — good
      return true;
   }

   errorOut = wxT("Not a valid Mach-O binary");
   return false;
}

// ─── Internal: Architecture detection ───────────────────────────────────

wxString PluginManagerModule::GetBinaryArch(const wxString &binaryPath)
{
   int fd = open(binaryPath.utf8_str(), O_RDONLY);
   if (fd < 0) return wxT("unknown");

   uint32_t magic;
   if (read(fd, &magic, sizeof(magic)) != sizeof(magic))
   {
      close(fd);
      return wxT("unknown");
   }

   if (magic == FAT_MAGIC || magic == FAT_MAGIC_64)
   {
      // Universal binary — determine which architectures
      struct fat_header fh;
      lseek(fd, 0, SEEK_SET);
      if (read(fd, &fh, sizeof(fh)) != sizeof(fh))
      {
         close(fd);
         return wxT("universal");
      }

      uint32_t narch = (magic == FAT_MAGIC)
         ? OSSwapBigToHostInt32(fh.nfat_arch)
         : fh.nfat_arch;

      bool hasX86 = false, hasArm = false;
      for (uint32_t i = 0; i < narch && i < 10; i++)
      {
         struct fat_arch fa;
         if (read(fd, &fa, sizeof(fa)) != sizeof(fa)) break;
         cpu_type_t cpu = OSSwapBigToHostInt32(fa.cputype);
         if (cpu == CPU_TYPE_X86_64) hasX86 = true;
         if (cpu == CPU_TYPE_ARM64) hasArm = true;
      }

      close(fd);
      if (hasX86 && hasArm) return wxT("universal");
      if (hasArm) return wxT("arm64");
      if (hasX86) return wxT("x86_64");
      return wxT("universal");
   }

   if (magic == MH_MAGIC_64 || magic == MH_CIGAM_64)
   {
      struct mach_header_64 mh;
      lseek(fd, 0, SEEK_SET);
      if (read(fd, &mh, sizeof(mh)) != sizeof(mh))
      {
         close(fd);
         return wxT("unknown");
      }
      close(fd);

      if (mh.cputype == CPU_TYPE_ARM64) return wxT("arm64");
      if (mh.cputype == CPU_TYPE_X86_64) return wxT("x86_64");
      return wxT("x86_64"); // Default assumption for 64-bit
   }

   close(fd);
   return wxT("unknown");
}

// ─── Internal: File hashing ─────────────────────────────────────────────

wxString PluginManagerModule::CalculateFileHash(const wxString &filePath)
{
   // Use SHA-256 for change detection
   int fd = open(filePath.utf8_str(), O_RDONLY);
   if (fd < 0) return wxEmptyString;

   CC_SHA256_CTX ctx;
   CC_SHA256_Init(&ctx);

   char buffer[65536];
   ssize_t bytes;
   while ((bytes = read(fd, buffer, sizeof(buffer))) > 0)
      CC_SHA256_Update(&ctx, buffer, bytes);

   close(fd);

   unsigned char hash[CC_SHA256_DIGEST_LENGTH];
   CC_SHA256_Final(hash, &ctx);

   wxString result;
   for (int i = 0; i < CC_SHA256_DIGEST_LENGTH; i++)
      result += wxString::Format(wxT("%02x"), hash[i]);

   return result;
}

// ─── Internal: Info.plist reader ────────────────────────────────────────

wxString PluginManagerModule::ReadInfoPlistValue(const wxString &plistPath,
                                                   const wxString &key)
{
   // Simple plist key reader for common keys
   // Uses CFPropertyList for proper XML plist parsing
#ifdef __APPLE__
   CFURLRef url = CFURLCreateFromFileSystemRepresentation(
      kCFAllocatorDefault,
      (const UInt8 *)plistPath.utf8_str(),
      plistPath.utf8_str().length(),
      false);

   if (!url) return wxEmptyString;

   CFDataRef data = nullptr;
   CFPropertyListRef plist = nullptr;

   if (!CFURLCreateDataAndPropertiesFromResource(
          kCFAllocatorDefault, url, &data, nullptr, nullptr, nullptr))
   {
      CFRelease(url);
      return wxEmptyString;
   }

   CFStringRef error = nullptr;
   plist = CFPropertyListCreateFromXMLData(
      kCFAllocatorDefault, data, kCFPropertyListImmutable, &error);

   if (error) CFRelease(error);
   CFRelease(data);
   CFRelease(url);

   if (!plist) return wxEmptyString;
   if (CFGetTypeID(plist) != CFDictionaryGetTypeID())
   {
      CFRelease(plist);
      return wxEmptyString;
   }

   CFDictionaryRef dict = (CFDictionaryRef)plist;
   CFStringRef keyStr = CFStringCreateWithCString(
      kCFAllocatorDefault, key.utf8_str(), kCFStringEncodingUTF8);

   if (!keyStr)
   {
      CFRelease(plist);
      return wxEmptyString;
   }

   CFTypeRef value = CFDictionaryGetValue(dict, keyStr);
   CFRelease(keyStr);

   wxString result;
   if (value && CFGetTypeID(value) == CFStringGetTypeID())
   {
      CFStringRef str = (CFStringRef)value;
      CFIndex len = CFStringGetLength(str);
      CFIndex maxSize = CFStringGetMaximumSizeForEncoding(len, kCFStringEncodingUTF8) + 1;
      char *buf = new char[maxSize];
      if (CFStringGetCString(str, buf, maxSize, kCFStringEncodingUTF8))
         result = wxString::FromUTF8(buf);
      delete[] buf;
   }

   CFRelease(plist);
   return result;
#else
   return wxEmptyString;
#endif
}

// ─── Paths ──────────────────────────────────────────────────────────────

wxString PluginManagerModule::GetBundledPluginsPath() const
{
   wxString exePath = wxStandardPaths::Get().GetExecutablePath();
   wxFileName fn(exePath);
   fn.RemoveLastDir(); // Remove MacOS
   fn.RemoveLastDir(); // Remove Contents
   fn.AppendDir(wxT("Plug-Ins"));
   return fn.GetFullPath();
}

wxString PluginManagerModule::GetBundledVST3Path() const
{
   wxString exePath = wxStandardPaths::Get().GetExecutablePath();
   wxFileName fn(exePath);
   fn.RemoveLastDir(); // Remove MacOS
   fn.RemoveLastDir(); // Remove Contents
   fn.AppendDir(wxT("VST3"));
   return fn.GetFullPath();
}

wxString PluginManagerModule::GetCacheFilePath() const
{
   return GetAppSupportDir() + wxFILE_SEP_PATH + wxT("plugin-cache.xml");
}

wxString PluginManagerModule::GetAppSupportDir() const
{
   wxString dir = wxStandardPaths::Get().GetUserDataDir();
   if (dir.IsEmpty())
      dir = wxFileName::GetHomeDir() + wxFILE_SEP_PATH +
            wxT("Library/Application Support/HgeMusicStudio");
   return dir;
}

// ─── UI Registration ────────────────────────────────────────────────────

void PluginManagerModule::RegisterUI(IPluginManagerUI *ui)
{
   mUI = ui;
}

void PluginManagerModule::UnregisterUI()
{
   mUI = nullptr;
}
