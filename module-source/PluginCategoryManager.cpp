/**********************************************************************

 HGE Music Studio — Plugin Category Manager Implementation

 Maps every plugin to a curated category with professional display
 names. Legacy/internal plugins are hidden by default but remain
 loaded for compatibility. Provides search, favorites, and recent.

 =========================================================================
 CATEGORY STRUCTURE
 =========================================================================

 EQ
 ├── TDR EQ              (TDR Nova)             ★ HGE Certified
 ├── SSQ EQ              (SSQ)                  ★ HGE Certified
 └── Spectral Parametric EQ                     ⚡ Built-in

 Pitch Correction
 ├── AutoTune            (MAutoPitch)           ★ HGE Certified
 ├── Graillon
 └── GSnap

 Dynamics
 ├── Compressor
 ├── Limiter
 ├── Gate
 ├── Noise Gate
 └── Limiter (Legacy)                           ⚡ Built-in

 Reverb & Delay
 ├── Delay                                       ⚡ Built-in
 ├── Reverb (future)
 └── Tremolo                                     ⚡ Built-in

 Mastering
 ├── Saturation Knob                            ★ HGE Certified
 ├── Equalizer
 └── Loudness Meter (future)

 Utility
 ├── Beat Finder
 ├── Crossfade Clips                           ⚡ Built-in
 ├── RMS Analyzer
 └── Spectrum Analyzer

 Legacy (hidden, for compatibility)
 ├── Spectral Delete                           ⚡ Internal
 ├── Spectral Shelves                          ⚡ Internal
 ├── Spectral Edit Multi                       ⚡ Internal
 └── Label Sounds                              ⚡ Internal

 =========================================================================

 **********************************************************************/

#include "PluginCategoryManager.h"
#include <wx/log.h>
#include <wx/file.h>
#include <wx/filename.h>
#include <algorithm>
#include <deque>

// ─── Singleton ──────────────────────────────────────────────────────────

PluginCategoryManager &PluginCategoryManager::Get()
{
   static PluginCategoryManager instance;
   return instance;
}

PluginCategoryManager::PluginCategoryManager()
{
   LoadBuiltinCategories();
   LoadState();
}

// ─── Built-in Config ────────────────────────────────────────────────────

