/**********************************************************************

 HGE Music Studio — Plugin Display Name System

 Maps internal plugin names to consumer-friendly display names.
 Eliminates vendor submenus, cleans up path names, and provides
 consistent "HGE Certified" branding across all bundled plugins.

 =========================================================================
 NAME RESOLUTION ORDER
 =========================================================================
 1. Exact match in alias table          → "TDR Nova" → "TDR EQ"
 2. Case-insensitive match in table     → "tdr nova" → "TDR EQ"
 3. Vendor-prefixed check               → "Tokyo Dawn Labs: TDR Nova" → "TDR EQ"
 4. Path-based name extraction          → "/.../TDR Nova.vst" → "TDR Nova"
 5. CamelCase splitting                 → "SpectralEditParametricEQ" → "Spectral Edit Parametric EQ"

 =========================================================================
 VENDOR HANDLING
 =========================================================================
 Vendor submenus are disabled (flat menu). Display vendor is the human-readable
 company name (e.g., "Tokyo Dawn Labs" → "TDR"). All vendor nesting is stripped.

 =========================================================================
 BUILT-IN ALIASES
 =========================================================================
 See plugin-aliases.xml for the full alias configuration.
 Currently covers all 28 Nyquist scripts + bundled VST2/VST3 plugins.

 **********************************************************************/

#include "PluginDisplayName.h"

#include <algorithm>
#include <cctype>
#include <map>
#include <vector>
#include <sstream>
#include <fstream>

// ─── Static Data ────────────────────────────────────────────────────────

struct AliasEntry
{
   std::string internal;
   std::string display;
   bool        hgeCertified;
   bool        hidden;
};

static std::vector<AliasEntry> sAliases;
static std::set<std::string>   sHiddenVendors;
static bool sLoaded = false;

// ─── Built-in Aliases ───────────────────────────────────────────────────

static void LoadBuiltinAliasesImpl()
{
   // VST2 Bundled
   sAliases.push_back({"TDR Nova",           "TDR EQ",           true, false});
   sAliases.push_back({"TDR Kotelnikov",     "TDR Compressor",   true, false});
   sAliases.push_back({"SaturationKnob",     "Saturation Knob",  true, false});
   sAliases.push_back({"Saturation Knob",    "Saturation Knob",  true, false});
   sAliases.push_back({"SSQ",                "SSQ",              true, false});

   // Nyquist — EQ
   sAliases.push_back({"SpectralEditParametricEQ", "Spectral EQ",    false, false});
   sAliases.push_back({"SpectralEditShelves",      "Spectral Shelves", false, false});
   sAliases.push_back({"ShelfFilter",              "Shelf Filter",   false, false});
   sAliases.push_back({"highpass",                 "High Pass Filter", false, false});
   sAliases.push_back({"lowpass",                  "Low Pass Filter",  false, false});
   sAliases.push_back({"notch",                    "Notch Filter",    false, false});

   // Nyquist — Dynamics
   sAliases.push_back({"noisegate",                "Noise Gate",      false, false});
   sAliases.push_back({"legacy-limiter",           "Legacy Limiter",  false, false});
   sAliases.push_back({"crossfadeclips",           "Crossfade Clips", false, false});
   sAliases.push_back({"crossfadetracks",          "Crossfade Tracks",false, false});
   sAliases.push_back({"StudioFadeOut",            "Studio Fade Out", false, false});
   sAliases.push_back({"adjustable-fade",          "Adjustable Fade", false, false});
   sAliases.push_back({"clipfix",                  "Clip Fix",        false, false});

   // Nyquist — Mastering
   sAliases.push_back({"rms",                      "RMS Analyzer",    false, false});
   sAliases.push_back({"limiter",                  "Limiter",         false, false});
   sAliases.push_back({"hardlimiter",              "Hard Limiter",    false, false});

   // Nyquist — Reverb & Delay
   sAliases.push_back({"delay",                    "Delay",           false, false});
   sAliases.push_back({"tremolo",                  "Tremolo",         false, false});

   // Nyquist — Utility
   sAliases.push_back({"beat",                     "Beat",            false, false});
   sAliases.push_back({"pluck",                    "Pluck",           false, false});
   sAliases.push_back({"rhythmtrack",              "Rhythm Track",    false, false});
   sAliases.push_back({"rissetdrum",               "Risset Drum",     false, false});
   sAliases.push_back({"vocoder",                  "Vocoder",         false, false});
   sAliases.push_back({"equalabel",                "Label Sounds",    false, false});
   sAliases.push_back({"label-sounds",             "Label Sounds",    false, false});
   sAliases.push_back({"sample-data-export",       "Sample Export",   false, false});
   sAliases.push_back({"sample-data-import",       "Sample Import",   false, false});

   // Nyquist — Spectral (hidden by default)
   sAliases.push_back({"SpectralEditMulti",        "Spectral Editor", false, false});
   sAliases.push_back({"spectral-delete",          "Spectral Delete", false, true});

   // Hidden utilities
   sAliases.push_back({"nyquist-plug-in-installer","Plugin Installer",false, true});

   // Hide vendor submenus
   sHiddenVendors.insert("Tokyo Dawn Labs");
   sHiddenVendors.insert("Softube");
   sHiddenVendors.insert("MeldaProduction");
   sHiddenVendors.insert("Audacity");
}

// ─── String Helpers ─────────────────────────────────────────────────────

static std::string toLower(const std::string &s)
{
   std::string r;
   r.reserve(s.size());
   for (char c : s) r.push_back(std::tolower(c));
   return r;
}

