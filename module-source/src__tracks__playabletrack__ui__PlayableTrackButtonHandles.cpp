/**********************************************************************

Audacity: A Digital Audio Editor

PlayableTrackButtonHandles.cpp

Paul Licameli split from TrackPanel.cpp

**********************************************************************/
#include "PlayableTrackButtonHandles.h"
#include "AudioIO.h"
#include "PlayableTrack.h"
#include "PlayableTrackControls.h"
#include "CommandManager.h"
#include "Project.h"
#include "ProjectHistory.h"
#include "../../../RefreshCode.h"
#include "../../../RealtimeEffectPanel.h"
#include "SampleTrack.h"
#include "TrackFocus.h"
#include "WaveTrack.h"
#include "../../ui/CommonTrackInfo.h"
#include "../../../TrackPanelMouseEvent.h"
#include "../../../TrackUtilities.h"

#include <wx/window.h>
#include <wx/log.h>

namespace
{
   std::weak_ptr<Track> gHgeArmedTrack;

   bool HgeRecordingActive()
   {
      const auto gAudioIO = AudioIO::Get();
      return gAudioIO &&
         gAudioIO->IsBusy() &&
         !gAudioIO->IsMonitoring() &&
         gAudioIO->GetNumCaptureChannels() > 0;
   }
}

bool HgeTrackArm::IsArmed(const Track *pTrack)
{
   auto armed = gHgeArmedTrack.lock();
   return armed && armed.get() == pTrack;
}

std::shared_ptr<Track> HgeTrackArm::GetArmedTrack()
{
   return gHgeArmedTrack.lock();
}

void HgeTrackArm::SetArmed(AudacityProject &project, const std::shared_ptr<Track> &pTrack)
{
   auto previous = gHgeArmedTrack.lock();
   gHgeArmedTrack = pTrack;
   if (pTrack) {
      wxLogDebug("HGE track armed current=%p previous=%p", pTrack.get(), previous.get());
      TrackFocus::Get(project).Set(pTrack.get(), true);
   }
   else
      wxLogDebug("HGE track arm cleared previous=%p", previous.get());
}

bool HgeTrackArm::SelectArmedTrackForRecording(AudacityProject &project)
{
   auto armed = gHgeArmedTrack.lock();
   auto waveTrack = dynamic_cast<WaveTrack *>(armed.get());
   if (!waveTrack) {
      if (armed)
         wxLogDebug("HGE record target unavailable armed=%p not a WaveTrack", armed.get());
      else
         wxLogDebug("HGE record target unavailable no armed track");
      return false;
   }

   auto &tracks = TrackList::Get(project);
   for (auto track : tracks)
      track->SetSelected(false);

   waveTrack->SetSelected(true);
   TrackFocus::Get(project).Set(waveTrack, true);
   ProjectHistory::Get(project).ModifyState(false);
   wxLogDebug("HGE recording target selected track=%p", waveTrack);
   return true;
}

MuteButtonHandle::MuteButtonHandle
( const std::shared_ptr<Track> &pTrack, const wxRect &rect )
   : ButtonHandle{ pTrack, rect }
{}

MuteButtonHandle::~MuteButtonHandle()
{
}

UIHandle::Result MuteButtonHandle::CommitChanges
   (const wxMouseEvent &event, AudacityProject *pProject, wxWindow *)
{
   auto pTrack = mpTrack.lock();
   if (dynamic_cast<PlayableTrack*>(pTrack.get()))
      TrackUtilities::DoTrackMute(*pProject, *pTrack, event.ShiftDown());

   return RefreshCode::RefreshNone;
}

TranslatableString MuteButtonHandle::Tip(
   const wxMouseState &, AudacityProject &project) const
{
   auto name = XO("Mute");
   auto focused =
      TrackFocus::Get( project ).Get() == GetTrack().get();
   if (!focused)
      return name;

   auto &commandManager = CommandManager::Get( project );
   ComponentInterfaceSymbol command{ wxT("TrackMute"), name };
   return commandManager.DescribeCommandsAndShortcuts(&command, 1u);
}

UIHandlePtr MuteButtonHandle::HitTest
(std::weak_ptr<MuteButtonHandle> &holder,
 const wxMouseState &state, const wxRect &rect,
 const AudacityProject *pProject, const std::shared_ptr<Track> &pTrack)
{
   wxRect buttonRect;
   if ( pTrack )
      PlayableTrackControls::GetMuteSoloRect(rect, buttonRect, false,
          pTrack.get());
   if ( CommonTrackInfo::HideTopItem( rect, buttonRect ) )
      return {};

   if ( pTrack && buttonRect.Contains(state.m_x, state.m_y) ) {
      auto result = std::make_shared<MuteButtonHandle>(pTrack, buttonRect);
      result = AssignUIHandlePtr(holder, result);
      return result;
   }
   else
      return {};
}

////////////////////////////////////////////////////////////////////////////////

ArmButtonHandle::ArmButtonHandle
( const std::shared_ptr<Track> &pTrack, const wxRect &rect )
   : ButtonHandle{ pTrack, rect }
{}

ArmButtonHandle::~ArmButtonHandle()
{
}