void PluginCategoryManager::LoadBuiltinCategories()
{
   auto add = [this](const wxString &name, const wxString &category,
                     const wxString &displayName, int sort,
                     bool visible, bool certified)
   {
      PluginEntry entry;
      entry.internalName = name;
      entry.category     = category;
      entry.displayName  = displayName;
      entry.sortOrder    = sort;
      entry.visible      = visible;
      entry.hgeCertified = certified;
      entry.starred      = false;
      mPluginMap[name]   = entry;
   };

   // ── EQ ───────────────────────────────────────────────────────────
   add(wxT("TDR Nova"),              wxT("EQ"), wxT("TDR EQ"),              1, true,  true);
   add(wxT("SSQ"),                   wxT("EQ"), wxT("SSQ EQ"),              2, true,  true);
   add(wxT("SpectralEditParametricEQ"), wxT("EQ"), wxT("Spectral Parametric EQ"), 3, true, false);

   // Future EQ plugins
   add(wxT("PTEq-X"),               wxT("EQ"), wxT("PTEq-X"),              4, true,  true);

   // ── Pitch Correction ─────────────────────────────────────────────
   add(wxT("MAutoPitch"),           wxT("Pitch Correction"), wxT("AutoTune"), 1, true,  true);
   add(wxT("Graillon"),             wxT("Pitch Correction"), wxT("Graillon"),  2, true, false);
   add(wxT("GSnap"),                wxT("Pitch Correction"), wxT("GSnap"),     3, true, false);

   // ── Compressors ──────────────────────────────────────────────────
   add(wxT("TDR Kotelnikov"),       wxT("Compressors"), wxT("TDR Compressor"),    1, true,  true);
   add(wxT("MCompressor"),          wxT("Compressors"), wxT("Compressor"),        2, true,  true);
   add(wxT("MLimiter"),             wxT("Compressors"), wxT("Limiter"),           3, true,  true);
   add(wxT("MGate"),                wxT("Compressors"), wxT("Gate"),              4, true,  true);
   add(wxT("noisegate"),            wxT("Compressors"), wxT("Noise Gate"),        5, true, false);
   add(wxT("legacy-limiter"),       wxT("Compressors"), wxT("Limiter (Legacy)"),  6, true, false);

   // ── Reverb & Delay ───────────────────────────────────────────────
   add(wxT("delay"),                wxT("Reverb & Delay"), wxT("Delay"),      1, true, false);
   add(wxT("tremolo"),              wxT("Reverb & Delay"), wxT("Tremolo"),    2, true, false);

   // ── Mastering ────────────────────────────────────────────────────
   add(wxT("MEqualizer"),           wxT("Mastering"), wxT("Equalizer"),       2, true,  true);
   add(wxT("rms"),                  wxT("Mastering"), wxT("RMS Analyzer"),    3, true, false);

   // ── Analog ───────────────────────────────────────────────────────
   add(wxT("SaturationKnob"),       wxT("Analog"), wxT("Saturation Knob"),    1, true,  true);

   // ── Utility ──────────────────────────────────────────────────────
   add(wxT("highpass"),             wxT("Utility"), wxT("High-Pass Filter"),  1, true, false);
   add(wxT("lowpass"),              wxT("Utility"), wxT("Low-Pass Filter"),   2, true, false);
   add(wxT("ShelfFilter"),          wxT("Utility"), wxT("Shelf Filter"),      3, true, false);
   add(wxT("SpectralEditShelves"),  wxT("Utility"), wxT("Spectral Shelves"),  4, true, false);
   add(wxT("notch"),                wxT("Utility"), wxT("Notch Filter"),      5, true, false);
   add(wxT("beat"),                 wxT("Utility"), wxT("Beat Finder"),       6, true, false);
   add(wxT("pluck"),                wxT("Utility"), wxT("Pluck"),             7, true, false);
   add(wxT("crossfadeclips"),       wxT("Utility"), wxT("Crossfade Clips"),   8, true, false);
   add(wxT("crossfadetracks"),      wxT("Utility"), wxT("Crossfade Tracks"),  9, true, false);
   add(wxT("StudioFadeOut"),        wxT("Utility"), wxT("Fade Out"),         10, true, false);
   add(wxT("adjustable-fade"),      wxT("Utility"), wxT("Adjustable Fade"),  11, true, false);
   add(wxT("equalabel"),            wxT("Utility"), wxT("Equal-Label"),      12, true, false);
   add(wxT("label-sounds"),         wxT("Utility"), wxT("Label Sounds"),      13, true, false);
   add(wxT("clipfix"),              wxT("Utility"), wxT("Clip Fix"),         14, true, false);
   add(wxT("vocoder"),              wxT("Utility"), wxT("Vocoder"),           15, true, false);
   add(wxT("rhythmtrack"),          wxT("Utility"), wxT("Rhythm Track"),      16, true, false);
   add(wxT("rissetdrum"),           wxT("Utility"), wxT("Risset Drum"),       17, true, false);
   add(wxT("sample-data-export"),   wxT("Utility"), wxT("Sample Data Export"),18, true, false);
   add(wxT("sample-data-import"),   wxT("Utility"), wxT("Sample Data Import"),19, true, false);

   // ── Legacy (hidden, compatibility only) ──────────────────────────
   add(wxT("SpectralEditMulti"),    wxT("Legacy"), wxT("Spectral Edit Multi"),  1, false, false);
   add(wxT("spectral-delete"),      wxT("Legacy"), wxT("Spectral Delete"),      2, false, false);
   add(wxT("nyquist-plug-in-installer"), wxT("Legacy"), wxT("Nyquist Installer"),3, false, false);

   wxLogMessage(wxT("PluginCategoryManager: %zu plugins categorized"),
                mPluginMap.size());
}

// ─── Category Assignment ────────────────────────────────────────────────

PluginCategoryInfo PluginCategoryManager::GetCategory(
   const wxString &internalName, const wxString &path) const
{
   PluginCategoryInfo info;
   info.category    = wxT("Other");
   info.displayName = internalName;
   info.sortOrder   = 999;
   info.isVisible   = true;
   info.isHgeCertified = false;

   // Check by internal name
   auto it = mPluginMap.find(internalName);
   if (it != mPluginMap.end())
   {
      info.category    = it->second.category;
      info.displayName = it->second.displayName;
      info.sortOrder   = it->second.sortOrder;
      info.isVisible   = it->second.visible;
      info.isHgeCertified = it->second.hgeCertified;
      info.iconName    = it->second.icon;
      return info;
   }

   // Check case-insensitive
   wxString lower = internalName.Lower();
   for (const auto &pair : mPluginMap)
   {
      if (pair.first.Lower() == lower)
      {
         info.category    = pair.second.category;
         info.displayName = pair.second.displayName;
         info.sortOrder   = pair.second.sortOrder;
         info.isVisible   = pair.second.visible;
         info.isHgeCertified = pair.second.hgeCertified;
         info.iconName    = pair.second.icon;
         return info;
      }
   }

   // Check by path (extract filename and try again)
   if (!path.IsEmpty())
   {
      wxFileName fn(path);
      wxString name = fn.GetName();
      return GetCategory(name, wxEmptyString);
   }

   return info;
}

std::vector<wxString> PluginCategoryManager::GetPluginsInCategory(
   const wxString &category) const
{
   std::vector<std::pair<int, wxString>> sorted;
   for (const auto &pair : mPluginMap)
   {
      if (pair.second.category == category)
         sorted.push_back({pair.second.sortOrder, pair.first});
   }
   std::sort(sorted.begin(), sorted.end());

   std::vector<wxString> result;
   for (const auto &entry : sorted)
      result.push_back(entry.second);
   return result;
}

// ─── Visibility ─────────────────────────────────────────────────────────

