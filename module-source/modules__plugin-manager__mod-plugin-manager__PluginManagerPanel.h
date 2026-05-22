/**********************************************************************

 HGE Music Studio — Plugin Manager Panel

 Preferences panel for managing bundled plugins.
 Accessible via Preferences → Plugins.

 Features:
   - Lists all bundled plugins with status (active/quarantined/unknown)
   - Shows plugin type, version, architecture
   - Rescan all / rescan by format buttons
   - Quarantine management (view/release)
   - Plugin validation details
   - Log viewer for plugin loading issues

 **********************************************************************/

#ifndef __HGE_PLUGIN_MANAGER_PANEL_H__
#define __HGE_PLUGIN_MANAGER_PANEL_H__

#include "PluginManagerModule.h"
#include "PluginValidator.h"

#include <memory>
#include <vector>

#include <wx/panel.h>
#include <wx/listctrl.h>
#include <wx/button.h>
#include <wx/stattext.h>
#include <wx/textctrl.h>
#include <wx/choice.h>
#include <wx/checkbox.h>
#include <wx/notebook.h>

class ShuttleGui;

// -----------------------------------------------------------------------
// PluginManagerPanel — Main plugin management UI
// -----------------------------------------------------------------------
class PluginManagerPanel final : public wxPanel, public IPluginManagerUI
{
public:
   PluginManagerPanel(wxWindow *parent, wxWindowID id,
                      const wxPoint &pos = wxDefaultPosition,
                      const wxSize &size = wxDefaultSize);
   ~PluginManagerPanel() override;

   // IPluginManagerUI
   void UpdatePluginList() override;
   void ShowStatus(const wxString &msg) override;
   void ShowError(const wxString &msg) override;

   // Populate (called when panel is shown)
   void Populate();

private:
   // Event handlers
   void OnRescanAll(wxCommandEvent &evt);
   void OnRescanFormat(wxCommandEvent &evt);
   void OnPluginSelected(wxListEvent &evt);
   void OnPluginActivated(wxListEvent &evt);
   void OnUnquarantine(wxCommandEvent &evt);
   void OnClearQuarantine(wxCommandEvent &evt);
   void OnValidateSelected(wxCommandEvent &evt);
   void OnShowInFinder(wxCommandEvent &evt);

   // Helpers
   void BuildUi();
   void RefreshList();
   void UpdateDetails(const PluginBundle *bundle);
   void LogMessage(const wxString &msg);

   // Controls
   wxListCtrl       *mPluginList;
   wxStaticText     *mDetailName;
   wxStaticText     *mDetailType;
   wxStaticText     *mDetailArch;
   wxStaticText     *mDetailVersion;
   wxStaticText     *mDetailVendor;
   wxStaticText     *mDetailPath;
   wxStaticText     *mDetailStatus;
   wxTextCtrl       *mLogView;
   wxChoice         *mFormatFilter;
   wxButton         *mRescanAllBtn;
   wxButton         *mRescanFormatBtn;
   wxButton         *mUnquarantineBtn;
   wxButton         *mClearQuarantineBtn;
   wxButton         *mValidateBtn;
   wxButton         *mShowInFinderBtn;

   wxString          mSelectedPluginPath;

   DECLARE_EVENT_TABLE()
};

#endif // __HGE_PLUGIN_MANAGER_PANEL_H__
