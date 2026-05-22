/**********************************************************************

 HGE Music Studio — Plugin Category Manager Implementation

 Maps all bundled plugins to 6 curated categories. Legacy Nyquist
 scripts are hidden (not deleted) for compatibility. HGE Certified
 badge is applied to premium/tier-1 plugins.

 Category Order:
   1. EQ
   2. Pitch Correction
   3. Dynamics
   4. Reverb & Delay
   5. Mastering
   6. Utility

 **********************************************************************/

#include "PluginCategoryManager.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <chrono>

// ─── Category Display Names ─────────────────────────────────────────────

static const std::vector<std::string> kCategories = {
   "EQ",
   "Pitch Correction",
   "Dynamics",
   "Reverb & Delay",
   "Mastering",
   "Utility",
   "Legacy"  // hidden from menu but kept for compatibility
};

// ─── Singleton ─────────────────────────────────────────────────────────

PluginCategoryManager &PluginCategoryManager::Get()
{
   static PluginCategoryManager instance;
   return instance;
}

// ─── Built-in Categories ───────────────────────────────────────────────

void PluginCategoryManager::LoadBuiltinCategories()
{
   mPlugins.clear();
   mCategoryOrder.clear();

   int order = 1;
   for (const auto &cat : kCategories)
      mCategoryOrder[cat] = order++;

   int sortIdx = 0;

   // ═══════════════════════════════════════════════════════════════════
   // EQ
   // ═══════════════════════════════════════════════════════════════════
   mPlugins.push_back({"TDR Nova",           "EQ", "TDR EQ",                 1,  true, true, false, ""});
   mPlugins.push_back({"SSQ",                "EQ", "SSQ",                    2,  true, true, false, ""});
   mPlugins.push_back({"SpectralEditParametricEQ", "EQ", "Spectral EQ",     3,  true, false, false, ""});
   mPlugins.push_back({"PTEq-X",             "EQ", "PTEq-X",                4,  true, true, false, ""});

   // ═══════════════════════════════════════════════════════════════════
   // Pitch Correction
   // ═══════════════════════════════════════════════════════════════════
   mPlugins.push_back({"MAutoPitch",         "Pitch Correction", "AutoTune", 1,  true, true, false, ""});
   mPlugins.push_back({"Graillon",           "Pitch Correction", "Graillon", 2,  true, false, false, ""});
   mPlugins.push_back({"GSnap",              "Pitch Correction", "GSnap",    3,  true, false, false, ""});

   // ═══════════════════════════════════════════════════════════════════
   // Dynamics
   // ═══════════════════════════════════════════════════════════════════
   mPlugins.push_back({"TDR Kotelnikov",     "Dynamics", "TDR Compressor",    1,  true, true, false, ""});
   mPlugins.push_back({"MCompressor",        "Dynamics", "Compressor",       2,  true, true, false, ""});
   mPlugins.push_back({"MLimiter",           "Dynamics", "Limiter",          3,  true, true, false, ""});
   mPlugins.push_back({"MGate",              "Dynamics", "Gate",             4,  true, false, false, ""});
   mPlugins.push_back({"noisegate",          "Dynamics", "Noise Gate",       5,  true, false, false, ""});
   mPlugins.push_back({"legacy-limiter",     "Dynamics", "Legacy Limiter",   6,  true, false, false, ""});
   mPlugins.push_back({"SpectralEditMulti",  "Dynamics", "Spectral Multi",   7,  true, false, false, ""});
   mPlugins.push_back({"crossfadeclips",     "Dynamics", "Crossfade Clips",  8,  true, false, false, ""});
   mPlugins.push_back({"crossfadetracks",    "Dynamics", "Crossfade Tracks", 9,  true, false, false, ""});
   mPlugins.push_back({"StudioFadeOut",      "Dynamics", "Studio Fade Out",  10, true, false, false, ""});

   // ═══════════════════════════════════════════════════════════════════
   // Reverb & Delay
   // ═══════════════════════════════════════════════════════════════════
   mPlugins.push_back({"delay",              "Reverb & Delay", "Delay",      1,  true, false, false, ""});
   mPlugins.push_back({"tremolo",            "Reverb & Delay", "Tremolo",    2,  true, false, false, ""});

   // ═══════════════════════════════════════════════════════════════════
   // Mastering
   // ═══════════════════════════════════════════════════════════════════
   mPlugins.push_back({"SaturationKnob",     "Mastering", "Saturation",      1,  true, true, false, ""});
   mPlugins.push_back({"MEqualizer",         "Mastering", "Equalizer",       2,  true, false, false, ""});
   mPlugins.push_back({"rms",                "Mastering", "RMS Analyzer",    3,  true, false, false, ""});
   mPlugins.push_back({"limiter",            "Mastering", "Limiter",         4,  true, false, false, ""});
   mPlugins.push_back({"hardlimiter",        "Mastering", "Hard Limiter",    5,  true, false, false, ""});
   mPlugins.push_back({"SciFiBassReverb",    "Mastering", "Bass Reverb",     6,  true, false, false, ""});

   // ═══════════════════════════════════════════════════════════════════
   // Utility
   // ═══════════════════════════════════════════════════════════════════
   mPlugins.push_back({"highpass",           "Utility", "High Pass Filter",  1,  true, false, false, ""});
   mPlugins.push_back({"lowpass",            "Utility", "Low Pass Filter",   2,  true, false, false, ""});
   mPlugins.push_back({"notch",              "Utility", "Notch Filter",      3,  true, false, false, ""});
   mPlugins.push_back({"SpectralEditShelves","Utility", "Spectral Shelves",  4,  true, false, false, ""});
   mPlugins.push_back({"ShelfFilter",        "Utility", "Shelf Filter",      5,  true, false, false, ""});
   mPlugins.push_back({"adjustable-fade",    "Utility", "Adjustable Fade",   6,  true, false, false, ""});
   mPlugins.push_back({"beat",               "Utility", "Beat",              7,  true, false, false, ""});
   mPlugins.push_back({"clipfix",            "Utility", "Clip Fix",          8,  true, false, false, ""});
   mPlugins.push_back({"equalabel",          "Utility", "Label Sounds",      9,  true, false, false, ""});
   mPlugins.push_back({"label-sounds",       "Utility", "Label Sounds Alt",  10, true, false, false, ""});
   mPlugins.push_back({"pluck",              "Utility", "Pluck",             11, true, false, false, ""});
   mPlugins.push_back({"rhythmtrack",        "Utility", "Rhythm Track",      12, true, false, false, ""});
   mPlugins.push_back({"rissetdrum",         "Utility", "Risset Drum",       13, true, false, false, ""});
   mPlugins.push_back({"sample-data-export", "Utility", "Sample Export",     14, true, false, false, ""});
   mPlugins.push_back({"sample-data-import", "Utility", "Sample Import",     15, true, false, false, ""});
   mPlugins.push_back({"vocoder",            "Utility", "Vocoder",           16, true, false, false, ""});
   mPlugins.push_back({"nyquist-plug-in-installer", "Utility", "Plugin Installer", 17, false, false, false, ""});

   // ═══════════════════════════════════════════════════════════════════
   // Legacy (hidden — kept for compatibility)
   // ═══════════════════════════════════════════════════════════════════
   mPlugins.push_back({"spectral-delete",    "Legacy", "Spectral Delete",    1,  false, false, false, ""});
   mPlugins.push_back({"nyquist-plug-in-installer", "Legacy", "Nyquist Installer", 2, false, false, false, ""});
}

