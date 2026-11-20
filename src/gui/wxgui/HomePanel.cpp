#include "wxgui/wxgui.h"
#include "wxgui/HomePanel.h"
#include "wxgui/MainWindow.h"
#include "wxgui/wxHelper.h"

#include "Cafe/TitleList/TitleList.h"
#include "Cafe/IOSU/PDM/iosu_pdm.h"
#include "config/ActiveSettings.h"
#include "input/InputManager.h"
#include "Cafe/GraphicPack/GraphicPack2.h"
#include "config/CemuConfig.h"
#include "wxgui/wxCemuConfig.h"

#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <wx/utils.h>
#include <wx/msgdlg.h>
#include <wx/datetime.h>
#include <wx/filedlg.h>
#include <wx/menu.h>
#include <algorithm>
#include <wx/toplevel.h>
#include <wx/time.h>

namespace
{
	// Breath of the Wild base title ids (JPN / USA / EUR)
	constexpr uint64 kSupportedTitles[] = { 0x00050000101C9300ULL, 0x00050000101C9400ULL, 0x00050000101C9500ULL };

	const wxColour kAccent(127, 224, 196);
	const wxColour kText(238, 243, 241);
	const wxColour kMuted(169, 201, 192);
	const wxColour kCaption(184, 207, 201);

	wxString U8(const char* s) { return wxString::FromUTF8(s); }

	wxFont MakeFont(int pt, bool bold = false)
	{
		wxFontInfo info(pt);
		info.FaceName("Segoe UI");
		if (bold)
			info.Bold();
		return wxFont(info);
	}

	void RoundRect(wxGraphicsContext* gc, const wxRect& r, double radius)
	{
		gc->DrawRoundedRectangle(r.x, r.y, r.width, r.height, radius);
	}
}

HomePanel::HomePanel(wxWindow* parent, Callbacks callbacks)
	: wxPanel(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxWANTS_CHARS | wxFULL_REPAINT_ON_RESIZE),
	  m_callbacks(std::move(callbacks)), m_timer(this, wxWindow::NewControlId()), m_padTimer(this, wxWindow::NewControlId())
{
	SetBackgroundStyle(wxBG_STYLE_PAINT);

	m_buttons.push_back({ U8("\xD0\x98\xD0\xB3\xD1\x80\xD0\xB0\xD1\x82\xD1\x8C"), "Enter", {}, true, [this]() { LaunchGame(); } });
	m_buttons.push_back({ U8("\xD0\x93\xD1\x80\xD0\xB0\xD1\x84\xD0\xBF\xD0\xB0\xD0\xBA\xD0\xB8"), "G", {}, false, [this]() { ShowPacks(); } });
	m_buttons.push_back({ U8("\xD0\x9C\xD0\xBE\xD0\xB4\xD1\x8B (UKMM)"), "M", {}, false, [this]() { OpenModManager(); } });
	m_buttons.push_back({ U8("\xD0\x9D\xD0\xB0\xD1\x81\xD1\x82\xD1\x80\xD0\xBE\xD0\xB9\xD0\xBA\xD0\xB8"), "S", {}, false, [this]() { ShowSettings(); } });
	m_buttons.push_back({ U8("\xD0\xA1\xD0\xBF\xD0\xB8\xD1\x81\xD0\xBE\xD0\xBA \xD0\xB8\xD0\xB3\xD1\x80"), "L", {}, false, [this]() { if (m_callbacks.showGameList) m_callbacks.showGameList(); } });

	Bind(wxEVT_PAINT, &HomePanel::OnPaint, this);
	Bind(wxEVT_SIZE, &HomePanel::OnSize, this);
	Bind(wxEVT_MOTION, &HomePanel::OnMouseMove, this);
	Bind(wxEVT_LEAVE_WINDOW, &HomePanel::OnMouseLeave, this);
	Bind(wxEVT_LEFT_UP, &HomePanel::OnLeftUp, this);
	Bind(wxEVT_KEY_DOWN, &HomePanel::OnKeyDown, this);
	Bind(wxEVT_TIMER, &HomePanel::OnTimer, this, m_timer.GetId());
	Bind(wxEVT_TIMER, &HomePanel::OnPadTimer, this, m_padTimer.GetId());
	Bind(wxEVT_RIGHT_UP, &HomePanel::OnRightUp, this);
	Bind(wxEVT_MOUSEWHEEL, [this](wxMouseEvent& e) {
		const int steps = e.GetWheelRotation() / std::max(1, e.GetWheelDelta());
		if (m_mode == Mode::Settings)
		{
			SettingsMove(-steps);
			return;
		}
		if (m_mode != Mode::Packs || m_packRows.empty())
			return;
		m_packScroll = std::max(0, m_packScroll - steps * 3);
		m_packSel = std::clamp(m_packSel - steps * 3, 0, (int)m_packRows.size() - 1);
		Refresh(false);
	});

	LoadBackground();

	RefreshGame();
	m_timer.Start(2000);
	m_padTimer.Start(33);
}

HomePanel::~HomePanel()
{
	m_timer.Stop();
	m_padTimer.Stop();
}

void HomePanel::RefreshGame()
{
	uint64 found = 0;
	for (uint64 tid : kSupportedTitles)
	{
		uint16 version;
		if (CafeTitleList::HasTitle(tid, version))
		{
			found = tid;
			break;
		}
	}
	m_titleId = found;
	if (found && !m_announced)
	{
		m_announced = true;
		if (m_callbacks.gameFound)
			CallAfter([this]() { m_callbacks.gameFound(); });
	}
	m_minutesPlayed = 0;
	m_lastPlayed.clear();
	if (found)
	{
		iosu::pdm::GameListStat stat{};
		if (iosu::pdm::GetStatForGamelist(found, stat))
		{
			m_minutesPlayed = stat.numMinutesPlayed;
			if (stat.last_played.year != 0)
			{
				wxDateTime now = wxDateTime::Now();
				if ((uint32)now.GetYear() == stat.last_played.year && (uint32)now.GetMonth() + 1 == stat.last_played.month && (uint32)now.GetDay() == stat.last_played.day)
					m_lastPlayed = U8("\xD0\xA1\xD0\xB5\xD0\xB3\xD0\xBE\xD0\xB4\xD0\xBD\xD1\x8F");
				else
					m_lastPlayed = wxString::Format("%02u.%02u.%04u", stat.last_played.day, stat.last_played.month, stat.last_played.year);
			}
		}
	}
	Refresh(false);
}

void HomePanel::OnTimer(wxTimerEvent&)
{
	if (IsShownOnScreen() || !m_announced)
		RefreshGame();
}

void HomePanel::LayoutButtons()
{
	const wxSize sz = GetClientSize();
	const int left = std::max(40, sz.x / 18);
	int x = left;
	const int y = (int)(sz.y * 0.2) + 290;
	wxClientDC dc(this);
	dc.SetFont(MakeFont(14, true));
	for (auto& b : m_buttons)
	{
		const wxSize ts = dc.GetTextExtent(b.label);
		const int w = ts.x + (b.primary ? 72 : 56);
		b.rect = wxRect(x, y, w, 58);
		x += w + 16;
	}
}

void HomePanel::OnSize(wxSizeEvent& event)
{
	LayoutButtons();
	Refresh(false);
	event.Skip();
}

