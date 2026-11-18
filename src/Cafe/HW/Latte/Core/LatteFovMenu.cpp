#include "Cafe/HW/Latte/Core/LatteFovMenu.h"
#include "Cafe/HW/MMU/MMU.h"
#include "Cafe/CafeSystem.h"
#include "WindowSystem.h"
#include "imgui/imgui_extension.h"
#include <imgui.h>
#include <mutex>
#include <chrono>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <iterator>

namespace FovMenu
{
	namespace
	{
		constexpr float kStep = 0.05f;
		constexpr float kMin = 0.25f;
		constexpr float kMax = 3.0f;
		constexpr float kPresets[] = { 0.75f, 1.0f, 1.25f, 1.5f, 2.0f };
		constexpr uint8 kMarker[8] = { 'F', 'O', 'V', 'C', 'T', 'L', 'R', '!' };

		float LoadBE(const uint8* p)
		{
			uint32 v;
			std::memcpy(&v, p, 4);
			v = _swapEndianU32(v);
			float f;
			std::memcpy(&f, &v, 4);
			return f;
		}

		void StoreBE(uint8* p, float f)
		{
			uint32 v;
			std::memcpy(&v, &f, 4);
			v = _swapEndianU32(v);
			std::memcpy(p, &v, 4);
		}

		std::mutex s_mutex;
		bool s_open = false;
		bool s_waitRelease = false;    // block input after closing until all buttons are released
		uint32 s_prevBits = 0;
		uint32 s_fovAddr = 0;          // guest address of the FOV float, 0 = not found
		uint64 s_scannedTitle = 0;
		float s_fov = 1.0f;
		float s_fovBeforeOpen = 1.0f;
		int s_repeatDir = 0;
		std::chrono::steady_clock::time_point s_repeatAt;

		uint32 FindFovAddress()
		{
			uint8* base = memory_getPointerFromVirtualOffsetAllowNull(MEMORY_CODECAVEAREA_ADDR);
			if (!base)
				return 0;
			const uint32 size = MEMORY_CODECAVEAREA_SIZE;
			for (uint32 i = 0; i + 12 <= size; i += 4)
			{
				if (std::memcmp(base + i, kMarker, sizeof(kMarker)) != 0)
					continue;
				const uint32 addr = MEMORY_CODECAVEAREA_ADDR + i + 8;
				const float v = LoadBE(base + i + 8);
				if (v >= kMin - 0.01f && v <= kMax + 0.01f)
					return addr;
			}
			return 0;
		}

		float ReadFov()
		{
			return LoadBE(memory_getPointerFromVirtualOffset(s_fovAddr));
		}

		void WriteFov(float v)
		{
			v = std::clamp(v, kMin, kMax);
			v = std::round(v * 100.0f) / 100.0f;
			s_fov = v;
			if (s_fovAddr)
				StoreBE(memory_getPointerFromVirtualOffset(s_fovAddr), v);
		}

		void StepPreset(int dir)
		{
			if (dir > 0)
			{
				for (float p : kPresets)
					if (p > s_fov + 0.001f) { WriteFov(p); return; }
				WriteFov(kPresets[std::size(kPresets) - 1]);
			}
			else
			{
				for (int i = (int)std::size(kPresets) - 1; i >= 0; i--)
					if (kPresets[i] < s_fov - 0.001f) { WriteFov(kPresets[i]); return; }
				WriteFov(kPresets[0]);
			}
		}

		void Open()
		{
			const uint64 title = CafeSystem::GetForegroundTitleId();
			if (title != s_scannedTitle || !s_fovAddr)
			{
				s_fovAddr = FindFovAddress();
				s_scannedTitle = title;
			}
			if (s_fovAddr)
				s_fov = ReadFov();
			s_fovBeforeOpen = s_fov;
			s_open = true;
			s_repeatDir = 0;
		}

		void Close(bool keep)
		{
			if (!keep && s_fovAddr)
				WriteFov(s_fovBeforeOpen);
			s_open = false;
			s_waitRelease = true;
		}
	}

	bool FeedInput(uint32 bits)
	{
		std::unique_lock lock(s_mutex);
		const uint32 pressed = bits & ~s_prevBits;
		s_prevBits = bits;

		const bool combo = (bits & kL3) && (bits & kR3) && (pressed & (kL3 | kR3));
		if (combo)
		{
			if (s_open)
				Close(true);
			else
				Open();
			return true;
		}

		if (!s_open)
		{
			if (s_waitRelease)
			{
				if (bits == 0)
					s_waitRelease = false;
				return s_waitRelease;
			}
			return false;
		}

		if (pressed & kA) { Close(true); return true; }
		if (pressed & kB) { Close(false); return true; }
		if (!s_fovAddr)
			return true;
		if (pressed & kUp) StepPreset(+1);
		if (pressed & kDown) StepPreset(-1);

		const auto now = std::chrono::steady_clock::now();
		const int dir = (bits & kRight) ? 1 : ((bits & kLeft) ? -1 : 0);
		if (dir && dir != s_repeatDir)
		{
			s_repeatDir = dir;
			s_repeatAt = now + std::chrono::milliseconds(350);
			WriteFov(s_fov + dir * kStep);
		}
		else if (dir && now >= s_repeatAt)
		{
			s_repeatAt = now + std::chrono::milliseconds(70);
			WriteFov(s_fov + dir * kStep);
		}
		else if (!dir)
			s_repeatDir = 0;
		return true;
	}