// ─── Access ─────────────────────────────────────────────────────────────

std::vector<PluginEntry> PluginCategoryManager::GetAllPlugins() const
{
   std::vector<PluginEntry> result;
   std::copy_if(mPlugins.begin(), mPlugins.end(), std::back_inserter(result),
      [](const PluginEntry &p) { return p.visible; });
   return result;
}

std::vector<PluginEntry> PluginCategoryManager::GetPluginsByCategory(const std::string &cat) const
{
   std::vector<PluginEntry> result;
   std::copy_if(mPlugins.begin(), mPlugins.end(), std::back_inserter(result),
      [&](const PluginEntry &p) { return p.category == cat && p.visible; });
   std::sort(result.begin(), result.end(),
      [](const PluginEntry &a, const PluginEntry &b) { return a.sortOrder < b.sortOrder; });
   return result;
}

std::vector<std::string> PluginCategoryManager::GetCategories() const
{
   std::vector<std::string> cats;
   for (const auto &cat : kCategories)
   {
      if (cat == "Legacy") continue;  // hide legacy category from menu
      auto it = mCategoryOrder.find(cat);
      if (it != mCategoryOrder.end())
         cats.push_back(cat);
   }
   return cats;
}

PluginEntry PluginCategoryManager::GetPlugin(const std::string &internalName) const
{
   for (const auto &p : mPlugins)
      if (p.internalName == internalName) return p;
   return {};
}

bool PluginCategoryManager::HasPlugin(const std::string &internalName) const
{
   return std::any_of(mPlugins.begin(), mPlugins.end(),
      [&](const PluginEntry &p) { return p.internalName == internalName; });
}

// ─── Favorites ─────────────────────────────────────────────────────────

void PluginCategoryManager::ToggleStar(const std::string &internalName)
{
   if (mStarred.count(internalName))
      mStarred.erase(internalName);
   else
      mStarred.insert(internalName);

   // Update entry
   for (auto &p : mPlugins)
      if (p.internalName == internalName)
         p.starred = mStarred.count(internalName) > 0;
}

bool PluginCategoryManager::IsStarred(const std::string &internalName) const
{
   return mStarred.count(internalName) > 0;
}

std::vector<PluginEntry> PluginCategoryManager::GetStarredPlugins() const
{
   std::vector<PluginEntry> result;
   for (const auto &name : mStarred)
   {
      auto it = std::find_if(mPlugins.begin(), mPlugins.end(),
         [&](const PluginEntry &p) { return p.internalName == name && p.visible; });
      if (it != mPlugins.end())
         result.push_back(*it);
   }
   return result;
}