void HomePanel::OnPaint(wxPaintEvent&)
{
	wxAutoBufferedPaintDC dc(this);
	std::unique_ptr<wxGraphicsContext> gc(wxGraphicsContext::Create(dc));
	if (!gc)
		return;
	const wxSize sz = GetClientSize();
	const double w = sz.x, h = sz.y;
	if (m_buttons.front().rect.IsEmpty())
		LayoutButtons();

	// custom background image (cover) with darkening for readability
	if (m_bgImage.IsOk())
	{
		if (m_bgScaledFor != sz)
			RescaleBackground();
		if (m_bgScaled.IsOk())
			gc->DrawBitmap(m_bgScaled, 0, 0, w, h);
		gc->SetPen(*wxTRANSPARENT_PEN);
		gc->SetBrush(gc->CreateLinearGradientBrush(0, 0, w, 0, wxColour(6, 14, 16, 235), wxColour(6, 14, 16, 40)));
		gc->DrawRectangle(0, 0, w, h);
		gc->SetBrush(gc->CreateLinearGradientBrush(0, h * 0.55, 0, h, wxColour(6, 14, 16, 0), wxColour(6, 14, 16, 200)));
		gc->DrawRectangle(0, h * 0.55, w, h * 0.45);
	}
	else
	{
	// background
	gc->SetPen(*wxTRANSPARENT_PEN);
	gc->SetBrush(gc->CreateLinearGradientBrush(0, 0, w * 0.6, h, wxColour(14, 29, 34), wxColour(7, 11, 13)));
	gc->DrawRectangle(0, 0, w, h);
	gc->SetBrush(gc->CreateRadialGradientBrush(w * 0.78, h * 0.3, w * 0.78, h * 0.3, w * 0.55, wxColour(64, 170, 150, 110), wxColour(64, 170, 150, 0)));
	gc->DrawRectangle(0, 0, w, h);
	gc->SetBrush(gc->CreateRadialGradientBrush(w * 0.2, h * 0.95, w * 0.2, h * 0.95, w * 0.4, wxColour(40, 90, 140, 90), wxColour(40, 90, 140, 0)));
	gc->DrawRectangle(0, 0, w, h);
	// moon
	const double mr = h * 0.08;
	gc->SetBrush(gc->CreateRadialGradientBrush(w * 0.62, h * 0.24, w * 0.62, h * 0.24, mr * 1.6, wxColour(234, 247, 227, 170), wxColour(234, 247, 227, 0)));
	gc->DrawEllipse(w * 0.62 - mr * 1.6, h * 0.24 - mr * 1.6, mr * 3.2, mr * 3.2);
	// hills
	const struct { double base, amp; wxColour col; } hills[] = {
		{0.66, 0.07, wxColour(18, 49, 47, 210)}, {0.76, 0.05, wxColour(13, 35, 33, 220)}, {0.86, 0.03, wxColour(8, 21, 20, 235)} };
	for (const auto& hl : hills)
	{
		wxGraphicsPath p = gc->CreatePath();
		p.MoveToPoint(0, h * hl.base);
		p.AddCurveToPoint(w * 0.15, h * (hl.base - hl.amp), w * 0.3, h * (hl.base + hl.amp * 0.4), w * 0.5, h * (hl.base - hl.amp * 0.5));
		p.AddCurveToPoint(w * 0.7, h * (hl.base - hl.amp * 1.2), w * 0.85, h * (hl.base + hl.amp * 0.3), w, h * (hl.base - hl.amp * 0.6));
		p.AddLineToPoint(w, h);
		p.AddLineToPoint(0, h);
		p.CloseSubpath();
		gc->SetBrush(wxBrush(hl.col));
		gc->FillPath(p);
	}
	}

	const double left = std::max(40.0, w / 18);
	// brand
	gc->SetFont(MakeFont(10, true), wxColour(159, 217, 201));
	gc->DrawText(U8("C E M U   \xC2\xB7   B O T W"), left, 28);

	if (m_mode == Mode::Packs)
	{
		PaintPacks(gc.get(), w, h, left);
		return;
	}
	if (m_mode == Mode::Settings)
	{
		PaintSettings(gc.get(), w, h, left);
		return;
	}

	if (!m_titleId)
	{
		gc->SetFont(MakeFont(20, true), kText);
		gc->DrawText(U8("Breath of the Wild \xD0\xBD\xD0\xB5 \xD0\xBD\xD0\xB0\xD0\xB9\xD0\xB4\xD0\xB5\xD0\xBD\xD0\xB0"), left, h * 0.3);
		gc->SetFont(MakeFont(12), kMuted);
		gc->DrawText(U8("\xD0\x94\xD0\xBE\xD0\xB1\xD0\xB0\xD0\xB2\xD1\x8C \xD0\xBF\xD1\x83\xD1\x82\xD1\x8C \xD0\xBA \xD0\xB8\xD0\xB3\xD1\x80\xD0\xB5 \xD0\xB2 \xD0\xBD\xD0\xB0\xD1\x81\xD1\x82\xD1\x80\xD0\xBE\xD0\xB9\xD0\xBA\xD0\xB0\xD1\x85 \xD0\xB8\xD0\xBB\xD0\xB8 \xD0\xBE\xD1\x82\xD0\xBA\xD1\x80\xD0\xBE\xD0\xB9 \xC2\xAB\xD0\xA1\xD0\xBF\xD0\xB8\xD1\x81\xD0\xBE\xD0\xBA \xD0\xB8\xD0\xB3\xD1\x80\xC2\xBB."), left, h * 0.3 + 50);
	}
	else
	{
		double y = h * 0.2;
		// tag
		gc->SetFont(MakeFont(9, true), kAccent);
		wxString tag = U8("\xD0\x93\xD0\x9E\xD0\xA2\xD0\x9E\xD0\x92\xD0\x9E \xD0\x9A \xD0\x97\xD0\x90\xD0\x9F\xD0\xA3\xD0\xA1\xD0\x9A\xD0\xA3");
		double tw, th;
		gc->GetTextExtent(tag, &tw, &th);
		gc->SetPen(wxPen(wxColour(127, 224, 196, 120), 1));
		gc->SetBrush(*wxTRANSPARENT_BRUSH);
		gc->DrawRoundedRectangle(left, y, tw + 26, th + 12, (th + 12) / 2);
		gc->DrawText(tag, left + 13, y + 6);
		y += th + 34;
		gc->SetFont(MakeFont(20), kMuted);
		gc->DrawText("The Legend of Zelda", left, y);
		y += 42;
		gc->SetFont(MakeFont(48, true), kText);
		gc->DrawText("Breath of the Wild", left, y);
		gc->GetTextExtent("Breath of the Wild", &tw, &th);
		y += th + 18;
		// stats
		const wxString playtime = m_minutesPlayed ? wxString::Format(U8("%u \xD1\x87 %02u \xD0\xBC\xD0\xB8\xD0\xBD"), m_minutesPlayed / 60, m_minutesPlayed % 60) : U8("\xE2\x80\x94");
		const wxString caps[] = { U8("\xD0\x9D\xD0\xB0\xD0\xB8\xD0\xB3\xD1\x80\xD0\xB0\xD0\xBD\xD0\xBE"), U8("\xD0\x9F\xD0\xBE\xD1\x81\xD0\xBB\xD0\xB5\xD0\xB4\xD0\xBD\xD0\xB8\xD0\xB9 \xD0\xB7\xD0\xB0\xD0\xBF\xD1\x83\xD1\x81\xD0\xBA") };
		const wxString vals[] = { playtime, m_lastPlayed.empty() ? U8("\xE2\x80\x94") : m_lastPlayed };
		double sx = left;
		for (int i = 0; i < 2; i++)
		{
			gc->SetFont(MakeFont(11), kCaption);
			gc->DrawText(caps[i], sx, y);
			gc->SetFont(MakeFont(18, true), kText);
			gc->DrawText(vals[i], sx, y + 22);
			double cw, ch, vw, vh;
			gc->SetFont(MakeFont(11), kCaption);
			gc->GetTextExtent(caps[i], &cw, &ch);
			gc->SetFont(MakeFont(18, true), kText);
			gc->GetTextExtent(vals[i], &vw, &vh);
			sx += std::max(cw, vw) + 48;
		}
	}

	// buttons
	for (size_t i = 0; i < m_buttons.size(); i++)
	{
		const auto& b = m_buttons[i];
		if (!m_titleId && b.primary)
			continue;
		const bool hot = (int)i == m_hover || ((int)i == m_focus && HasFocus());
		if (b.primary)
		{
			gc->SetPen(*wxTRANSPARENT_PEN);
			gc->SetBrush(wxBrush(hot ? wxColour(154, 240, 215) : kAccent));
		}
		else
		{
			gc->SetPen(wxPen(hot ? kAccent : wxColour(255, 255, 255, 40), hot ? 2 : 1));
			gc->SetBrush(wxBrush(wxColour(255, 255, 255, hot ? 30 : 20)));
		}
		RoundRect(gc.get(), b.rect, 14);
		gc->SetFont(MakeFont(14, true), b.primary ? wxColour(6, 35, 28) : kText);
		double tw, th;
		gc->GetTextExtent(b.label, &tw, &th);
		gc->DrawText(b.label, b.rect.x + (b.rect.width - tw) / 2, b.rect.y + (b.rect.height - th) / 2);
	}

	// bottom bar
	const double barH = 56;
	gc->SetPen(*wxTRANSPARENT_PEN);
	gc->SetBrush(wxBrush(wxColour(5, 10, 12, 200)));
	gc->DrawRectangle(0, h - barH, w, barH);
	gc->SetFont(MakeFont(10), kCaption);
	wxString hint = U8("Enter / A \xE2\x80\x94 \xD0\xB8\xD0\xB3\xD1\x80\xD0\xB0\xD1\x82\xD1\x8C     \xE2\x86\x90 \xE2\x86\x92 \xE2\x80\x94 \xD0\xB2\xD1\x8B\xD0\xB1\xD0\xBE\xD1\x80     \xD0\x9F\xD0\x9A\xD0\x9C \xE2\x80\x94 \xD1\x81\xD0\xBC\xD0\xB5\xD0\xBD\xD0\xB8\xD1\x82\xD1\x8C \xD1\x84\xD0\xBE\xD0\xBD");
	double hw, hh;
	gc->GetTextExtent(hint, &hw, &hh);
	gc->DrawText(hint, w - left - hw, h - barH + (barH - hh) / 2);
	if (!m_status.empty())
		gc->DrawText(m_status, left, h - barH + (barH - hh) / 2);
}

void HomePanel::OnMouseMove(wxMouseEvent& event)
{
	if (m_mode != Mode::Home)
		return;
	int hover = -1;
	for (size_t i = 0; i < m_buttons.size(); i++)
		if (m_buttons[i].rect.Contains(event.GetPosition()))
			hover = (int)i;
	if (hover != m_hover)
	{
		m_hover = hover;
		SetCursor(hover >= 0 ? wxCursor(wxCURSOR_HAND) : wxNullCursor);
		Refresh(false);
	}
}

void HomePanel::OnMouseLeave(wxMouseEvent&)
{
	if (m_hover != -1)
	{
		m_hover = -1;
		Refresh(false);
	}
}

void HomePanel::OnLeftUp(wxMouseEvent& event)
{
	SetFocus();
	if (m_mode == Mode::Settings)
	{
		for (size_t i = 0; i < m_setRows.size(); i++)
		{
			const auto& r = m_setRows[i];
			if (r.kind == SettingRow::Header || !r.rect.Contains(event.GetPosition()))
				continue;
			m_setSel = (int)i;
			if (r.kind == SettingRow::Choice)
				SettingsCycle(event.GetPosition().x > r.rect.GetX() + r.rect.GetWidth() / 2 ? 1 : -1);
			else
				SettingsActivate();
			return;
		}
		return;
	}
	if (m_mode == Mode::Packs)
	{
		for (size_t i = 0; i < m_packRows.size(); i++)
		{
			if (m_packRows[i].rect.Contains(event.GetPosition()))
			{
				m_packSel = (int)i;
				if (m_packRows[i].isPreset)
					PacksCycle(event.GetPosition().x > m_packRows[i].rect.GetX() + m_packRows[i].rect.GetWidth() / 2 ? 1 : -1);
				else
					PacksActivate();
				return;
			}
		}
		return;
	}
	for (size_t i = 0; i < m_buttons.size(); i++)
		if (m_buttons[i].rect.Contains(event.GetPosition()))
			Activate((int)i);
}

