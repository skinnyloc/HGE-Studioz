/**********************************************************************

 HGE Music Studio — Effect Module Registration

 Registers the HGE Effect Browser as a menu item under:
   Tools → HGE Effect Browser

 This module is loaded alongside the stock effect system.
 The stock Effects menu remains fully functional as fallback.

 Non-destructive integration:
   - Adds browser alongside existing menus
   - Feature flag: HGE_EFFECT_BROWSER (compile-time toggle)
   - Safe to remove: just disable the module

 **********************************************************************/

#ifndef __HGE_EFFECT_MODULE_H__
#define __HGE_EFFECT_MODULE_H__

#include <memory>

#include "ComponentInterface.h"

// -----------------------------------------------------------------------
// HgeEffectModule — Registers the custom effect browser
// -----------------------------------------------------------------------
class HgeEffectModule final : public ClientData
{
public:
   HgeEffectModule() = default;
   ~HgeEffectModule() override = default;

   // Called by the module system on load
   static bool OnStartup();
   static void OnShutdown();

   // Opens the HGE Effect Browser dialog
   static void OpenEffectBrowser(const struct CommandContext &ctx);

private:
   static bool sRegistered;
};

#endif // __HGE_EFFECT_MODULE_H__
