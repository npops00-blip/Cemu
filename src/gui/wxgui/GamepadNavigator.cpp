#include "wxgui/wxgui.h"
#include "wxgui/GamepadNavigator.h"
#include "wxgui/PadViewFrame.h"
#include "input/InputManager.h"
#include "Cafe/HW/Latte/Core/LatteFovMenu.h"

#include <wx/toplevel.h>
#include <wx/time.h>

#if BOOST_OS_WINDOWS
#include <wx/msw/wrapwin.h>
#endif

namespace
{
#if BOOST_OS_WINDOWS
	void SendKey(WORD vk, bool shift = false, bool ctrl = false)
	{
		INPUT in[6]{};
		int n = 0;
		auto key = [&](WORD k, bool up) {
			in[n].type = INPUT_KEYBOARD;
			in[n].ki.wVk = k;
			in[n].ki.dwFlags = up ? KEYEVENTF_KEYUP : 0;
			if (k == VK_LEFT || k == VK_RIGHT || k == VK_UP || k == VK_DOWN)
				in[n].ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
			n++;
		};
		if (ctrl) key(VK_CONTROL, false);
		if (shift) key(VK_SHIFT, false);
		key(vk, false);
		key(vk, true);
		if (shift) key(VK_SHIFT, true);
		if (ctrl) key(VK_CONTROL, true);
		SendInput(n, in, sizeof(INPUT));
	}
#endif
}

GamepadNavigator::GamepadNavigator(wxWindow* mainWindow)
	: m_mainWindow(mainWindow), m_timer(this)
{
	Bind(wxEVT_TIMER, &GamepadNavigator::OnTimer, this);
	m_timer.Start(33);
}

GamepadNavigator::~GamepadNavigator()
{
	m_timer.Stop();
}

void GamepadNavigator::OnTimer(wxTimerEvent&)
{
#if BOOST_OS_WINDOWS
	wxWindow* active = wxGetActiveWindow();
	wxWindow* top = active ? wxGetTopLevelParent(active) : nullptr;
	if (!top || top == m_mainWindow || dynamic_cast<PadViewFrame*>(top))
	{
		m_prev = 0;
		return;
	}

	uint32 bits = 0;
	auto& im = InputManager::instance();
	for (size_t i = 0; i < InputManager::kMaxController; i++)
	{
		const auto controller = im.get_controller(i);
		if (!controller)
			continue;
		controller->controllers_update_states();
		bits |= controller->fov_menu_bits();
		if (controller->is_up_down()) bits |= FovMenu::kUp;
		if (controller->is_down_down()) bits |= FovMenu::kDown;
		if (controller->is_left_down()) bits |= FovMenu::kLeft;
		if (controller->is_right_down()) bits |= FovMenu::kRight;
		if (controller->is_a_down()) bits |= FovMenu::kA;
		if (controller->is_b_down()) bits |= FovMenu::kB;
		if (controller->is_start_down()) bits |= FovMenu::kStart;
	}
	const uint32 pressed = bits & ~m_prev;
	m_prev = bits;
	const wxLongLong now = wxGetLocalTimeMillis();

	constexpr uint32 kRepeatable = FovMenu::kUp | FovMenu::kDown | FovMenu::kLeft | FovMenu::kRight | FovMenu::kLB | FovMenu::kRB;
	uint32 fire = pressed;
	if (pressed & kRepeatable)
	{
		m_repeatBit = pressed & kRepeatable;
		m_repeatBit &= ~(m_repeatBit - 1); // lowest set bit
		m_repeatAt = now + 400;
	}
	else if (m_repeatBit && (bits & m_repeatBit) && now >= m_repeatAt)
	{
		fire |= m_repeatBit;
		m_repeatAt = now + 110;
	}
	else if (!(bits & m_repeatBit))
		m_repeatBit = 0;

	if (fire & FovMenu::kUp) SendKey(VK_UP);
	if (fire & FovMenu::kDown) SendKey(VK_DOWN);
	if (fire & FovMenu::kLeft) SendKey(VK_LEFT);
	if (fire & FovMenu::kRight) SendKey(VK_RIGHT);
	if (fire & FovMenu::kLB) SendKey(VK_TAB, true);
	if (fire & FovMenu::kRB) SendKey(VK_TAB);
	if (fire & FovMenu::kA) SendKey(VK_SPACE);
	if (fire & FovMenu::kStart) SendKey(VK_RETURN);
	if (fire & FovMenu::kB) SendKey(VK_ESCAPE);
#endif
}