void HomePanel::OnKeyDown(wxKeyEvent& event)
{
	if (m_mode == Mode::Settings)
	{
		switch (event.GetKeyCode())
		{
		case WXK_UP: SettingsMove(-1); return;
		case WXK_DOWN: SettingsMove(1); return;
		case WXK_LEFT: SettingsCycle(-1); return;
		case WXK_RIGHT: SettingsCycle(1); return;
		case WXK_RETURN: case WXK_NUMPAD_ENTER: case WXK_SPACE: SettingsActivate(); return;
		case WXK_ESCAPE: case WXK_BACK: LeaveSettings(); return;
		default: event.Skip(); return;
		}
	}
	if (m_mode == Mode::Packs)
	{
		switch (event.GetKeyCode())
		{
		case WXK_UP: PacksMove(-1); return;
		case WXK_DOWN: PacksMove(1); return;
		case WXK_LEFT: PacksCycle(-1); return;
		case WXK_RIGHT: PacksCycle(1); return;
		case WXK_RETURN: case WXK_NUMPAD_ENTER: case WXK_SPACE: PacksActivate(); return;
		case WXK_ESCAPE: case WXK_BACK: LeavePacks(); return;
		default: event.Skip(); return;
		}
	}
	switch (event.GetKeyCode())
	{
	case WXK_LEFT:
		m_focus = (m_focus + (int)m_buttons.size() - 1) % (int)m_buttons.size();
		Refresh(false);
		return;
	case WXK_RIGHT:
	case WXK_TAB:
		m_focus = (m_focus + 1) % (int)m_buttons.size();
		Refresh(false);
		return;
	case WXK_RETURN:
	case WXK_NUMPAD_ENTER:
	case WXK_SPACE:
		Activate(m_focus);
		return;
	case 'G': Activate(1); return;
	case 'M': Activate(2); return;
	case 'S': Activate(3); return;
	case 'L': Activate(4); return;
	default:
		event.Skip();
	}
}

void HomePanel::Activate(int index)
{
	if (index < 0 || index >= (int)m_buttons.size())
		return;
	m_focus = index;
	if (m_buttons[index].action)
		m_buttons[index].action();
}

void HomePanel::LaunchGame()
{
	RefreshGame();
	if (!m_titleId)
		return;
	TitleInfo titleInfo;
	if (!CafeTitleList::GetFirstByTitleId(m_titleId, titleInfo) || !titleInfo.IsValid())
	{
		wxMessageBox(U8("\xD0\x9D\xD0\xB5 \xD1\x83\xD0\xB4\xD0\xB0\xD0\xBB\xD0\xBE\xD1\x81\xD1\x8C \xD0\xBD\xD0\xB0\xD0\xB9\xD1\x82\xD0\xB8 \xD1\x84\xD0\xB0\xD0\xB9\xD0\xBB\xD1\x8B \xD0\xB8\xD0\xB3\xD1\x80\xD1\x8B."), _("Error"), wxOK | wxICON_ERROR, this);
		return;
	}
	MainWindow::RequestLaunchGame(titleInfo.GetPath(), wxLaunchGameEvent::INITIATED_BY::GAME_LIST);
}

void HomePanel::OpenModManager()
{
	std::error_code ec;
	const fs::path exeDir = ActiveSettings::GetDataPath();
	const fs::path candidates[] = { exeDir / "UKMM" / "ukmm.exe", exeDir / "ukmm" / "ukmm.exe", exeDir / "ukmm.exe" };
	for (const auto& p : candidates)
	{
		if (fs::exists(p, ec))
		{
			wxExecute(wxString::Format("\"%s\"", wxHelper::FromPath(p)), wxEXEC_ASYNC);
			m_status = U8("UKMM \xD0\xBE\xD1\x82\xD0\xBA\xD1\x80\xD1\x8B\xD1\x82. \xD0\x9F\xD0\xBE\xD1\x81\xD0\xBB\xD0\xB5 Apply \xD0\xBC\xD0\xBE\xD0\xB4\xD1\x8B \xD0\xBF\xD1\x80\xD0\xB8\xD0\xBC\xD0\xB5\xD0\xBD\xD1\x8F\xD1\x82\xD1\x81\xD1\x8F \xD0\xBF\xD1\x80\xD0\xB8 \xD1\x81\xD0\xBB\xD0\xB5\xD0\xB4\xD1\x83\xD1\x8E\xD1\x89\xD0\xB5\xD0\xBC \xD0\xB7\xD0\xB0\xD0\xBF\xD1\x83\xD1\x81\xD0\xBA\xD0\xB5 \xD0\xB8\xD0\xB3\xD1\x80\xD1\x8B.");
			Refresh(false);
			return;
		}
	}
	wxMessageBox(U8("ukmm.exe \xD0\xBD\xD0\xB5 \xD0\xBD\xD0\xB0\xD0\xB9\xD0\xB4\xD0\xB5\xD0\xBD. \xD0\x9F\xD0\xBE\xD0\xBB\xD0\xBE\xD0\xB6\xD0\xB8 UKMM \xD0\xB2 \xD0\xBF\xD0\xB0\xD0\xBF\xD0\xBA\xD1\x83 UKMM \xD1\x80\xD1\x8F\xD0\xB4\xD0\xBE\xD0\xBC \xD1\x81 Cemu.exe."), _("Error"), wxOK | wxICON_WARNING, this);
}

namespace
{
	const char* kBgExts[] = { "png", "jpg", "jpeg", "bmp" };

	fs::path BackgroundFile(const char* ext)
	{
		return ActiveSettings::GetUserDataPath(fmt::format("home_background.{}", ext));
	}
}

void HomePanel::LoadBackground()
{
	m_bgImage = wxImage();
	m_bgScaled = wxBitmap();
	m_bgScaledFor = wxDefaultSize;
	std::error_code ec;
	for (const char* ext : kBgExts)
	{
		const fs::path p = BackgroundFile(ext);
		if (fs::exists(p, ec))
		{
			wxImage img;
			if (img.LoadFile(wxHelper::FromPath(p)))
			{
				m_bgImage = img;
				break;
			}
		}
	}
	Refresh(false);
}

void HomePanel::RescaleBackground()
{
	const wxSize sz = GetClientSize();
	m_bgScaledFor = sz;
	if (!m_bgImage.IsOk() || sz.x <= 0 || sz.y <= 0)
		return;
	const double iw = m_bgImage.GetWidth(), ih = m_bgImage.GetHeight();
	const double scale = std::max(sz.x / iw, sz.y / ih);
	const int cw = std::max(1, (int)(sz.x / scale)), ch = std::max(1, (int)(sz.y / scale));
	const int cx = std::max(0, (int)((iw - cw) / 2)), cy = std::max(0, (int)((ih - ch) / 2));
	wxImage crop = m_bgImage.GetSubImage(wxRect(cx, cy, std::min(cw, (int)iw - cx), std::min(ch, (int)ih - cy)));
	crop.Rescale(sz.x, sz.y, wxIMAGE_QUALITY_HIGH);
	m_bgScaled = wxBitmap(crop);
}

void HomePanel::OnRightUp(wxMouseEvent&)
{
	wxMenu menu;
	const int idChoose = wxWindow::NewControlId(), idRemove = wxWindow::NewControlId();
	menu.Append(idChoose, U8("\xD0\x92\xD1\x8B\xD0\xB1\xD1\x80\xD0\xB0\xD1\x82\xD1\x8C \xD1\x84\xD0\xBE\xD0\xBD\xE2\x80\xA6"));
	menu.Append(idRemove, U8("\xD0\xA1\xD1\x82\xD0\xB0\xD0\xBD\xD0\xB4\xD0\xB0\xD1\x80\xD1\x82\xD0\xBD\xD1\x8B\xD0\xB9 \xD1\x84\xD0\xBE\xD0\xBD"))->Enable(m_bgImage.IsOk());
	const int sel = GetPopupMenuSelectionFromUser(menu);
	if (sel == idChoose)
		ChooseBackground();
	else if (sel == idRemove)
		RemoveBackground();
}

void HomePanel::ChooseBackground()
{
	wxFileDialog dlg(this, U8("\xD0\xA4\xD0\xBE\xD0\xBD \xD0\xB4\xD0\xBE\xD0\xBC\xD0\xB0\xD1\x88\xD0\xBD\xD0\xB5\xD0\xB3\xD0\xBE \xD1\x8D\xD0\xBA\xD1\x80\xD0\xB0\xD0\xBD\xD0\xB0"), wxEmptyString, wxEmptyString,
		"Images (*.png;*.jpg;*.jpeg;*.bmp)|*.png;*.jpg;*.jpeg;*.bmp", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
	if (dlg.ShowModal() != wxID_OK)
		return;
	const fs::path src = wxHelper::MakeFSPath(dlg.GetPath());
	std::string ext = src.extension().string();
	if (!ext.empty() && ext[0] == '.')
		ext = ext.substr(1);
	std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });
	if (ext != "png" && ext != "jpg" && ext != "jpeg" && ext != "bmp")
		return;
	std::error_code ec;
	for (const char* e : kBgExts)
		fs::remove(BackgroundFile(e), ec);
	fs::copy_file(src, BackgroundFile(ext.c_str()), fs::copy_options::overwrite_existing, ec);
	if (ec)
	{
		wxMessageBox(wxString::FromUTF8(ec.message()), _("Error"), wxOK | wxICON_ERROR, this);
		return;
	}
	LoadBackground();
}

void HomePanel::RemoveBackground()
{
	std::error_code ec;
	for (const char* e : kBgExts)
		fs::remove(BackgroundFile(e), ec);
	LoadBackground();
}

