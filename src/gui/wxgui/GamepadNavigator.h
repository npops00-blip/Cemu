#pragma once

#include <wx/timer.h>

// Lets the gamepad drive Cemu's regular dialogs (settings, graphic packs, input settings, ...)
// by translating controller buttons into keyboard navigation for the active window:
//   D-pad          -> arrow keys        LB / RB -> Shift+Tab / Tab (previous / next control)
//   A              -> Space             Start   -> Enter
//   B              -> Escape
// The main window (home screen / game) is left alone; it handles the gamepad itself.
class GamepadNavigator : public wxEvtHandler
{
public:
	explicit GamepadNavigator(wxWindow* mainWindow);
	~GamepadNavigator() override;

private:
	void OnTimer(wxTimerEvent& event);

	wxWindow* m_mainWindow;
	wxTimer m_timer;
	uint32 m_prev = 0;
	uint32 m_repeatBit = 0;
	wxLongLong m_repeatAt = 0;
};
