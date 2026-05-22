/**********************************************************************

 HGE Music Studio — Plugin Display Name System : Implementation

 Maps raw plugin identifiers to consumer-facing labels and flattens
 the VST/AU/LV2 menu hierarchy so every plugin appears directly
 under its format heading without vendor sub-menus.

 Built-in aliases (loaded at startup):
   "TDR Nova"       → "TDR EQ"
   "TDR Nova"       → "TDR EQ"        (VST3 variant)
   "MAutoPitch"     → "AutoTune"
   "SaturationKnob" → "Saturation Knob"

 Add more via RegisterAlias() or an external config file.

 **********************************************************************/

#include "PluginDisplayName.h"

#include <wx/log.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <wx/txtstrm.h>
#include <wx/wfstream.h>
#include <wx/xml/xml.h>
#include <algorithm>
#include <cctype>

// ─── Singleton ──────────────────────────────────────────────────────────

PluginDisplayName &PluginDisplayName::Get()
{
   static PluginDisplayName instance;
   return instance;
}

// ─── Constructor loads built-in aliases ────────────────────────────────

PluginDisplayName::PluginDisplayName()
{
   LoadBuiltinAliases();
}

// ─── Core Lookup ────────────────────────────────────────────────────────

wxString PluginDisplayName::GetDisplayName(
   const wxString &internalName,
   const wxString &vendor,
   const wxString &format) const
{
   wxString key = internalName;

   // 1. Try exact match first
   auto it = mAliasMap.find(key);
   if (it != mAliasMap.end())
      return it->second;

   // 2. Try case-insensitive match
   wxString lowerKey = key.Lower();
   for (const auto &pair : mAliasMap)
   {
      if (pair.first.Lower() == lowerKey)
         return pair.second;
   }

   // 3. Try vendor-prefixed match (for "Tokyo Dawn Labs:TDR Nova" style)
   if (!vendor.IsEmpty())
   {
      wxString composite = vendor + wxT(":") + key;
      auto cit = mAliasMap.find(composite);
      if (cit != mAliasMap.end())
         return cit->second;
   }

   // 4. No alias found — clean up the raw name for display
   wxString display = key;

   // Remove common suffixes that look like file extensions
   if (display.Lower().EndsWith(wxT(".vst3")))
      display = display.Left(display.length() - 5);
   else if (display.Lower().EndsWith(wxT(".vst")))
      display = display.Left(display.length() - 4);
   else if (display.Lower().EndsWith(wxT(".component")))
      display = display.Left(display.length() - 10);
   else if (display.Lower().EndsWith(wxT(".lv2")))
      display = display.Left(display.length() - 4);
   else if (display.Lower().EndsWith(wxT(".ny")))
      display = display.Left(display.length() - 3);

   // Remove "Contents/MacOS/" etc. if a full path leaked through
   int pos = display.Find(wxT("Contents"));
   if (pos != wxNOT_FOUND)
   {
      display = display.Mid(0, pos);
      pos = display.Find(wxT('/'), true);
      if (pos != wxNOT_FOUND && pos < (int)display.length() - 1)
         display = display.Mid(pos + 1);
   }

   // CamelCase → space-separated (e.g. "SpectralEditMulti" → "Spectral Edit Multi")
   wxString spaced;
   for (size_t i = 0; i < display.length(); ++i)
   {
      wxChar ch = display[i];
      if (i > 0 && wxIsupper(ch) && !wxIsspace(display[i-1]))
         spaced += wxT(' ');
      spaced += ch;
   }
   if (!spaced.IsEmpty())
      display = spaced;

   // Clean up any double spaces
   while (display.Replace(wxT("  "), wxT(" "))) {}

   return display.Trim(true).Trim(false);
}

wxString PluginDisplayName::GetDisplayVendor(const wxString &internalVendor) const
{
   // Return empty string — this tells the menu builder not to create
   // a vendor submenu. ALL plugins appear flat under the format heading.
   return wxEmptyString;
}