// gamepad navigation (home: left/right + A, Start = play; graphic packs: up/down, left/right, A, B)
void HomePanel::OnPadTimer(wxTimerEvent&)
{
	auto* top = dynamic_cast<wxTopLevelWindow*>(wxGetTopLevelParent(this));
	if (!top || !top->IsActive())
	{
		m_padPrev = ~0u; // ignore buttons still held when the window becomes active again
		return;
	}
	// game list is shown instead of the home screen: B goes back
	const bool gameListMode = !IsShown() && GetParent() && GetParent()->IsShownOnScreen();
	if (!IsShownOnScreen() && !gameListMode)
		return;
	enum : uint32 { kLeft = 1, kRight = 2, kA = 4, kStart = 8, kUp = 16, kDown = 32, kB = 64 };
	uint32 state = 0;
	auto& im = InputManager::instance();
	for (size_t i = 0; i < InputManager::kMaxController; i++)
	{
		const auto controller = im.get_controller(i);
		if (!controller)
			continue;
		controller->controllers_update_states();
		const auto axis = controller->get_axis();
		if (controller->is_left_down() || axis.x < -0.6f) state |= kLeft;
		if (controller->is_right_down() || axis.x > 0.6f) state |= kRight;
		if (controller->is_up_down() || axis.y > 0.6f) state |= kUp;
		if (controller->is_down_down() || axis.y < -0.6f) state |= kDown;
		if (controller->is_a_down()) state |= kA;
		if (controller->is_b_down()) state |= kB;
		if (controller->is_start_down()) state |= kStart;
	}
	const uint32 pressed = state & ~m_padPrev;
	m_padPrev = state;
	const wxLongLong now = wxGetLocalTimeMillis();

	if (gameListMode)
	{
		if ((pressed & kB) && m_callbacks.showHome)
			m_callbacks.showHome();
		return;
	}

	// direction with auto-repeat: -1/+1 horizontal, -2/+2 vertical
	int dir = 0;
	auto dirOf = [](uint32 bits) { return (bits & kLeft) ? -1 : (bits & kRight) ? 1 : (bits & kUp) ? -2 : (bits & kDown) ? 2 : 0; };
	if (const int d = dirOf(pressed))
	{
		dir = d;
		m_padRepeatAt = now + 400;
	}
	else if (dirOf(state) && now >= m_padRepeatAt)
	{
		dir = dirOf(state);
		m_padRepeatAt = now + 130;
	}

	if (m_mode == Mode::Settings)
	{
		SetFocus();
		if (dir == -2 || dir == 2)
			SettingsMove(dir / 2);
		else if (dir)
			SettingsCycle(dir);
		if (pressed & kA)
		{
			m_padPrev = ~0u; // the full settings dialog may open; ignore held buttons afterwards
			SettingsActivate();
		}
		else if (pressed & kB)
			LeaveSettings();
		return;
	}
	if (m_mode == Mode::Packs)
	{
		SetFocus();
		if (dir == -2 || dir == 2)
			PacksMove(dir / 2);
		else if (dir)
			PacksCycle(dir);
		if (pressed & kA)
			PacksActivate();
		else if (pressed & kB)
		{
			m_padPrev = state;
			LeavePacks();
		}
		return;
	}

	if (dir == -1 || dir == 1)
	{
		const int n = (int)m_buttons.size();
		m_focus = (m_focus + dir + n) % n;
		if (!m_titleId && m_focus == 0)
			m_focus = (m_focus + dir + n) % n;
		m_hover = -1;
		SetFocus();
		Refresh(false);
	}
	if (pressed & kStart)
		LaunchGame();
	else if (pressed & kA)
		Activate(m_focus);
}

// ---------------------------------------------------------------- graphic packs screen

void HomePanel::ShowPacks()
{
	m_mode = Mode::Packs;
	m_packSel = 0;
	m_packScroll = 0;
	m_packsDirty = false;
	m_hover = -1;
	SetCursor(wxNullCursor);
	RebuildPackRows();
	SetFocus();
	Refresh(false);
}

void HomePanel::LeavePacks()
{
	if (m_packsDirty)
		SavePacks();
	m_mode = Mode::Home;
	m_status = m_packsDirty ? U8("\xD0\x93\xD1\x80\xD0\xB0\xD1\x84\xD0\xBF\xD0\xB0\xD0\xBA\xD0\xB8 \xD1\x81\xD0\xBE\xD1\x85\xD1\x80\xD0\xB0\xD0\xBD\xD0\xB5\xD0\xBD\xD1\x8B. \xD0\x98\xD0\xB7\xD0\xBC\xD0\xB5\xD0\xBD\xD0\xB5\xD0\xBD\xD0\xB8\xD1\x8F \xD0\xBF\xD1\x80\xD0\xB8\xD0\xBC\xD0\xB5\xD0\xBD\xD1\x8F\xD1\x82\xD1\x81\xD1\x8F \xD0\xBF\xD1\x80\xD0\xB8 \xD0\xB7\xD0\xB0\xD0\xBF\xD1\x83\xD1\x81\xD0\xBA\xD0\xB5 \xD0\xB8\xD0\xB3\xD1\x80\xD1\x8B.") : wxString();
	m_packsDirty = false;
	Refresh(false);
}

void HomePanel::RebuildPackRows()
{
	std::string selPath, selCat;
	bool selPreset = false;
	if (m_packSel >= 0 && m_packSel < (int)m_packRows.size())
	{
		selPath = m_packRows[m_packSel].pack->GetNormalizedPathString();
		selCat = m_packRows[m_packSel].category;
		selPreset = m_packRows[m_packSel].isPreset;
	}
	m_packRows.clear();
	std::vector<std::shared_ptr<GraphicPack2>> packs;
	for (const auto& gp : GraphicPack2::GetGraphicPacks())
		if (m_titleId && gp->ContainsTitleId(m_titleId))
			packs.push_back(gp);
	std::sort(packs.begin(), packs.end(), [](const auto& a, const auto& b) { return a->GetVirtualPath() < b->GetVirtualPath(); });
	for (const auto& gp : packs)
	{
		m_packRows.push_back({ gp, "", false, {} });
		if (!gp->IsEnabled())
			continue;
		std::vector<std::string> order;
		const auto cats = gp->GetCategorizedPresets(order);
		for (const auto& cat : order)
		{
			const auto it = cats.find(cat);
			if (it == cats.end())
				continue;
			const bool anyVisible = std::any_of(it->second.begin(), it->second.end(), [](const auto& p) { return p->visible; });
			if (anyVisible)
				m_packRows.push_back({ gp, cat, true, {} });
		}
	}
	m_packSel = 0;
	for (size_t i = 0; i < m_packRows.size(); i++)
	{
		if (m_packRows[i].pack->GetNormalizedPathString() == selPath && m_packRows[i].category == selCat && m_packRows[i].isPreset == selPreset)
		{
			m_packSel = (int)i;
			break;
		}
	}
}

void HomePanel::PacksMove(int delta)
{
	if (m_packRows.empty())
		return;
	m_packSel = std::clamp(m_packSel + delta, 0, (int)m_packRows.size() - 1);
	Refresh(false);
}

void HomePanel::PacksActivate()
{
	if (m_packSel < 0 || m_packSel >= (int)m_packRows.size())
		return;
	auto& row = m_packRows[m_packSel];
	if (row.isPreset)
	{
		PacksCycle(1);
		return;
	}
	row.pack->SetEnabled(!row.pack->IsEnabled());
	m_packsDirty = true;
	RebuildPackRows();
	Refresh(false);
}

void HomePanel::PacksCycle(int dir)
{
	if (m_packSel < 0 || m_packSel >= (int)m_packRows.size())
		return;
	auto& row = m_packRows[m_packSel];
	if (!row.isPreset)
	{
		// left = off, right = on for the pack row itself
		const bool want = dir > 0;
		if (row.pack->IsEnabled() != want)
			PacksActivate();
		return;
	}
	std::vector<std::string> order;
	const auto cats = row.pack->GetCategorizedPresets(order);
	const auto it = cats.find(row.category);
	if (it == cats.end())
		return;
	std::vector<std::string> names;
	for (const auto& p : it->second)
		if (p->visible)
			names.push_back(p->name);
	if (names.empty())
		return;
	const std::string current = row.pack->GetActivePreset(row.category);
	int idx = 0;
	for (size_t i = 0; i < names.size(); i++)
		if (names[i] == current)
			idx = (int)i;
	idx = (idx + dir + (int)names.size()) % (int)names.size();
	row.pack->SetActivePreset(row.category, names[idx]);
	m_packsDirty = true;
	RebuildPackRows(); // visibility of other categories may depend on this preset
	Refresh(false);
}

void HomePanel::SavePacks()
{
	auto& data = GetConfigHandle().data();
	data.graphic_pack_entries.clear();
	for (const auto& gp : GraphicPack2::GetGraphicPacks())
	{
		auto filename = _utf8ToPath(gp->GetNormalizedPathString());
		if (gp->IsEnabled())
		{
			data.graphic_pack_entries.try_emplace(filename);
			auto& it = data.graphic_pack_entries[filename];
			for (const auto& preset : gp->GetActivePresets())
				it.try_emplace(preset->category, preset->name);
		}
		else if (gp->IsDefaultEnabled())
		{
			data.graphic_pack_entries.try_emplace(filename);
			auto& it = data.graphic_pack_entries[filename];
			it.try_emplace("_disabled", "false");
		}
	}
	GetConfigHandle().Save();
}

