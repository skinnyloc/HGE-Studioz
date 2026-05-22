/**********************************************************************

 HGE Music Studio — Effect Module Implementation

 Registers:
   1. Tools → HGE Effect Browser menu item
   2. Command handler to open the browser dialog
   3. Startup/shutdown lifecycle

 Feature flag: HGE_EFFECT_BROWSER
   Set to 1 (default) to enable the custom browser.
   Set to 0 to fall back to the stock Effects menu entirely.

 =========================================================================
 REGISTRATION DETAILS
 =========================================================================

 The module hooks into the app's MenuRegistry system to add a new
 command. The command is registered with a unique ID and label, and
 appears under the Tools menu.

 Command ID:  "HgeEffectBrowser"
 Menu label:  "HGE Effect Browser...\tCtrl+Shift+E"
 Location:    Tools menu (between existing items)

 =========================================================================
 SAFETY
 =========================================================================

 If the module fails to load:
   - The stock Effects menu is untouched
   - No crash on startup
   - The command simply won't appear
   - Remove mod-plugin-manager.so to restore stock behavior

 **********************************************************************/

#include "HgeEffectModule.h"
#include "HgeEffectBrowser.h"

#include <wx/log.h>
#include <wx/frame.h>
#include <wx/menu.h>

#include "MenuRegistry.h"
#include "CommandManager.h"
#include "EffectManager.h"
#include "Project.h"
#include "ProjectWindow.h"

// Feature flag — set to 0 to disable the custom browser
#ifndef HGE_EFFECT_BROWSER
#define HGE_EFFECT_BROWSER 1
#endif

bool HgeEffectModule::sRegistered = false;

// ─── Startup / Shutdown ──────────────────────────────────────────────────

bool HgeEffectModule::OnStartup()
{
#if HGE_EFFECT_BROWSER
   if (sRegistered)
   {
      wxLogMessage("HGE EffectModule: Already registered, skipping");
      return true;
   }

   wxLogMessage("HGE EffectModule: Registering effect browser...");

   // Register command
   CommandManager::Get().RegisterCommand(
      wxT("HgeEffectBrowser"),                           // ID
      XX("HGE Effect Browser...\tCtrl+Shift+E"),         // Label + shortcut
      XX("Open the categorized HGE effect browser"),     // Description
      wxT("Tools"),                                       // Menu path
      std::function<void(const CommandContext&)>(         // Handler
         &HgeEffectModule::OpenEffectBrowser
      ),
      wxT("HGE"),                                         // Category
      true                                                // Can use
   );

   sRegistered = true;
   wxLogMessage("HGE EffectModule: Registered successfully");

   // Initialize PluginCategoryManager and PluginDisplayName
   PluginCategoryManager::Get().LoadBuiltinCategories();
   PluginDisplayName::LoadBuiltinAliases();

   wxLogMessage("HGE EffectModule: Plugin systems initialized");
   return true;
#else
   wxLogMessage("HGE EffectModule: Disabled (HGE_EFFECT_BROWSER=0)");
   return true; // Still return true so module loads, just without browser
#endif
}

void HgeEffectModule::OnShutdown()
{
   wxLogMessage("HGE EffectModule: Shutting down");

   // Save plugin state (favorites, recent)
   wxString statePath = wxStandardPaths::Get().GetUserDataDir()
                       + wxFILE_SEP_PATH + wxT("plugin-state.json");
   PluginCategoryManager::Get().SaveState(statePath.ToStdString());

   sRegistered = false;
}

// ─── Menu Handler ────────────────────────────────────────────────────────

void HgeEffectModule::OpenEffectBrowser(const CommandContext &ctx)
{
   wxWindow *parent = nullptr;

   // Try to get the active project window as parent
   auto project = ctx.GetProject();
   if (project)
   {
      auto &projWindow = ProjectWindow::Get(*project);
      parent = &projWindow;
   }

   // Create and show the effect browser dialog
   HgeEffectBrowser browser(parent);
   browser.ShowModal();

   // Refresh the project window after closing browser
   if (project)
   {
      auto &projWindow = ProjectWindow::Get(*project);
      projWindow.Refresh();
   }
}
