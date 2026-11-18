#pragma once

// In-game FOV menu for Breath of the Wild (works with the "Camera FOV Changer" graphic pack
// whose codecave contains the marker "FOVCTLR!" followed by the FOV float).
//
// Open/close: press L3 + R3 together (Wii U stick buttons as mapped in Cemu's input settings).
// While open, game input is blocked; D-pad / left stick left-right adjusts FOV,
// up/down jumps between presets, A keeps the value, B restores the previous one.

namespace FovMenu
{
	enum InputBits : uint32
	{
		kL3 = 1 << 0,
		kR3 = 1 << 1,
		kLeft = 1 << 2,
		kRight = 1 << 3,
		kUp = 1 << 4,
		kDown = 1 << 5,
		kA = 1 << 6,
		kB = 1 << 7,
	};

	// called from the VPAD read path (CPU thread) once per read with the raw button state.
	// returns true if the game should not see any input this read
	bool FeedInput(uint32 bits);

	// called from the render thread inside an ImGui frame (main window only)
	void Render();
}