void HomePanel::PaintPacks(wxGraphicsContext* gc, double w, double h, double left)
{
	// darker veil for readability
	gc->SetPen(*wxTRANSPARENT_PEN);
	gc->SetBrush(wxBrush(wxColour(6, 14, 16, 150)));
	gc->DrawRectangle(0, 0, w, h);

	double y = 70;
	gc->SetFont(MakeFont(26, true), kText);
	gc->DrawText(U8("\xD0\x93\xD1\x80\xD0\xB0\xD1\x84\xD0\xB8\xD1\x87\xD0\xB5\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB5 \xD0\xBF\xD0\xB0\xD0\xBA\xD0\xB5\xD1\x82\xD1\x8B"), left, y);
	y += 52;
	gc->SetFont(MakeFont(11), kCaption);
	gc->DrawText(U8("\xD0\x98\xD0\xB7\xD0\xBC\xD0\xB5\xD0\xBD\xD0\xB5\xD0\xBD\xD0\xB8\xD1\x8F \xD0\xBF\xD1\x80\xD0\xB8\xD0\xBC\xD0\xB5\xD0\xBD\xD1\x8F\xD1\x8E\xD1\x82\xD1\x81\xD1\x8F \xD0\xBF\xD1\x80\xD0\xB8 \xD1\x81\xD0\xBB\xD0\xB5\xD0\xB4\xD1\x83\xD1\x8E\xD1\x89\xD0\xB5\xD0\xBC \xD0\xB7\xD0\xB0\xD0\xBF\xD1\x83\xD1\x81\xD0\xBA\xD0\xB5 \xD0\xB8\xD0\xB3\xD1\x80\xD1\x8B."), left, y);
	y += 36;

	const double barH = 56;
	const double rowH = 46, gap = 6;
	const double listTop = y, listBottom = h - barH - 20;
	const int visibleRows = std::max(1, (int)((listBottom - listTop) / (rowH + gap)));
	if (m_packSel < m_packScroll)
		m_packScroll = m_packSel;
	if (m_packSel >= m_packScroll + visibleRows)
		m_packScroll = m_packSel - visibleRows + 1;
	m_packScroll = std::clamp(m_packScroll, 0, std::max(0, (int)m_packRows.size() - visibleRows));

	const double listW = std::min(w - left * 2, 1100.0);
	for (auto& r : m_packRows)
		r.rect = wxRect();
	if (m_packRows.empty())
	{
		gc->SetFont(MakeFont(14), kMuted);
		gc->DrawText(U8("\xD0\x93\xD1\x80\xD0\xB0\xD1\x84\xD0\xBF\xD0\xB0\xD0\xBA\xD0\xB8 \xD0\xB4\xD0\xBB\xD1\x8F Breath of the Wild \xD0\xBD\xD0\xB5 \xD0\xBD\xD0\xB0\xD0\xB9\xD0\xB4\xD0\xB5\xD0\xBD\xD1\x8B."), left, y);
	}
	for (int i = m_packScroll; i < (int)m_packRows.size() && i < m_packScroll + visibleRows; i++)
	{
		auto& r = m_packRows[i];
		const double indent = r.isPreset ? 40 : 0;
		const wxRect rect((int)(left + indent), (int)y, (int)(listW - indent), (int)rowH);
		r.rect = rect;
		const bool sel = i == m_packSel;
		gc->SetPen(sel ? wxPen(kAccent, 2) : *wxTRANSPARENT_PEN);
		gc->SetBrush(wxBrush(sel ? wxColour(31, 74, 66, 230) : wxColour(16, 30, 33, r.isPreset ? 170 : 210)));
		RoundRect(gc, rect, 10);
		const double ty = rect.y + rowH / 2;
		if (!r.isPreset)
		{
			// switch
			const bool on = r.pack->IsEnabled();
			const double sx = rect.x + 16, sy = ty - 11;
			gc->SetPen(*wxTRANSPARENT_PEN);
			gc->SetBrush(wxBrush(on ? kAccent : wxColour(255, 255, 255, 50)));
			gc->DrawRoundedRectangle(sx, sy, 40, 22, 11);
			gc->SetBrush(wxBrush(on ? wxColour(6, 35, 28) : wxColour(207, 227, 221)));
			gc->DrawEllipse(on ? sx + 21 : sx + 3, sy + 3, 16, 16);
			std::string name = r.pack->GetVirtualPath();
			const auto slash = name.find('/');
			if (slash != std::string::npos && name.substr(0, slash).find("Breath of the Wild") != std::string::npos)
				name = name.substr(slash + 1);
			wxString label = wxString::FromUTF8(name);
			label.Replace("/", U8(" \xE2\x80\xBA "));
			gc->SetFont(MakeFont(13, true), on ? kText : kMuted);
			double tw, th;
			gc->GetTextExtent(label, &tw, &th);
			gc->DrawText(label, sx + 56, ty - th / 2);
		}
		else
		{
			gc->SetFont(MakeFont(12), kCaption);
			const wxString cat = r.category.empty() ? U8("\xD0\x9F\xD1\x80\xD0\xB5\xD1\x81\xD0\xB5\xD1\x82") : wxString::FromUTF8(r.category);
			double tw, th;
			gc->GetTextExtent(cat, &tw, &th);
			gc->DrawText(cat, rect.x + 18, ty - th / 2);
			const wxString val = U8("\xE2\x80\xB9  ") + wxString::FromUTF8(r.pack->GetActivePreset(r.category)) + U8("  \xE2\x80\xBA");
			gc->SetFont(MakeFont(12, true), sel ? kAccent : kText);
			gc->GetTextExtent(val, &tw, &th);
			gc->DrawText(val, rect.x + rect.width - 18 - tw, ty - th / 2);
		}
		y += rowH + gap;
	}

	// bottom bar
	gc->SetPen(*wxTRANSPARENT_PEN);
	gc->SetBrush(wxBrush(wxColour(5, 10, 12, 200)));
	gc->DrawRectangle(0, h - barH, w, barH);
	gc->SetFont(MakeFont(10), kCaption);
	const wxString hint = U8("\xE2\x86\x91 \xE2\x86\x93 \xE2\x80\x94 \xD0\xB2\xD1\x8B\xD0\xB1\xD0\xBE\xD1\x80     \xE2\x86\x90 \xE2\x86\x92 \xE2\x80\x94 \xD0\xBF\xD1\x80\xD0\xB5\xD1\x81\xD0\xB5\xD1\x82 / \xD0\xB2\xD0\xBA\xD0\xBB-\xD0\xB2\xD1\x8B\xD0\xBA\xD0\xBB     A / Enter \xE2\x80\x94 \xD0\xBF\xD0\xB5\xD1\x80\xD0\xB5\xD0\xBA\xD0\xBB\xD1\x8E\xD1\x87\xD0\xB8\xD1\x82\xD1\x8C     B / Esc \xE2\x80\x94 \xD0\xBD\xD0\xB0\xD0\xB7\xD0\xB0\xD0\xB4");
	double hw, hh;
	gc->GetTextExtent(hint, &hw, &hh);
	gc->DrawText(hint, w - left - hw, h - barH + (barH - hh) / 2);
}

// ---------------------------------------------------------------- settings screen

namespace
{
	std::vector<wxString> PositionNames()
	{
		return { U8("\xD0\x92\xD1\x8B\xD0\xBA\xD0\xBB"), U8("\xD0\xA1\xD0\xB2\xD0\xB5\xD1\x80\xD1\x85\xD1\x83 \xD1\x81\xD0\xBB\xD0\xB5\xD0\xB2\xD0\xB0"), U8("\xD0\xA1\xD0\xB2\xD0\xB5\xD1\x80\xD1\x85\xD1\x83 \xD0\xBF\xD0\xBE \xD1\x86\xD0\xB5\xD0\xBD\xD1\x82\xD1\x80\xD1\x83"), U8("\xD0\xA1\xD0\xB2\xD0\xB5\xD1\x80\xD1\x85\xD1\x83 \xD1\x81\xD0\xBF\xD1\x80\xD0\xB0\xD0\xB2\xD0\xB0"), U8("\xD0\xA1\xD0\xBD\xD0\xB8\xD0\xB7\xD1\x83 \xD1\x81\xD0\xBB\xD0\xB5\xD0\xB2\xD0\xB0"), U8("\xD0\xA1\xD0\xBD\xD0\xB8\xD0\xB7\xD1\x83 \xD0\xBF\xD0\xBE \xD1\x86\xD0\xB5\xD0\xBD\xD1\x82\xD1\x80\xD1\x83"), U8("\xD0\xA1\xD0\xBD\xD0\xB8\xD0\xB7\xD1\x83 \xD1\x81\xD0\xBF\xD1\x80\xD0\xB0\xD0\xB2\xD0\xB0") };
	}
	std::vector<wxString> FilterNames()
	{
		return { U8("\xD0\x91\xD0\xB8\xD0\xBB\xD0\xB8\xD0\xBD\xD0\xB5\xD0\xB9\xD0\xBD\xD1\x8B\xD0\xB9"), U8("\xD0\x91\xD0\xB8\xD0\xBA\xD1\x83\xD0\xB1\xD0\xB8\xD1\x87\xD0\xB5\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9"), U8("\xD0\x91\xD0\xB8\xD0\xBA\xD1\x83\xD0\xB1\xD0\xB8\xD1\x87\xD0\xB5\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9 (\xD0\xAD\xD1\x80\xD0\xBC\xD0\xB8\xD1\x82)"), U8("\xD0\x91\xD0\xBB\xD0\xB8\xD0\xB6\xD0\xB0\xD0\xB9\xD1\x88\xD0\xB8\xD0\xB9 \xD1\x81\xD0\xBE\xD1\x81\xD0\xB5\xD0\xB4") };
	}
	std::vector<wxString> VolumeNames()
	{
		std::vector<wxString> v;
		for (int i = 0; i <= 100; i += 5)
			v.push_back(wxString::Format("%d%%", i));
		return v;
	}
}