	void Render()
	{
		bool open;
		bool found;
		float fov;
		{
			std::unique_lock lock(s_mutex);
			open = s_open;
			found = s_fovAddr != 0;
			fov = s_fov;
		}
		if (!open)
			return;

		sint32 w = 0, h = 0;
		WindowSystem::GetWindowPhysSize(w, h);
		if (w == 0 || h == 0)
			return;
		const float dpi = WindowSystem::GetWindowDPIScale();
		const float bigSize = std::round(30.0f * dpi);
		const float smallSize = std::round(15.0f * dpi);
		ImFont* big = ImGui_GetFont(bigSize);
		ImFont* small = ImGui_GetFont(smallSize);
		if (!big || !small)
			return; // fonts are created on the next frame

		const ImVec4 accent(127 / 255.0f, 224 / 255.0f, 196 / 255.0f, 1.0f);
		const ImVec4 muted(0.62f, 0.74f, 0.71f, 1.0f);
		const float panelW = 520.0f * dpi;

		ImGui::SetNextWindowPos(ImVec2(w * 0.5f, 40.0f * dpi), ImGuiCond_Always, ImVec2(0.5f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(panelW, 0.0f), ImGuiCond_Always);
		ImGui::SetNextWindowBgAlpha(0.88f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 16.0f * dpi);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22.0f * dpi, 18.0f * dpi));
		ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.04f, 0.09f, 0.10f, 1.0f));
		ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.29f, 0.55f, 0.48f, 0.6f));
		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBringToFrontOnFocus;
		if (ImGui::Begin("##fovmenu", nullptr, flags))
		{
			ImDrawList* dl = ImGui::GetWindowDrawList();
			ImGui::PushFont(small);
			ImGui::TextColored(muted, "FIELD OF VIEW");
			ImGui::PopFont();

			if (!found)
			{
				ImGui::PushFont(small);
				ImGui::TextWrapped("Camera FOV Changer graphic pack with the FOVCTLR! marker is not active. Enable it and restart the game.");
				ImGui::TextColored(muted, "A / B - close");
				ImGui::PopFont();
			}
			else
			{
				ImGui::PushFont(big);
				char buf[32];
				snprintf(buf, sizeof(buf), "%.2fx", fov);
				const float tw = ImGui::CalcTextSize(buf).x;
				ImGui::SameLine(panelW - 22.0f * dpi - tw);
				ImGui::SetCursorPosY(ImGui::GetCursorPosY() - bigSize * 0.45f);
				ImGui::TextColored(accent, "%s", buf);
				ImGui::PopFont();

				// slider
				ImGui::Dummy(ImVec2(0, 6.0f * dpi));
				const ImVec2 p = ImGui::GetCursorScreenPos();
				const float trackW = panelW - 44.0f * dpi;
				const float t = (fov - kMin) / (kMax - kMin);
				const float ty = p.y + 10.0f * dpi;
				dl->AddRectFilled(ImVec2(p.x, ty - 4 * dpi), ImVec2(p.x + trackW, ty + 4 * dpi), IM_COL32(34, 56, 58, 255), 4 * dpi);
				dl->AddRectFilled(ImVec2(p.x, ty - 4 * dpi), ImVec2(p.x + trackW * t, ty + 4 * dpi), ImGui::GetColorU32(accent), 4 * dpi);
				dl->AddCircleFilled(ImVec2(p.x + trackW * t, ty), 11 * dpi, IM_COL32(234, 255, 248, 255));
				ImGui::Dummy(ImVec2(trackW, 24.0f * dpi));

				// presets
				ImGui::PushFont(small);
				const float gap = 8.0f * dpi;
				const float pw = (trackW - gap * (float)(std::size(kPresets) - 1)) / (float)std::size(kPresets);
				const ImVec2 pp = ImGui::GetCursorScreenPos();
				for (size_t i = 0; i < std::size(kPresets); i++)
				{
					const bool on = std::abs(fov - kPresets[i]) < 0.001f;
					const ImVec2 a(pp.x + (float)i * (pw + gap), pp.y);
					const ImVec2 b(a.x + pw, a.y + 30.0f * dpi);
					dl->AddRectFilled(a, b, on ? IM_COL32(31, 74, 66, 255) : IM_COL32(22, 38, 42, 255), 8 * dpi);
					snprintf(buf, sizeof(buf), "%g", kPresets[i]);
					const ImVec2 ts = ImGui::CalcTextSize(buf);
					dl->AddText(ImVec2(a.x + (pw - ts.x) * 0.5f, a.y + (30.0f * dpi - ts.y) * 0.5f), on ? ImGui::GetColorU32(accent) : IM_COL32(238, 243, 241, 255), buf);
				}
				ImGui::Dummy(ImVec2(trackW, 38.0f * dpi));
				ImGui::TextColored(muted, "Left/Right - adjust    Up/Down - presets    A - OK    B - cancel");
				ImGui::PopFont();
			}
		}
		ImGui::End();
		ImGui::PopStyleColor(2);
		ImGui::PopStyleVar(2);
	}
}