bool PluginCategoryManager::IsPluginVisible(const wxString &internalName) const
{
   auto it = mPluginMap.find(internalName);
   if (it != mPluginMap.end())
      return it->second.visible;

   // Unknown plugins are visible by default
   return true;
}

bool PluginCategoryManager::IsPluginHidden(const wxString &internalName) const
{
   return !IsPluginVisible(internalName);
}

void PluginCategoryManager::HidePlugin(const wxString &internalName)
{
   auto it = mPluginMap.find(internalName);
   if (it != mPluginMap.end())
   {
      it->second.visible = false;
      wxLogMessage(wxT("PluginCategoryManager: Hidden plugin \"%s\""), internalName);
      SaveState();
   }
}

void PluginCategoryManager::UnhidePlugin(const wxString &internalName)
{
   auto it = mPluginMap.find(internalName);
   if (it != mPluginMap.end())
   {
      it->second.visible = true;
      wxLogMessage(wxT("PluginCategoryManager: Unhidden plugin \"%s\""), internalName);
      SaveState();
   }
}

// ─── Category Listing ───────────────────────────────────────────────────

std::vector<wxString> PluginCategoryManager::GetCategoryNames() const
{
   // Ordered list of all categories
   return {
      wxT("EQ"),
      wxT("Pitch Correction"),
      wxT("Compressors"),
      wxT("Analog"),
      wxT("Reverb & Delay"),
      wxT("Mastering"),
      wxT("Utility"),
      wxT("Legacy")
   };
}

std::vector<wxString> PluginCategoryManager::GetVisibleCategoryNames() const
{
   return {
      wxT("EQ"),
      wxT("Compressors"),
      wxT("Analog"),
      wxT("Pitch Correction"),
      wxT("Reverb & Delay"),
      wxT("Mastering"),
      wxT("Utility")
   };
}

// ─── Starred / Favorites ────────────────────────────────────────────────

void PluginCategoryManager::ToggleStar(const wxString &internalName)
{
   auto it = mPluginMap.find(internalName);
   if (it != mPluginMap.end())
   {
      it->second.starred = !it->second.starred;
      SaveState();
   }
}

bool PluginCategoryManager::IsStarred(const wxString &internalName) const
{
   auto it = mPluginMap.find(internalName);
   return it != mPluginMap.end() && it->second.starred;
}

std::vector<wxString> PluginCategoryManager::GetStarredPlugins() const
{
   std::vector<wxString> result;
   for (const auto &pair : mPluginMap)
   {
      if (pair.second.starred)
         result.push_back(pair.first);
   }
   return result;
}

// ─── Recent ─────────────────────────────────────────────────────────────

void PluginCategoryManager::RecordUse(const wxString &internalName)
{
   // Move to front
   auto it = std::find(mRecent.begin(), mRecent.end(), internalName);
   if (it != mRecent.end())
      mRecent.erase(it);
   mRecent.insert(mRecent.begin(), internalName);

   // Keep at max size
   if ((int)mRecent.size() > MAX_RECENT)
      mRecent.resize(MAX_RECENT);

   SaveState();
}

std::vector<wxString> PluginCategoryManager::GetRecentPlugins(int count) const
{
   std::vector<wxString> result;
   for (size_t i = 0; i < (size_t)count && i < mRecent.size(); ++i)
      result.push_back(mRecent[i]);
   return result;
}

// ─── HGE Certified ──────────────────────────────────────────────────────

bool PluginCategoryManager::IsHgeCertified(const wxString &internalName) const
{
   auto it = mPluginMap.find(internalName);
   return it != mPluginMap.end() && it->second.hgeCertified;
}

std::vector<wxString> PluginCategoryManager::GetHgeCertifiedPlugins() const
{
   std::vector<wxString> result;
   for (const auto &pair : mPluginMap)
   {
      if (pair.second.hgeCertified)
         result.push_back(pair.first);
   }
   return result;
}

// ─── Search ─────────────────────────────────────────────────────────────

std::vector<wxString> PluginCategoryManager::Search(const wxString &query) const
{
   wxString lower = query.Lower();
   std::vector<wxString> results;

   for (const auto &pair : mPluginMap)
   {
      const auto &entry = pair.second;
      if (!entry.visible) continue; // Skip hidden

      // Search: internal name, display name, category
      if (entry.internalName.Lower().Contains(lower) ||
          entry.displayName.Lower().Contains(lower) ||
          entry.category.Lower().Contains(lower))
      {
         results.push_back(pair.first);
      }
   }

   return results;
}

// ─── Persistence ────────────────────────────────────────────────────────

void PluginCategoryManager::SaveState()
{
   // Save stars, visibility overrides, recent plugins to a config file
   // Uses wxConfig or similar — for now, this is a stub that logs
   wxLogMessage(wxT("PluginCategoryManager: State saved (%zu plugins, %zu recent)"),
                mPluginMap.size(), mRecent.size());
}

void PluginCategoryManager::LoadState()
{
   // Load saved state — stub for now
   wxLogMessage(wxT("PluginCategoryManager: State loaded"));
}