void HomePanel::ShowSettings()
{
	m_mode = Mode::Settings;
	m_setSel = 0;
	m_setScroll = 0;
	m_setDirty = false;
	m_hover = -1;
	SetCursor(wxNullCursor);
	BuildSettingRows();
	SettingsMove(0);
	SetFocus();
	Refresh(false);
}

void HomePanel::LeaveSettings()
{
	if (m_setDirty)
		GetConfigHandle().Save();
	m_mode = Mode::Home;
	m_status = m_setDirty ? U8("\xD0\x9D\xD0\xB0\xD1\x81\xD1\x82\xD1\x80\xD0\xBE\xD0\xB9\xD0\xBA\xD0\xB8 \xD1\x81\xD0\xBE\xD1\x85\xD1\x80\xD0\xB0\xD0\xBD\xD0\xB5\xD0\xBD\xD1\x8B.") : wxString();
	m_setDirty = false;
	Refresh(false);
}

void HomePanel::BuildSettingRows()
{
	m_setRows.clear();
	auto header = [this](const wxString& label) {
		SettingRow r;
		r.kind = SettingRow::Header;
		r.label = label;
		m_setRows.push_back(std::move(r));
	};
	auto toggle = [this](const wxString& label, std::function<bool()> get, std::function<void(bool)> set, const wxString& hint = {}) {
		SettingRow r;
		r.kind = SettingRow::Toggle;
		r.label = label;
		r.hint = hint;
		r.getBool = std::move(get);
		r.setBool = std::move(set);
		m_setRows.push_back(std::move(r));
	};
	auto choice = [this](const wxString& label, std::vector<wxString> options, std::function<int()> get, std::function<void(int)> set, bool wrap = true, const wxString& hint = {}) {
		SettingRow r;
		r.kind = SettingRow::Choice;
		r.label = label;
		r.hint = hint;
		r.options = std::move(options);
		r.getIndex = std::move(get);
		r.setIndex = std::move(set);
		r.wrap = wrap;
		m_setRows.push_back(std::move(r));
	};

	auto& cfg = GetConfig();
	const bool vulkan = cfg.graphic_api.GetValue() == GraphicAPI::kVulkan;

	header(U8("\xD0\x93\xD1\x80\xD0\xB0\xD1\x84\xD0\xB8\xD0\xBA\xD0\xB0"));
	choice(U8("\xD0\x93\xD1\x80\xD0\xB0\xD1\x84\xD0\xB8\xD1\x87\xD0\xB5\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9 API"), { "OpenGL", "Vulkan" },
		[]() { return GetConfig().graphic_api.GetValue() == GraphicAPI::kVulkan ? 1 : 0; },
		[this](int i) {
			GetConfig().graphic_api = i == 1 ? GraphicAPI::kVulkan : GraphicAPI::kOpenGL;
			if (i == 0 && GetConfig().vsync.GetValue() > 1)
				GetConfig().vsync = 1;
			CallAfter([this]() { BuildSettingRows(); Refresh(false); });
		}, true, U8("\xD0\x9F\xD1\x80\xD0\xB8\xD0\xBC\xD0\xB5\xD0\xBD\xD0\xB8\xD1\x82\xD1\x81\xD1\x8F \xD0\xBF\xD1\x80\xD0\xB8 \xD1\x81\xD0\xBB\xD0\xB5\xD0\xB4\xD1\x83\xD1\x8E\xD1\x89\xD0\xB5\xD0\xBC \xD0\xB7\xD0\xB0\xD0\xBF\xD1\x83\xD1\x81\xD0\xBA\xD0\xB5 \xD0\xB8\xD0\xB3\xD1\x80\xD1\x8B"));
	if (vulkan)
		choice(U8("\xD0\x92\xD0\xB5\xD1\x80\xD1\x82\xD0\xB8\xD0\xBA\xD0\xB0\xD0\xBB\xD1\x8C\xD0\xBD\xD0\xB0\xD1\x8F \xD1\x81\xD0\xB8\xD0\xBD\xD1\x85\xD1\x80\xD0\xBE\xD0\xBD\xD0\xB8\xD0\xB7\xD0\xB0\xD1\x86\xD0\xB8\xD1\x8F"), { U8("\xD0\x92\xD1\x8B\xD0\xBA\xD0\xBB"), U8("\xD0\x94\xD0\xB2\xD0\xBE\xD0\xB9\xD0\xBD\xD0\xB0\xD1\x8F \xD0\xB1\xD1\x83\xD1\x84\xD0\xB5\xD1\x80\xD0\xB8\xD0\xB7\xD0\xB0\xD1\x86\xD0\xB8\xD1\x8F"), U8("\xD0\xA2\xD1\x80\xD0\xBE\xD0\xB9\xD0\xBD\xD0\xB0\xD1\x8F \xD0\xB1\xD1\x83\xD1\x84\xD0\xB5\xD1\x80\xD0\xB8\xD0\xB7\xD0\xB0\xD1\x86\xD0\xB8\xD1\x8F"), U8("\xD0\x9A\xD0\xB0\xD0\xBA \xD1\x83 \xD1\x8D\xD0\xBC\xD1\x83\xD0\xBB\xD0\xB8\xD1\x80\xD1\x83\xD0\xB5\xD0\xBC\xD0\xBE\xD0\xB3\xD0\xBE \xD1\x8D\xD0\xBA\xD1\x80\xD0\xB0\xD0\xBD\xD0\xB0") },
			[]() { return std::clamp(GetConfig().vsync.GetValue(), 0, 3); }, [](int i) { GetConfig().vsync = i; });
	else
		choice(U8("\xD0\x92\xD0\xB5\xD1\x80\xD1\x82\xD0\xB8\xD0\xBA\xD0\xB0\xD0\xBB\xD1\x8C\xD0\xBD\xD0\xB0\xD1\x8F \xD1\x81\xD0\xB8\xD0\xBD\xD1\x85\xD1\x80\xD0\xBE\xD0\xBD\xD0\xB8\xD0\xB7\xD0\xB0\xD1\x86\xD0\xB8\xD1\x8F"), { U8("\xD0\x92\xD1\x8B\xD0\xBA\xD0\xBB"), U8("\xD0\x92\xD0\xBA\xD0\xBB") },
			[]() { return GetConfig().vsync.GetValue() ? 1 : 0; }, [](int i) { GetConfig().vsync = i; });
	if (vulkan)
		toggle(U8("\xD0\x90\xD1\x81\xD0\xB8\xD0\xBD\xD1\x85\xD1\x80\xD0\xBE\xD0\xBD\xD0\xBD\xD0\xB0\xD1\x8F \xD0\xBA\xD0\xBE\xD0\xBC\xD0\xBF\xD0\xB8\xD0\xBB\xD1\x8F\xD1\x86\xD0\xB8\xD1\x8F \xD1\x88\xD0\xB5\xD0\xB9\xD0\xB4\xD0\xB5\xD1\x80\xD0\xBE\xD0\xB2"), []() { return GetConfig().async_compile.GetValue(); }, [](bool v) { GetConfig().async_compile = v; },
			U8("\xD0\x9C\xD0\xB5\xD0\xBD\xD1\x8C\xD1\x88\xD0\xB5 \xD0\xBF\xD0\xBE\xD0\xB4\xD1\x82\xD0\xBE\xD1\x80\xD0\xBC\xD0\xB0\xD0\xB6\xD0\xB8\xD0\xB2\xD0\xB0\xD0\xBD\xD0\xB8\xD0\xB9 \xD0\xBF\xD1\x80\xD0\xB8 \xD0\xBF\xD0\xBE\xD1\x8F\xD0\xB2\xD0\xBB\xD0\xB5\xD0\xBD\xD0\xB8\xD0\xB8 \xD0\xBD\xD0\xBE\xD0\xB2\xD1\x8B\xD1\x85 \xD1\x8D\xD1\x84\xD1\x84\xD0\xB5\xD0\xBA\xD1\x82\xD0\xBE\xD0\xB2"));
	choice(U8("\xD0\xA4\xD0\xB8\xD0\xBB\xD1\x8C\xD1\x82\xD1\x80 \xD1\x83\xD0\xB2\xD0\xB5\xD0\xBB\xD0\xB8\xD1\x87\xD0\xB5\xD0\xBD\xD0\xB8\xD1\x8F"), FilterNames(), []() { return std::clamp((int)GetConfig().upscale_filter.GetValue(), 0, 3); }, [](int i) { GetConfig().upscale_filter = i; });
	choice(U8("\xD0\xA4\xD0\xB8\xD0\xBB\xD1\x8C\xD1\x82\xD1\x80 \xD1\x83\xD0\xBC\xD0\xB5\xD0\xBD\xD1\x8C\xD1\x88\xD0\xB5\xD0\xBD\xD0\xB8\xD1\x8F"), FilterNames(), []() { return std::clamp((int)GetConfig().downscale_filter.GetValue(), 0, 3); }, [](int i) { GetConfig().downscale_filter = i; });
	choice(U8("\xD0\x9C\xD0\xB0\xD1\x81\xD1\x88\xD1\x82\xD0\xB0\xD0\xB1 \xD0\xBD\xD0\xB0 \xD0\xB2\xD0\xB5\xD1\x81\xD1\x8C \xD1\x8D\xD0\xBA\xD1\x80\xD0\xB0\xD0\xBD"), { U8("\xD0\xA1\xD0\xBE\xD1\x85\xD1\x80\xD0\xB0\xD0\xBD\xD1\x8F\xD1\x82\xD1\x8C \xD0\xBF\xD1\x80\xD0\xBE\xD0\xBF\xD0\xBE\xD1\x80\xD1\x86\xD0\xB8\xD0\xB8"), U8("\xD0\xA0\xD0\xB0\xD1\x81\xD1\x82\xD1\x8F\xD0\xBD\xD1\x83\xD1\x82\xD1\x8C") },
		[]() { return GetConfig().fullscreen_scaling.GetValue() == kStretch ? 1 : 0; }, [](int i) { GetConfig().fullscreen_scaling = i == 1 ? kStretch : kKeepAspectRatio; });
	toggle(U8("\xD0\x97\xD0\xB0\xD0\xBF\xD1\x83\xD1\x81\xD0\xBA\xD0\xB0\xD1\x82\xD1\x8C \xD0\xB8\xD0\xB3\xD1\x80\xD1\x83 \xD0\xBD\xD0\xB0 \xD0\xB2\xD0\xB5\xD1\x81\xD1\x8C \xD1\x8D\xD0\xBA\xD1\x80\xD0\xB0\xD0\xBD"), []() { return GetWxGUIConfig().fullscreen.GetValue(); }, [](bool v) { GetWxGUIConfig().fullscreen = v; });

	header(U8("\xD0\x97\xD0\xB2\xD1\x83\xD0\xBA"));
	choice(U8("\xD0\x93\xD1\x80\xD0\xBE\xD0\xBC\xD0\xBA\xD0\xBE\xD1\x81\xD1\x82\xD1\x8C (\xD0\xA2\xD0\x92)"), VolumeNames(), []() { return std::clamp(GetConfig().tv_volume / 5, 0, 20); }, [](int i) { GetConfig().tv_volume = i * 5; }, false);
	choice(U8("\xD0\x93\xD1\x80\xD0\xBE\xD0\xBC\xD0\xBA\xD0\xBE\xD1\x81\xD1\x82\xD1\x8C (\xD0\xB3\xD0\xB5\xD0\xB9\xD0\xBC\xD0\xBF\xD0\xB0\xD0\xB4)"), VolumeNames(), []() { return std::clamp(GetConfig().pad_volume / 5, 0, 20); }, [](int i) { GetConfig().pad_volume = i * 5; }, false);

	header(U8("\xD0\xAD\xD0\xBA\xD1\x80\xD0\xB0\xD0\xBD\xD0\xBD\xD1\x8B\xD0\xB9 \xD0\xBE\xD0\xB2\xD0\xB5\xD1\x80\xD0\xBB\xD0\xB5\xD0\xB9"));
	choice(U8("\xD0\x9F\xD0\xBE\xD0\xBB\xD0\xBE\xD0\xB6\xD0\xB5\xD0\xBD\xD0\xB8\xD0\xB5 \xD0\xBE\xD0\xB2\xD0\xB5\xD1\x80\xD0\xBB\xD0\xB5\xD1\x8F"), PositionNames(), []() { return std::clamp((int)GetConfig().overlay.position, 0, 6); }, [](int i) { GetConfig().overlay.position = (ScreenPosition)i; });
	toggle(U8("\xD0\x9F\xD0\xBE\xD0\xBA\xD0\xB0\xD0\xB7\xD1\x8B\xD0\xB2\xD0\xB0\xD1\x82\xD1\x8C FPS"), []() { return GetConfig().overlay.fps; }, [](bool v) { GetConfig().overlay.fps = v; });
	toggle(U8("\xD0\x97\xD0\xB0\xD0\xB3\xD1\x80\xD1\x83\xD0\xB7\xD0\xBA\xD0\xB0 \xD0\xBF\xD1\x80\xD0\xBE\xD1\x86\xD0\xB5\xD1\x81\xD1\x81\xD0\xBE\xD1\x80\xD0\xB0"), []() { return GetConfig().overlay.cpu_usage; }, [](bool v) { GetConfig().overlay.cpu_usage = v; });
	toggle(U8("\xD0\x98\xD1\x81\xD0\xBF\xD0\xBE\xD0\xBB\xD1\x8C\xD0\xB7\xD0\xBE\xD0\xB2\xD0\xB0\xD0\xBD\xD0\xB8\xD0\xB5 RAM"), []() { return GetConfig().overlay.ram_usage; }, [](bool v) { GetConfig().overlay.ram_usage = v; });
	toggle(U8("\xD0\x98\xD1\x81\xD0\xBF\xD0\xBE\xD0\xBB\xD1\x8C\xD0\xB7\xD0\xBE\xD0\xB2\xD0\xB0\xD0\xBD\xD0\xB8\xD0\xB5 \xD0\xB2\xD0\xB8\xD0\xB4\xD0\xB5\xD0\xBE\xD0\xBF\xD0\xB0\xD0\xBC\xD1\x8F\xD1\x82\xD0\xB8"), []() { return GetConfig().overlay.vram_usage; }, [](bool v) { GetConfig().overlay.vram_usage = v; });
	choice(U8("\xD0\x9F\xD0\xBE\xD0\xBB\xD0\xBE\xD0\xB6\xD0\xB5\xD0\xBD\xD0\xB8\xD0\xB5 \xD1\x83\xD0\xB2\xD0\xB5\xD0\xB4\xD0\xBE\xD0\xBC\xD0\xBB\xD0\xB5\xD0\xBD\xD0\xB8\xD0\xB9"), PositionNames(), []() { return std::clamp((int)GetConfig().notification.position, 0, 6); }, [](int i) { GetConfig().notification.position = (ScreenPosition)i; });

	header(U8("\xD0\xA1\xD0\xB8\xD1\x81\xD1\x82\xD0\xB5\xD0\xBC\xD0\xB0"));
	{
		static const CPUMode modes[] = { CPUMode::Auto, CPUMode::MulticoreRecompiler, CPUMode::SinglecoreRecompiler, CPUMode::SinglecoreInterpreter };
		choice(U8("\xD0\xA0\xD0\xB5\xD0\xB6\xD0\xB8\xD0\xBC \xD0\xBF\xD1\x80\xD0\xBE\xD1\x86\xD0\xB5\xD1\x81\xD1\x81\xD0\xBE\xD1\x80\xD0\xB0"), { U8("\xD0\x90\xD0\xB2\xD1\x82\xD0\xBE"), U8("\xD0\x9C\xD0\xBD\xD0\xBE\xD0\xB3\xD0\xBE\xD1\x8F\xD0\xB4\xD0\xB5\xD1\x80\xD0\xBD\xD1\x8B\xD0\xB9 \xD1\x80\xD0\xB5\xD0\xBA\xD0\xBE\xD0\xBC\xD0\xBF\xD0\xB8\xD0\xBB\xD1\x8F\xD1\x82\xD0\xBE\xD1\x80"), U8("\xD0\x9E\xD0\xB4\xD0\xBD\xD0\xBE\xD1\x8F\xD0\xB4\xD0\xB5\xD1\x80\xD0\xBD\xD1\x8B\xD0\xB9 \xD1\x80\xD0\xB5\xD0\xBA\xD0\xBE\xD0\xBC\xD0\xBF\xD0\xB8\xD0\xBB\xD1\x8F\xD1\x82\xD0\xBE\xD1\x80"), U8("\xD0\x9E\xD0\xB4\xD0\xBD\xD0\xBE\xD1\x8F\xD0\xB4\xD0\xB5\xD1\x80\xD0\xBD\xD1\x8B\xD0\xB9 \xD0\xB8\xD0\xBD\xD1\x82\xD0\xB5\xD1\x80\xD0\xBF\xD1\x80\xD0\xB5\xD1\x82\xD0\xB0\xD1\x82\xD0\xBE\xD1\x80") },
			[]() {
				const CPUMode m = GetConfig().cpu_mode.GetValue();
				for (int i = 0; i < 4; i++)
					if (modes[i] == m)
						return i;
				return 0;
			},
			[](int i) { GetConfig().cpu_mode = modes[std::clamp(i, 0, 3)]; });
	}
	toggle(U8("\xD0\x97\xD0\xB2\xD1\x83\xD0\xBA \xD0\xBF\xD1\x80\xD0\xB8 \xD0\xB7\xD0\xB0\xD0\xBF\xD1\x83\xD1\x81\xD0\xBA\xD0\xB5 \xD0\xB8\xD0\xB3\xD1\x80\xD1\x8B"), []() { return GetConfig().play_boot_sound.GetValue(); }, [](bool v) { GetConfig().play_boot_sound = v; });
	toggle(U8("\xD0\xA1\xD0\xBE\xD1\x85\xD1\x80\xD0\xB0\xD0\xBD\xD1\x8F\xD1\x82\xD1\x8C \xD1\x81\xD0\xBA\xD1\x80\xD0\xB8\xD0\xBD\xD1\x88\xD0\xBE\xD1\x82\xD1\x8B \xD0\xB2 \xD1\x84\xD0\xB0\xD0\xB9\xD0\xBB"), []() { return GetWxGUIConfig().save_screenshot.GetValue(); }, [](bool v) { GetWxGUIConfig().save_screenshot = v; });

	{
		SettingRow r;
		r.kind = SettingRow::Action;
		r.label = U8("\xD0\x92\xD1\x81\xD0\xB5 \xD0\xBD\xD0\xB0\xD1\x81\xD1\x82\xD1\x80\xD0\xBE\xD0\xB9\xD0\xBA\xD0\xB8 Cemu\xE2\x80\xA6");
		r.hint = U8("\xD0\xA1\xD1\x82\xD0\xB0\xD0\xBD\xD0\xB4\xD0\xB0\xD1\x80\xD1\x82\xD0\xBD\xD0\xBE\xD0\xB5 \xD0\xBE\xD0\xBA\xD0\xBD\xD0\xBE \xD1\x81\xD0\xBE \xD0\xB2\xD1\x81\xD0\xB5\xD0\xBC\xD0\xB8 \xD0\xBF\xD0\xB0\xD1\x80\xD0\xB0\xD0\xBC\xD0\xB5\xD1\x82\xD1\x80\xD0\xB0\xD0\xBC\xD0\xB8");
		r.action = [this]() {
			if (m_setDirty)
				GetConfigHandle().Save();
			if (m_callbacks.openSettings)
				m_callbacks.openSettings();
			BuildSettingRows();
			Refresh(false);
		};
		m_setRows.push_back(std::move(r));
	}
	if (m_setSel >= (int)m_setRows.size())
		m_setSel = (int)m_setRows.size() - 1;
}