bool PluginDisplayName::ShouldShowVendorSubmenu(const wxString &vendor) const
{
   // Never show vendor submenus. The menu is flat.
   return false;
}

// ─── Registration ───────────────────────────────────────────────────────

void PluginDisplayName::RegisterAlias(const wxString &internalName,
                                       const wxString &displayName)
{
   mAliasMap[internalName] = displayName;
   wxLogMessage(wxT("PluginDisplayName: Alias registered \"%s\" → \"%s\""),
                internalName, displayName);
}

void PluginDisplayName::HideVendor(const wxString &vendor)
{
   if (std::find(mHiddenVendors.begin(), mHiddenVendors.end(), vendor)
       == mHiddenVendors.end())
   {
      mHiddenVendors.push_back(vendor);
   }
}

// ─── Built-in Aliases ───────────────────────────────────────────────────

void PluginDisplayName::LoadBuiltinAliases()
{
   // ── HGE Music Studio Built-in Bundle ──────────────────────────────
   RegisterAlias(wxT("TDR Nova"),        wxT("TDR EQ"));
   RegisterAlias(wxT("MAutoPitch"),      wxT("AutoTune"));
   RegisterAlias(wxT("SaturationKnob"),  wxT("Saturation Knob"));
   RegisterAlias(wxT("MEqualizer"),      wxT("Equalizer"));
   RegisterAlias(wxT("MCompressor"),     wxT("Compressor"));
   RegisterAlias(wxT("MLimiter"),        wxT("Limiter"));
   RegisterAlias(wxT("MGate"),           wxT("Gate"));
   RegisterAlias(wxT("MTransformer"),    wxT("Transformer"));
   RegisterAlias(wxT("delay"),           wxT("Delay"));
   RegisterAlias(wxT("notch"),           wxT("Notch Filter"));
   RegisterAlias(wxT("highpass"),        wxT("High-Pass Filter"));
   RegisterAlias(wxT("lowpass"),         wxT("Low-Pass Filter"));
   RegisterAlias(wxT("ShelfFilter"),     wxT("Shelf Filter"));
   RegisterAlias(wxT("tremolo"),         wxT("Tremolo"));
   RegisterAlias(wxT("vocoder"),         wxT("Vocoder"));
   RegisterAlias(wxT("noisegate"),       wxT("Noise Gate"));
   RegisterAlias(wxT("legacy-limiter"),  wxT("Limiter (Legacy)"));
   RegisterAlias(wxT("rms"),             wxT("RMS Analyzer"));
   RegisterAlias(wxT("beat"),            wxT("Beat Finder"));
   RegisterAlias(wxT("pluck"),           wxT("Pluck"));
   RegisterAlias(wxT("rissetdrum"),      wxT("Risset Drum"));
   RegisterAlias(wxT("rhythmtrack"),     wxT("Rhythm Track"));
   RegisterAlias(wxT("crossfadeclips"),  wxT("Crossfade Clips"));
   RegisterAlias(wxT("crossfadetracks"), wxT("Crossfade Tracks"));
   RegisterAlias(wxT("StudioFadeOut"),   wxT("Fade Out"));
   RegisterAlias(wxT("adjustable-fade"), wxT("Adjustable Fade"));
   RegisterAlias(wxT("clipfix"),         wxT("Clip Fix"));
   RegisterAlias(wxT("equalabel"),       wxT("Equal-Label"));
   RegisterAlias(wxT("label-sounds"),    wxT("Label Sounds"));
   RegisterAlias(wxT("sample-data-export"), wxT("Sample Data Export"));
   RegisterAlias(wxT("sample-data-import"), wxT("Sample Data Import"));
   RegisterAlias(wxT("nyquist-plug-in-installer"), wxT("Nyquist Plugin Installer"));

   // Spectral edits
   RegisterAlias(wxT("SpectralEditMulti"),       wxT("Spectral Edit Multi"));
   RegisterAlias(wxT("SpectralEditParametricEQ"),wxT("Spectral Parametric EQ"));
   RegisterAlias(wxT("SpectralEditShelves"),     wxT("Spectral Shelves"));
   RegisterAlias(wxT("spectral-delete"),         wxT("Spectral Delete"));

   // ── Hide common vendors to flatten the menu ────────────────────────
   HideVendor(wxT("Tokyo Dawn Labs"));
   HideVendor(wxT("Tokyo Dawn Records"));
   HideVendor(wxT("MeldaProduction"));
   HideVendor(wxT("Waves"));
   HideVendor(wxT("iZotope"));
   HideVendor(wxT("FabFilter"));
   HideVendor(wxT("Valhalla"));
   HideVendor(wxT("Soundtoys"));
   HideVendor(wxT("Eventide"));
   HideVendor(wxT("Universal Audio"));
   HideVendor(wxT("Native Instruments"));
   HideVendor(wxT("Plugin Alliance"));
   HideVendor(wxT("Brainworx"));
   HideVendor(wxT("PSP Audio"));
   HideVendor(wxT("AudioThing"));
   HideVendor(wxT("Klanghelm"));
   HideVendor(wxT("ToneBoosters"));
   HideVendor(wxT("Voxengo"));
   HideVendor(wxT("Sonic Anomaly"));
   HideVendor(wxT("Variety Of Sound"));

   wxLogMessage(wxT("PluginDisplayName: %zu built-in aliases loaded"),
                mAliasMap.size());
}

