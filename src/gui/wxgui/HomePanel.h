#pragma once

#include <wx/panel.h>
#include <wx/timer.h>
#include <wx/image.h>
#include <wx/bitmap.h>
#include <functional>
#include <vector>
#include <memory>
#include <string>
class wxGraphicsContext;
class GraphicPack2;

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
		std::function<void()> showHome; // back from the game list
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

	// gamepad-friendly graphic pack screen
	struct PackRow
	{
		std::shared_ptr<GraphicPack2> pack;
		std::string category; // empty for the pack row itself
		bool isPreset = false;
		wxRect rect;
	};
	enum class Mode { Home, Packs };
	Mode m_mode = Mode::Home;
	std::vector<PackRow> m_packRows;
	int m_packSel = 0;
	int m_packScroll = 0;
	bool m_packsDirty = false;
	void ShowPacks();
	void LeavePacks();
	void RebuildPackRows();
	void PaintPacks(wxGraphicsContext* gc, double w, double h, double left);
	void PacksMove(int delta);
	void PacksActivate();
	void PacksCycle(int dir);
	void SavePacks();
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