void HomePanel::SettingsMove(int delta)
{
	if (m_setRows.empty())
		return;
	int i = std::clamp(m_setSel + delta, 0, (int)m_setRows.size() - 1);
	const int step = delta < 0 ? -1 : 1;
	while (i >= 0 && i < (int)m_setRows.size() && m_setRows[i].kind == SettingRow::Header)
		i += step;
	if (i < 0 || i >= (int)m_setRows.size())
	{
		// ran past the end: search the other way
		i = std::clamp(m_setSel, 0, (int)m_setRows.size() - 1);
		while (i < (int)m_setRows.size() && m_setRows[i].kind == SettingRow::Header)
			i++;
	}
	m_setSel = std::clamp(i, 0, (int)m_setRows.size() - 1);
	Refresh(false);
}

void HomePanel::SettingsActivate()
{
	if (m_setSel < 0 || m_setSel >= (int)m_setRows.size())
		return;
	auto& r = m_setRows[m_setSel];
	switch (r.kind)
	{
	case SettingRow::Toggle:
		r.setBool(!r.getBool());
		m_setDirty = true;
		Refresh(false);
		break;
	case SettingRow::Choice:
		SettingsCycle(1);
		break;
	case SettingRow::Action:
	{
		auto action = r.action; // rows are rebuilt inside
		action();
		break;
	}
	default:
		break;
	}
}