// ─── Config File Loading ────────────────────────────────────────────────

bool PluginDisplayName::LoadConfigFile(const wxString &filePath)
{
   if (!wxFile::Exists(filePath))
   {
      wxLogWarning(wxT("PluginDisplayName: Config file not found: %s"), filePath);
      return false;
   }

   wxXmlDocument doc;
   if (!doc.Load(filePath))
   {
      wxLogWarning(wxT("PluginDisplayName: Failed to parse config: %s"), filePath);
      return false;
   }

   wxXmlNode *root = doc.GetRoot();
   if (!root || root->GetName() != wxT("PluginAliases"))
      return false;

   wxXmlNode *child = root->GetChildren();
   int count = 0;
   while (child)
   {
      if (child->GetName() == wxT("Alias"))
      {
         wxString internal = child->GetAttribute(wxT("internal"));
         wxString display  = child->GetAttribute(wxT("display"));
         if (!internal.IsEmpty() && !display.IsEmpty())
         {
            RegisterAlias(internal, display);
            count++;
         }
      }
      child = child->GetNext();
   }

   wxLogMessage(wxT("PluginDisplayName: Loaded %d aliases from %s"),
                count, filePath);
   return true;
}

// ─── Utilities ──────────────────────────────────────────────────────────

wxString PluginDisplayName::SanitizePath(const wxString &path) const
{
   wxString safe = path;

   // Strip app bundle internal paths
   int idx = safe.Find(wxT(".app/Contents/"));
   if (idx != wxNOT_FOUND)
   {
      safe = safe.Mid(idx + 5); // skip ".app"
   }

   // Strip absolute paths, keep just the plugin name
   wxFileName fn(safe);
   wxString name = fn.GetName();

   // Remove common extensions
   if (name.Lower().EndsWith(wxT(".vst3")))
      name = name.Left(name.length() - 5);
   else if (name.Lower().EndsWith(wxT(".vst")))
      name = name.Left(name.length() - 4);

   // Fall back to display name if available
   wxString display = GetDisplayName(name);
   if (display != name)
      return display;

   return name;
}

wxString PluginDisplayName::NameFromPath(const wxString &path) const
{
   wxFileName fn(path);
   return fn.GetName();
}

std::vector<std::pair<wxString, wxString>>
PluginDisplayName::GetAllAliases() const
{
   std::vector<std::pair<wxString, wxString>> result;
   for (const auto &pair : mAliasMap)
      result.push_back(pair);
   return result;
}

void PluginDisplayName::ClearAliases()
{
   mAliasMap.clear();
   mHiddenVendors.clear();
   wxLogMessage(wxT("PluginDisplayName: All aliases cleared"));
}