UIHandle::Result ArmButtonHandle::CommitChanges
(const wxMouseEvent &, AudacityProject *pProject, wxWindow *)
{
   auto pTrack = mpTrack.lock();
   if (pProject && dynamic_cast<PlayableTrack*>(pTrack.get())) {
      if (HgeRecordingActive()) {
         wxLogDebug("HGE arm click ignored while recording track=%p", pTrack.get());
         return RefreshCode::RefreshNone;
      }

      const auto armed = HgeTrackArm::GetArmedTrack();
      const bool wasArmed = armed && armed.get() == pTrack.get();
      wxLogDebug("HGE arm clicked track=%p wasArmed=%d", pTrack.get(), wasArmed);
      HgeTrackArm::SetArmed(*pProject, wasArmed ? std::shared_ptr<Track>{} : pTrack);
   }

   return RefreshCode::RefreshAll;
}

TranslatableString ArmButtonHandle::Tip(
   const wxMouseState &, AudacityProject &) const
{
   if (HgeRecordingActive())
      return XO("Stop recording before changing the armed track");

   return XO("Arm this track for recording");
}

UIHandlePtr ArmButtonHandle::HitTest
(std::weak_ptr<ArmButtonHandle> &holder,
 const wxMouseState &state, const wxRect &rect,
 const AudacityProject *, const std::shared_ptr<Track> &pTrack)
{
   wxRect buttonRect;
   if ( pTrack )
      PlayableTrackControls::GetArmButtonRect(rect, buttonRect, pTrack.get());

   if ( CommonTrackInfo::HideTopItem( rect, buttonRect ) )
      return {};

   if ( pTrack && buttonRect.Contains(state.m_x, state.m_y) ) {
      auto result = std::make_shared<ArmButtonHandle>( pTrack, buttonRect );
      result = AssignUIHandlePtr(holder, result);
      return result;
   }
   else
      return {};
}

////////////////////////////////////////////////////////////////////////////////

SoloButtonHandle::SoloButtonHandle
( const std::shared_ptr<Track> &pTrack, const wxRect &rect )
   : ButtonHandle{ pTrack, rect }
{}

SoloButtonHandle::~SoloButtonHandle()
{
}

UIHandle::Result SoloButtonHandle::CommitChanges
(const wxMouseEvent &event, AudacityProject *pProject, wxWindow *WXUNUSED(pParent))
{
   auto pTrack = mpTrack.lock();
   if (dynamic_cast<PlayableTrack*>(pTrack.get()))
      TrackUtilities::DoTrackSolo(*pProject, *pTrack, event.ShiftDown());

   return RefreshCode::RefreshNone;
}

TranslatableString SoloButtonHandle::Tip(
   const wxMouseState &, AudacityProject &project) const
{
   auto name = XO("Solo");
   auto focused =
      TrackFocus::Get( project ).Get() == GetTrack().get();
   if (!focused)
      return name;

   auto &commandManager = CommandManager::Get( project );
   ComponentInterfaceSymbol command{ wxT("TrackSolo"), name };
   return commandManager.DescribeCommandsAndShortcuts( &command, 1u );
}

UIHandlePtr SoloButtonHandle::HitTest
(std::weak_ptr<SoloButtonHandle> &holder,
 const wxMouseState &state, const wxRect &rect,
 const AudacityProject *pProject, const std::shared_ptr<Track> &pTrack)
{
   wxRect buttonRect;
   if ( pTrack )
      PlayableTrackControls::GetMuteSoloRect(rect, buttonRect, true,
          pTrack.get());

   if ( CommonTrackInfo::HideTopItem( rect, buttonRect ) )
      return {};

   if ( pTrack && buttonRect.Contains(state.m_x, state.m_y) ) {
      auto result = std::make_shared<SoloButtonHandle>( pTrack, buttonRect );
      result = AssignUIHandlePtr(holder, result);
      return result;
   }
   else
      return {};
}

////////////////////////////////////////////////////////////////////////////////

EffectsButtonHandle::EffectsButtonHandle
( const std::shared_ptr<Track> &pTrack, const wxRect &rect )
   : ButtonHandle{ pTrack, rect }
{}

EffectsButtonHandle::~EffectsButtonHandle()
{
}

UIHandle::Result EffectsButtonHandle::CommitChanges
(const wxMouseEvent &event, AudacityProject *pProject, wxWindow *pParent)
{
   RealtimeEffectPanel::Get(*pProject).ShowPanel(
      dynamic_cast<SampleTrack *>(mpTrack.lock().get()), true);
   return RefreshCode::RefreshNone;
}

TranslatableString EffectsButtonHandle::Tip(
   const wxMouseState &, AudacityProject &project) const
{
   auto name = XO("Effects");
   auto focused =
      TrackFocus::Get( project ).Get() == GetTrack().get();
   if (!focused)
      return name;
   else {
      return name;
   // Instead supply shortcut when "TrackEffects" is defined
   /*
   auto &commandManager = CommandManager::Get( project );
   ComponentInterfaceSymbol command{ wxT("TrackEffects"), name };
   return commandManager.DescribeCommandsAndShortcuts( &command, 1u );
    */
   }
}

UIHandlePtr EffectsButtonHandle::HitTest
(std::weak_ptr<EffectsButtonHandle> &holder,
 const wxMouseState &state, const wxRect &rect,
 const AudacityProject *pProject, const std::shared_ptr<Track> &pTrack)
{
   wxRect buttonRect;
   if ( pTrack )
      PlayableTrackControls::GetEffectsButtonRect(rect, buttonRect,
         pTrack.get());

   if ( CommonTrackInfo::HideTopItem( rect, buttonRect ) )
      return {};

   if ( pTrack && buttonRect.Contains(state.m_x, state.m_y) ) {
      auto result = std::make_shared<EffectsButtonHandle>( pTrack, buttonRect );
      result = AssignUIHandlePtr(holder, result);
      return result;
   }
   else
      return {};
}
