#include "wxgui/wxgui.h"
#include "wxgui/HomePanel.h"
#include "wxgui/MainWindow.h"
#include "wxgui/wxHelper.h"

#include "Cafe/TitleList/TitleList.h"
#include "Cafe/IOSU/PDM/iosu_pdm.h"
#include "config/ActiveSettings.h"

#include <wx/dcbuffer.h>
#include <wx/graphics.h>
#include <wx/utils.h>
#include <wx/msgdlg.h>
#include <wx/datetime.h>
#include <wx/filedlg.h>
#include <wx/menu.h>
#include <algorithm>

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
	  m_callbacks(std::move(callbacks)), m_timer(this)
{
	SetBackgroundStyle(wxBG_STYLE_PAINT);

	m_buttons.push_back({ U8("\xD0\x98\xD0\xB3\xD1\x80\xD0\xB0\xD1\x82\xD1\x8C"), "Enter", {}, true, [this]() { LaunchGame(); } });
	m_buttons.push_back({ U8("\xD0\x93\xD1\x80\xD0\xB0\xD1\x84\xD0\xBF\xD0\xB0\xD0\xBA\xD0\xB8"), "G", {}, false, [this]() { if (m_callbacks.openGraphicPacks) m_callbacks.openGraphicPacks(); } });
	m_buttons.push_back({ U8("\xD0\x9C\xD0\xBE\xD0\xB4\xD1\x8B (UKMM)"), "M", {}, false, [this]() { OpenModManager(); } });
	m_buttons.push_back({ U8("\xD0\x9D\xD0\xB0\xD1\x81\xD1\x82\xD1\x80\xD0\xBE\xD0\xB9\xD0\xBA\xD0\xB8"), "S", {}, false, [this]() { if (m_callbacks.openSettings) m_callbacks.openSettings(); } });
	m_buttons.push_back({ U8("\xD0\xA1\xD0\xBF\xD0\xB8\xD1\x81\xD0\xBE\xD0\xBA \xD0\xB8\xD0\xB3\xD1\x80"), "L", {}, false, [this]() { if (m_callbacks.showGameList) m_callbacks.showGameList(); } });

	Bind(wxEVT_PAINT, &HomePanel::OnPaint, this);
	Bind(wxEVT_SIZE, &HomePanel::OnSize, this);
	Bind(wxEVT_MOTION, &HomePanel::OnMouseMove, this);
	Bind(wxEVT_LEAVE_WINDOW, &HomePanel::OnMouseLeave, this);
	Bind(wxEVT_LEFT_UP, &HomePanel::OnLeftUp, this);
	Bind(wxEVT_KEY_DOWN, &HomePanel::OnKeyDown, this);
	Bind(wxEVT_TIMER, &HomePanel::OnTimer, this);
	Bind(wxEVT_RIGHT_UP, &HomePanel::OnRightUp, this);

	LoadBackground();

	RefreshGame();
	m_timer.Start(2000);
}

HomePanel::~HomePanel()
{
	m_timer.Stop();
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
	wxString hint = U8("Enter \xE2\x80\x94 \xD0\xB8\xD0\xB3\xD1\x80\xD0\xB0\xD1\x82\xD1\x8C     \xE2\x86\x90 \xE2\x86\x92 \xE2\x80\x94 \xD0\xB2\xD1\x8B\xD0\xB1\xD0\xBE\xD1\x80     \xD0\x9F\xD0\x9A\xD0\x9C \xE2\x80\x94 \xD1\x81\xD0\xBC\xD0\xB5\xD0\xBD\xD0\xB8\xD1\x82\xD1\x8C \xD1\x84\xD0\xBE\xD0\xBD");
	double hw, hh;
	gc->GetTextExtent(hint, &hw, &hh);
	gc->DrawText(hint, w - left - hw, h - barH + (barH - hh) / 2);
	if (!m_status.empty())
		gc->DrawText(m_status, left, h - barH + (barH - hh) / 2);
}

void HomePanel::OnMouseMove(wxMouseEvent& event)
{
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
	for (size_t i = 0; i < m_buttons.size(); i++)
		if (m_buttons[i].rect.Contains(event.GetPosition()))
			Activate((int)i);
}

void HomePanel::OnKeyDown(wxKeyEvent& event)
{
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