static std::string trim(const std::string &s)
{
   size_t start = s.find_first_not_of(" \t\r\n");
   size_t end = s.find_last_not_of(" \t\r\n");
   if (start == std::string::npos) return "";
   return s.substr(start, end - start + 1);
}

static std::string splitCamelCase(const std::string &s)
{
   std::string result;
   for (size_t i = 0; i < s.size(); i++)
   {
      if (i > 0 && std::isupper(s[i]) && !std::isupper(s[i-1]))
         result += ' ';
      result += s[i];
   }
   return result;
}

static std::string cleanPluginPath(const std::string &path)
{
   std::string name = path;

   // Remove directory path
   auto lastSlash = name.find_last_of("/\\");
   if (lastSlash != std::string::npos)
      name = name.substr(lastSlash + 1);

   // Remove extension
   for (const auto &ext : {".vst3", ".vst", ".component", ".lv2", ".ny", ".dll", ".so"})
   {
      if (name.size() >= ext.size() &&
          name.substr(name.size() - ext.size()) == ext)
      {
         name = name.substr(0, name.size() - ext.size());
         break;
      }
   }

   // Remove "Contents/MacOS/" artifacts
   auto macosPos = name.find("Contents/MacOS/");
   if (macosPos != std::string::npos)
      name = name.substr(macosPos + 15);

   return trim(name);
}

// ─── API Implementation ─────────────────────────────────────────────────

std::string PluginDisplayName::GetDisplayName(const std::string &internalName,
                                              const std::string &vendor,
                                              const std::string &format)
{
   if (!sLoaded) { LoadBuiltinAliasesImpl(); sLoaded = true; }

   std::string clean = cleanPluginPath(internalName);

   // 1. Exact match
   for (const auto &a : sAliases)
      if (toLower(a.internal) == toLower(clean))
         return a.display;

   // 2. Exact match on original name (before path cleaning)
   for (const auto &a : sAliases)
      if (toLower(a.internal) == toLower(internalName))
         return a.display;

   // 3. Vendor-prefixed: "Vendor: Name" → check alias
   auto colonPos = clean.find(':');
   if (colonPos != std::string::npos)
   {
      std::string afterVendor = trim(clean.substr(colonPos + 1));
      for (const auto &a : sAliases)
         if (toLower(a.internal) == toLower(afterVendor))
            return a.display;
   }

   // 4. Fallback: clean up the name
   std::string display = splitCamelCase(clean);
   return display;
}

std::string PluginDisplayName::GetDisplayVendor(const std::string &internalVendor)
{
   if (!sLoaded) { LoadBuiltinAliasesImpl(); sLoaded = true; }

   // Map known vendors to clean display names
   static const std::map<std::string, std::string> vendorMap = {
      {"Tokyo Dawn Labs", "TDR"},
      {"Softube",         "Softube"},
      {"MeldaProduction", "Melda"},
      {"Audacity",        ""},
   };

   auto it = vendorMap.find(internalVendor);
   if (it != vendorMap.end())
      return it->second;

   return internalVendor;
}

bool PluginDisplayName::ShouldShowVendorSubmenu(const std::string &vendor)
{
   // Always return false — we use flat menus
   // No vendor submenus in HGE Music Studio
   return false;
}

void PluginDisplayName::RegisterAlias(const std::string &internal,
                                       const std::string &display)
{
   for (auto &a : sAliases)
   {
      if (a.internal == internal)
      {
         a.display = display;
         return;
      }
   }
   sAliases.push_back({internal, display, false, false});
}

void PluginDisplayName::HideVendor(const std::string &vendor)
{
   sHiddenVendors.insert(vendor);
}

bool PluginDisplayName::LoadConfigFile(const std::string &path)
{
   // Load from plugin-aliases.xml
   // Simple XML parser — production should use a proper XML library
   std::ifstream file(path);
   if (!file.is_open()) return false;

   std::string line;
   while (std::getline(file, line))
   {
      // Parse: <Alias internal="..." display="..." hge="..." hidden="..."/>
      auto intPos = line.find("internal=\"");
      auto dispPos = line.find("display=\"");
      auto hgePos = line.find("hge=\"");
      auto hiddenPos = line.find("hidden=\"");

      if (intPos != std::string::npos && dispPos != std::string::npos)
      {
         intPos += 10;
         std::string internal = line.substr(intPos, line.find('"', intPos) - intPos);
         dispPos += 9;
         std::string display = line.substr(dispPos, line.find('"', dispPos) - dispPos);
         bool hge = (hgePos != std::string::npos &&
                     line.substr(hgePos + 5, 4) == "true");
         bool hidden = (hiddenPos != std::string::npos &&
                        line.substr(hiddenPos + 8, 4) == "true");

         sAliases.push_back({internal, display, hge, hidden});
      }
   }

   return true;
}

void PluginDisplayName::LoadBuiltinAliases()
{
   LoadBuiltinAliasesImpl();
}

std::vector<PluginDisplayName::AliasInfo> PluginDisplayName::GetAllAliases()
{
   if (!sLoaded) { LoadBuiltinAliasesImpl(); sLoaded = true; }

   std::vector<AliasInfo> result;
   for (const auto &a : sAliases)
      result.push_back({a.internal, a.display, a.hgeCertified, a.hidden});
   return result;
}

void PluginDisplayName::ClearAliases()
{
   sAliases.clear();
   sHiddenVendors.clear();
   sLoaded = false;
}
