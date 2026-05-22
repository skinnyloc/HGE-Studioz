/**********************************************************************

 HGE Music Studio — Effect Module Implementation

 Registers:
   1. Effect → HGE Effect Browser menu item
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
 Location:    Effect menu (top HGE section)

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

#include "CommonCommandFlags.h"
#include "MenuRegistry.h"
#include "../../../libraries/lib-plugin-curation/PluginCategoryManager.h"
#include "../../../libraries/lib-plugin-display/PluginDisplayName.h"
#include "Project.h"
#include "ProjectWindow.h"

// Feature flag — set to 0 to disable the custom browser
#ifndef HGE_EFFECT_BROWSER
#define HGE_EFFECT_BROWSER 1
#endif

bool HgeEffectModule::sRegistered = false;

#if HGE_EFFECT_BROWSER
namespace {

void OnHgeEffectBrowser(const CommandContext &ctx)
{
   HgeEffectModule::OpenEffectBrowser(ctx);
}

using namespace MenuRegistry;

static AttachedItem sHgeEffectBrowserMenuItem{
   Section("HGE",
      Command(wxT("HgeEffectBrowser"),
         XXO("HGE Effect Browser...\tCtrl+Shift+E"),
         OnHgeEffectBrowser,
         AlwaysEnabledFlag,
         Options{}.LongName(XO("Open the categorized HGE effect browser")))),
   { wxT("Effect"), Registry::OrderingHint(Registry::OrderingHint::Begin) }
};

}
#endif

// ─── Startup / Shutdown ──────────────────────────────────────────────────

bool HgeEffectModule::OnStartup()
{
#if HGE_EFFECT_BROWSER
   if (sRegistered)
   {
      wxLogMessage("HGE EffectModule: Already registered, skipping");
      return true;
   }

   wxLogMessage("HGE EffectModule: Initializing effect browser systems...");

   PluginCategoryManager::Get().LoadBuiltinCategories();
   PluginDisplayName::Get().LoadBuiltinAliases();

   sRegistered = true;
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

   PluginCategoryManager::Get().SaveState();

   sRegistered = false;
}

// ─── Menu Handler ────────────────────────────────────────────────────────

void HgeEffectModule::OpenEffectBrowser(const CommandContext &ctx)
{
   wxWindow *parent = nullptr;

   // Try to get the active project window as parent
   auto *project = &ctx.project;
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