// ─── Recent ────────────────────────────────────────────────────────────

void PluginCategoryManager::RecordUse(const std::string &internalName)
{
   // Remove existing entry (to re-add at front)
   mRecent.erase(
      std::remove(mRecent.begin(), mRecent.end(), internalName),
      mRecent.end()
   );
   // Add to front
   mRecent.insert(mRecent.begin(), internalName);
   // Keep max 25
   if (mRecent.size() > 25)
      mRecent.resize(25);
}

std::vector<PluginEntry> PluginCategoryManager::GetRecentPlugins(int maxCount) const
{
   std::vector<PluginEntry> result;
   int count = 0;
   for (const auto &name : mRecent)
   {
      if (count >= maxCount) break;
      auto it = std::find_if(mPlugins.begin(), mPlugins.end(),
         [&](const PluginEntry &p) { return p.internalName == name && p.visible; });
      if (it != mPlugins.end())
      {
         result.push_back(*it);
         count++;
      }
   }
   return result;
}

// ─── Search ────────────────────────────────────────────────────────────

static std::string toLower(const std::string &s)
{
   std::string result;
   result.reserve(s.size());
   for (char c : s)
      result.push_back(std::tolower(c));
   return result;
}

std::vector<PluginEntry> PluginCategoryManager::Search(const std::string &query) const
{
   if (query.empty())
      return GetAllPlugins();

   std::string lower = toLower(query);
   std::vector<PluginEntry> results;

   for (const auto &p : mPlugins)
   {
      if (!p.visible) continue;

      // Search in display name, internal name, category, and vendor
      std::string displayLower = toLower(p.displayName);
      std::string internalLower = toLower(p.internalName);
      std::string categoryLower = toLower(p.category);

      if (displayLower.find(lower) != std::string::npos ||
          internalLower.find(lower) != std::string::npos ||
          categoryLower.find(lower) != std::string::npos)
      {
         results.push_back(p);
      }
   }

   return results;
}

// ─── HGE Certified ─────────────────────────────────────────────────────

bool PluginCategoryManager::IsHgeCertified(const std::string &internalName) const
{
   auto it = std::find_if(mPlugins.begin(), mPlugins.end(),
      [&](const PluginEntry &p) { return p.internalName == internalName; });
   return it != mPlugins.end() && it->hgeCertified;
}

std::vector<PluginEntry> PluginCategoryManager::GetHgeCertifiedPlugins() const
{
   std::vector<PluginEntry> result;
   std::copy_if(mPlugins.begin(), mPlugins.end(), std::back_inserter(result),
      [](const PluginEntry &p) { return p.hgeCertified && p.visible; });
   return result;
}

// ─── Persistence (JSON-based) ─────────────────────────────────────────

void PluginCategoryManager::SaveState(const std::string &path)
{
   std::ofstream file(path);
   if (!file.is_open()) return;

   file << "{\n";
   file << "  \"starred\": [";
   bool first = true;
   for (const auto &s : mStarred)
   {
      if (!first) file << ", ";
      file << "\"" << s << "\"";
      first = false;
   }
   file << "],\n";

   file << "  \"recent\": [";
   first = true;
   for (const auto &r : mRecent)
   {
      if (!first) file << ", ";
      file << "\"" << r << "\"";
      first = false;
   }
   file << "]\n";
   file << "}\n";
}

void PluginCategoryManager::LoadState(const std::string &path)
{
   std::ifstream file(path);
   if (!file.is_open()) return;

   // Simple JSON parsing — just for starred and recent
   std::string content((std::istreambuf_iterator<char>(file)),
                       std::istreambuf_iterator<char>());

   // Parse "starred": ["a", "b"]
   auto starPos = content.find("\"starred\"");
   if (starPos != std::string::npos)
   {
      auto arrStart = content.find('[', starPos);
      auto arrEnd = content.find(']', arrStart);
      if (arrStart != std::string::npos && arrEnd != std::string::npos)
      {
         std::string arr = content.substr(arrStart + 1, arrEnd - arrStart - 1);
         std::string item;
         bool inStr = false;
         for (char c : arr)
         {
            if (c == '"') { inStr = !inStr; continue; }
            if (inStr) item += c;
            else if (!item.empty()) { mStarred.insert(item); item.clear(); }
         }
         if (!item.empty()) mStarred.insert(item);
      }
   }

   // Parse "recent": ["a", "b"]
   auto recentPos = content.find("\"recent\"");
   if (recentPos != std::string::npos)
   {
      auto arrStart = content.find('[', recentPos);
      auto arrEnd = content.find(']', arrStart);
      if (arrStart != std::string::npos && arrEnd != std::string::npos)
      {
         std::string arr = content.substr(arrStart + 1, arrEnd - arrStart - 1);
         std::string item;
         bool inStr = false;
         for (char c : arr)
         {
            if (c == '"') { inStr = !inStr; continue; }
            if (inStr) item += c;
            else if (!item.empty()) { mRecent.push_back(item); item.clear(); }
         }
         if (!item.empty()) mRecent.push_back(item);
      }
   }
}
