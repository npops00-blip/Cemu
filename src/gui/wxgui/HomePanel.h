#pragma once

#include <wx/panel.h>
#include <wx/timer.h>
#include <wx/image.h>
#include <wx/bitmap.h>
#include <functional>
#include <vector>

// Console-style home screen for a single game (Breath of the Wild).
// Shown in place of the game list; everything is custom painted.
class HomePanel : public wxPanel
{
public:
	struct Callbacks
	{
		std::function<void()> openGraphicPacks;
		std::function<void()> openSettings;
		std::function<void()> showGameList;
		std::function<void()> gameFound; // called once when BotW appears in the title list
	};

	HomePanel(wxWindow* parent, Callbacks callbacks);
	~HomePanel() override;

	// true if a supported game (BotW) is present in the title list
	bool HasGame() const { return m_titleId != 0; }
	void RefreshGame();

private:
	struct Button
	{
		wxString label;
		wxString key;
		wxRect rect;
		bool primary = false;
		std::function<void()> action;
	};

	void OnPaint(wxPaintEvent& event);
	void OnSize(wxSizeEvent& event);
	void OnMouseMove(wxMouseEvent& event);
	void OnMouseLeave(wxMouseEvent& event);
	void OnLeftUp(wxMouseEvent& event);
	void OnKeyDown(wxKeyEvent& event);
	void OnTimer(wxTimerEvent& event);
	void OnRightUp(wxMouseEvent& event);
	void OnPadTimer(wxTimerEvent& event);

	void LoadBackground();
	void ChooseBackground();
	void RemoveBackground();
	void RescaleBackground();

	void LayoutButtons();
	void Activate(int index);
	void LaunchGame();
	void OpenModManager();

	Callbacks m_callbacks;
	std::vector<Button> m_buttons;
	int m_hover = -1;
	int m_focus = 0;
	wxTimer m_timer;
	wxTimer m_padTimer;
	uint32 m_padPrev = 0;
	wxLongLong m_padRepeatAt = 0;

	uint64 m_titleId = 0;
	uint32 m_minutesPlayed = 0;
	wxString m_lastPlayed;
	wxString m_status;
	bool m_announced = false;

	wxImage m_bgImage;
	wxBitmap m_bgScaled;
	wxSize m_bgScaledFor;
};