void HomePanel::SettingsCycle(int dir)
{
	if (m_setSel < 0 || m_setSel >= (int)m_setRows.size())
		return;
	auto& r = m_setRows[m_setSel];
	if (r.kind == SettingRow::Toggle)
	{
		const bool want = dir > 0;
		if (r.getBool() != want)
		{
			r.setBool(want);
			m_setDirty = true;
		}
		Refresh(false);
		return;
	}
	if (r.kind != SettingRow::Choice || r.options.empty())
		return;
	const int n = (int)r.options.size();
	int idx = r.getIndex() + dir;
	if (r.wrap)
		idx = (idx % n + n) % n;
	else
		idx = std::clamp(idx, 0, n - 1);
	r.setIndex(idx);
	m_setDirty = true;
	Refresh(false);
}

void HomePanel::PaintSettings(wxGraphicsContext* gc, double w, double h, double left)
{
	gc->SetPen(*wxTRANSPARENT_PEN);
	gc->SetBrush(wxBrush(wxColour(6, 14, 16, 150)));
	gc->DrawRectangle(0, 0, w, h);

	double y = 70;
	gc->SetFont(MakeFont(26, true), kText);
	gc->DrawText(U8("\xD0\x9D\xD0\xB0\xD1\x81\xD1\x82\xD1\x80\xD0\xBE\xD0\xB9\xD0\xBA\xD0\xB8"), left, y);
	y += 52;
	gc->SetFont(MakeFont(11), kCaption);
	const wxString sub = (m_setSel >= 0 && m_setSel < (int)m_setRows.size() && !m_setRows[m_setSel].hint.empty())
		? m_setRows[m_setSel].hint : U8("\xD0\x9E\xD1\x81\xD0\xBD\xD0\xBE\xD0\xB2\xD0\xBD\xD1\x8B\xD0\xB5 \xD0\xBF\xD0\xB0\xD1\x80\xD0\xB0\xD0\xBC\xD0\xB5\xD1\x82\xD1\x80\xD1\x8B. \xD0\xA1\xD0\xBE\xD1\x85\xD1\x80\xD0\xB0\xD0\xBD\xD1\x8F\xD1\x8E\xD1\x82\xD1\x81\xD1\x8F \xD0\xBF\xD1\x80\xD0\xB8 \xD0\xB2\xD1\x8B\xD1\x85\xD0\xBE\xD0\xB4\xD0\xB5 \xD1\x81 \xD1\x8D\xD1\x82\xD0\xBE\xD0\xB3\xD0\xBE \xD1\x8D\xD0\xBA\xD1\x80\xD0\xB0\xD0\xBD\xD0\xB0.");
	gc->DrawText(sub, left, y);
	y += 36;

	const double barH = 56;
	const double rowH = 46, headH = 40, gap = 6;
	const double listTop = y, listBottom = h - barH - 20;
	const double listW = std::min(w - left * 2, 1100.0);
	auto heightOf = [&](int i) { return (m_setRows[i].kind == SettingRow::Header ? headH : rowH) + gap; };

	// keep the selection (and the header right above it) visible
	if (m_setSel < m_setScroll)
		m_setScroll = m_setSel;
	if (m_setScroll > 0 && m_setScroll == m_setSel && m_setRows[m_setScroll - 1].kind == SettingRow::Header)
		m_setScroll--;
	for (;;)
	{
		double used = 0;
		for (int i = m_setScroll; i <= m_setSel && i < (int)m_setRows.size(); i++)
			used += heightOf(i);
		if (used <= listBottom - listTop || m_setScroll >= m_setSel)
			break;
		m_setScroll++;
	}
	m_setScroll = std::clamp(m_setScroll, 0, std::max(0, (int)m_setRows.size() - 1));

	for (auto& r : m_setRows)
		r.rect = wxRect();
	for (int i = m_setScroll; i < (int)m_setRows.size(); i++)
	{
		auto& r = m_setRows[i];
		if (y + heightOf(i) - gap > listBottom)
			break;
		if (r.kind == SettingRow::Header)
		{
			gc->SetFont(MakeFont(10, true), kAccent);
			double tw, th;
			wxString t = r.label.Upper();
			gc->GetTextExtent(t, &tw, &th);
			gc->DrawText(t, left + 4, y + headH - th - 6);
			y += headH + gap;
			continue;
		}
		const wxRect rect((int)left, (int)y, (int)listW, (int)rowH);
		r.rect = rect;
		const bool sel = i == m_setSel;
		gc->SetPen(sel ? wxPen(kAccent, 2) : *wxTRANSPARENT_PEN);
		gc->SetBrush(wxBrush(sel ? wxColour(31, 74, 66, 230) : wxColour(16, 30, 33, 210)));
		RoundRect(gc, rect, 10);
		const double ty = rect.y + rowH / 2;
		double tw, th;
		if (r.kind == SettingRow::Toggle)
		{
			const bool on = r.getBool();
			const double sx = rect.x + 16, sy = ty - 11;
			gc->SetPen(*wxTRANSPARENT_PEN);
			gc->SetBrush(wxBrush(on ? kAccent : wxColour(255, 255, 255, 50)));
			gc->DrawRoundedRectangle(sx, sy, 40, 22, 11);
			gc->SetBrush(wxBrush(on ? wxColour(6, 35, 28) : wxColour(207, 227, 221)));
			gc->DrawEllipse(on ? sx + 21 : sx + 3, sy + 3, 16, 16);
			gc->SetFont(MakeFont(13, true), on ? kText : kMuted);
			gc->GetTextExtent(r.label, &tw, &th);
			gc->DrawText(r.label, sx + 56, ty - th / 2);
		}
		else if (r.kind == SettingRow::Choice)
		{
			gc->SetFont(MakeFont(13, true), kText);
			gc->GetTextExtent(r.label, &tw, &th);
			gc->DrawText(r.label, rect.x + 18, ty - th / 2);
			const int idx = std::clamp(r.getIndex(), 0, std::max(0, (int)r.options.size() - 1));
			const wxString val = U8("\xE2\x80\xB9  ") + (r.options.empty() ? wxString() : r.options[idx]) + U8("  \xE2\x80\xBA");
			gc->SetFont(MakeFont(12, true), sel ? kAccent : kText);
			gc->GetTextExtent(val, &tw, &th);
			gc->DrawText(val, rect.x + rect.width - 18 - tw, ty - th / 2);
		}
		else
		{
			gc->SetFont(MakeFont(13, true), sel ? kAccent : kText);
			gc->GetTextExtent(r.label, &tw, &th);
			gc->DrawText(r.label, rect.x + 18, ty - th / 2);
			gc->SetFont(MakeFont(13, true), kMuted);
			gc->GetTextExtent(U8("\xE2\x80\xBA"), &tw, &th);
			gc->DrawText(U8("\xE2\x80\xBA"), rect.x + rect.width - 18 - tw, ty - th / 2);
		}
		y += rowH + gap;
	}

	gc->SetPen(*wxTRANSPARENT_PEN);
	gc->SetBrush(wxBrush(wxColour(5, 10, 12, 200)));
	gc->DrawRectangle(0, h - barH, w, barH);
	gc->SetFont(MakeFont(10), kCaption);
	const wxString hint = U8("\xE2\x86\x91 \xE2\x86\x93 \xE2\x80\x94 \xD0\xB2\xD1\x8B\xD0\xB1\xD0\xBE\xD1\x80     \xE2\x86\x90 \xE2\x86\x92 \xE2\x80\x94 \xD0\xB8\xD0\xB7\xD0\xBC\xD0\xB5\xD0\xBD\xD0\xB8\xD1\x82\xD1\x8C     A / Enter \xE2\x80\x94 \xD0\xBF\xD0\xB5\xD1\x80\xD0\xB5\xD0\xBA\xD0\xBB\xD1\x8E\xD1\x87\xD0\xB8\xD1\x82\xD1\x8C     B / Esc \xE2\x80\x94 \xD0\xBD\xD0\xB0\xD0\xB7\xD0\xB0\xD0\xB4");
	double hw, hh;
	gc->GetTextExtent(hint, &hw, &hh);
	gc->DrawText(hint, w - left - hw, h - barH + (barH - hh) / 2);
}
