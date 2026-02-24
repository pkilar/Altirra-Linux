//	Altirra - Atari 800/800XL/5200 emulator
//	Linux port - Dear ImGui debugger UI
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.

#include <vd2/system/vdtypes.h>
#include <vd2/system/vdstl.h>
#include <vd2/system/VDString.h>
#include <at/atdebugger/target.h>
#include <at/atcpu/execstate.h>
#include <at/atcpu/history.h>

#include <imgui.h>
#include <debugger_imgui.h>

#include "debugger.h"
#include "disasm.h"

#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>
#include <string>

// Console output buffer — written by ATConsoleWrite, read by ImGui draw
static std::mutex s_consoleMutex;
static std::string s_consoleBuffer;
static constexpr size_t kMaxConsoleBuffer = 256 * 1024;

// Console input history
static std::vector<std::string> s_cmdHistory;
static int s_cmdHistoryPos = -1;
static char s_cmdInputBuf[512] = {};

// Debugger client for state updates
class ATImGuiDebuggerClient : public IATDebuggerClient {
public:
	void OnDebuggerSystemStateUpdate(const ATDebuggerSystemState& state) override {
		mState = state;
		mbStateValid = true;
	}

	void OnDebuggerEvent(ATDebugEvent eventId) override {
		if (eventId == kATDebugEvent_BreakpointsChanged)
			mbBreakpointsChanged = true;
	}

	ATDebuggerSystemState mState {};
	bool mbStateValid = false;
	bool mbBreakpointsChanged = false;
};

static ATImGuiDebuggerClient *s_pClient = nullptr;

// Window visibility state
static bool s_showRegisters = true;
static bool s_showDisassembly = true;
static bool s_showMemory = true;
static bool s_showConsole = true;
static bool s_showBreakpoints = true;
static bool s_showWatch = false;
static bool s_showCallStack = false;

// Watch window state
static char s_watchAddrBuf[16] = "";

// Memory window state
static uint32 s_memoryAddr = 0;
static char s_memoryAddrBuf[16] = "0000";
static int s_memEditByteIdx = -1;  // -1 = no byte being edited
static char s_memEditBuf[4] = "";

// Breakpoint add state
static char s_bpAddrBuf[16] = "";

// Disassembly context menu state
static uint16 s_disasmContextAddr = 0;

// Disassembly state
static uint32 s_disasmAddr = 0;
static bool s_disasmFollowPC = true;
static char s_disasmAddrBuf[16] = "0000";

void ATImGuiDebuggerInit() {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return;

	s_pClient = new ATImGuiDebuggerClient;
	dbg->AddClient(s_pClient, true);
}

void ATImGuiDebuggerShutdown() {
	if (s_pClient) {
		IATDebugger *dbg = ATGetDebugger();
		if (dbg)
			dbg->RemoveClient(s_pClient);
		delete s_pClient;
		s_pClient = nullptr;
	}
}

void ATImGuiDebuggerAppendConsole(const char *s) {
	std::lock_guard<std::mutex> lock(s_consoleMutex);
	s_consoleBuffer.append(s);
	if (s_consoleBuffer.size() > kMaxConsoleBuffer) {
		size_t trim = s_consoleBuffer.size() - kMaxConsoleBuffer;
		// Find next newline after trim point
		size_t nl = s_consoleBuffer.find('\n', trim);
		if (nl != std::string::npos)
			s_consoleBuffer.erase(0, nl + 1);
		else
			s_consoleBuffer.erase(0, trim);
	}
}

// Input callback for command history
static int ConsoleInputCallback(ImGuiInputTextCallbackData *data) {
	if (data->EventFlag == ImGuiInputTextFlags_CallbackHistory) {
		if (s_cmdHistory.empty())
			return 0;

		if (data->EventKey == ImGuiKey_UpArrow) {
			if (s_cmdHistoryPos < 0)
				s_cmdHistoryPos = (int)s_cmdHistory.size() - 1;
			else if (s_cmdHistoryPos > 0)
				s_cmdHistoryPos--;
		} else if (data->EventKey == ImGuiKey_DownArrow) {
			if (s_cmdHistoryPos >= 0) {
				s_cmdHistoryPos++;
				if (s_cmdHistoryPos >= (int)s_cmdHistory.size())
					s_cmdHistoryPos = -1;
			}
		}

		if (s_cmdHistoryPos >= 0 && s_cmdHistoryPos < (int)s_cmdHistory.size()) {
			data->DeleteChars(0, data->BufTextLen);
			data->InsertChars(0, s_cmdHistory[s_cmdHistoryPos].c_str());
		} else {
			data->DeleteChars(0, data->BufTextLen);
		}
	}
	return 0;
}

// ============= Toolbar =============

static void DrawToolbar() {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return;

	bool running = dbg->IsRunning();

	if (ImGui::BeginMainMenuBar()) {
		if (ImGui::BeginMenu("Debug")) {
			if (running) {
				if (ImGui::MenuItem("Break", "F5"))
					dbg->Break();
			} else {
				if (ImGui::MenuItem("Run", "F5"))
					dbg->Run(kATDebugSrcMode_Disasm);
			}

			ImGui::Separator();

			if (ImGui::MenuItem("Step Into", "F11", false, !running))
				dbg->StepInto(kATDebugSrcMode_Disasm);

			if (ImGui::MenuItem("Step Over", "F10", false, !running))
				dbg->StepOver(kATDebugSrcMode_Disasm);

			if (ImGui::MenuItem("Step Out", "Shift+F11", false, !running))
				dbg->StepOut(kATDebugSrcMode_Disasm);

			ImGui::EndMenu();
		}

		if (ImGui::BeginMenu("View")) {
			ImGui::MenuItem("Registers", nullptr, &s_showRegisters);
			ImGui::MenuItem("Disassembly", nullptr, &s_showDisassembly);
			ImGui::MenuItem("Memory", nullptr, &s_showMemory);
			ImGui::MenuItem("Console", nullptr, &s_showConsole);
			ImGui::MenuItem("Breakpoints", nullptr, &s_showBreakpoints);
			ImGui::MenuItem("Watch", nullptr, &s_showWatch);
			ImGui::MenuItem("Call Stack", nullptr, &s_showCallStack);
			ImGui::EndMenu();
		}

		// Status indicator on the right
		const char *status = running ? "RUNNING" : "STOPPED";
		ImVec4 color = running ? ImVec4(0.2f, 0.8f, 0.2f, 1.0f) : ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
		float textW = ImGui::CalcTextSize(status).x;
		ImGui::SameLine(ImGui::GetWindowWidth() - textW - 20.0f);
		ImGui::TextColored(color, "%s", status);

		ImGui::EndMainMenuBar();
	}
}

// ============= Registers Window =============

static void DrawRegisters() {
	if (!s_showRegisters)
		return;

	ImGui::SetNextWindowSize(ImVec2(260, 200), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Registers", &s_showRegisters)) {
		ImGui::End();
		return;
	}

	if (!s_pClient || !s_pClient->mbStateValid) {
		ImGui::TextDisabled("No debugger state available");
		ImGui::End();
		return;
	}

	IATDebugger *dbg = ATGetDebugger();
	IATDebugTarget *target = dbg ? dbg->GetTarget() : nullptr;
	const auto& state = s_pClient->mState;
	const auto& regs = state.mExecState.m6502;

	// Helper: editable hex register field (double-click to edit)
	static int s_regEditId = -1;
	static char s_regEditBuf[8] = "";

	auto EditableReg = [&](const char *name, int id, uint32 val, int hexDigits) {
		if (s_regEditId == id) {
			ImGui::Text("%s:", name);
			ImGui::SameLine();
			ImGui::SetNextItemWidth((float)(hexDigits * 10 + 8));
			bool commit = ImGui::InputText("##regedit", s_regEditBuf,
				sizeof(s_regEditBuf),
				ImGuiInputTextFlags_CharsHexadecimal
				| ImGuiInputTextFlags_EnterReturnsTrue
				| ImGuiInputTextFlags_AutoSelectAll);

			if (commit) {
				unsigned int newVal;
				if (sscanf(s_regEditBuf, "%x", &newVal) == 1 && target) {
					ATCPUExecState es;
					target->GetExecState(es);
					switch (id) {
						case 0: es.m6502.mPC = (uint16)newVal; break;
						case 1: es.m6502.mA = (uint8)newVal; break;
						case 2: es.m6502.mX = (uint8)newVal; break;
						case 3: es.m6502.mY = (uint8)newVal; break;
						case 4: es.m6502.mS = (uint8)newVal; break;
						case 5: es.m6502.mP = (uint8)newVal; break;
					}
					target->SetExecState(es);
				}
				s_regEditId = -1;
			} else if (!ImGui::IsItemActive() && ImGui::IsItemDeactivated()) {
				s_regEditId = -1;
			}
		} else {
			if (hexDigits == 4)
				ImGui::Text("%s: %04X", name, val);
			else if (hexDigits == 2)
				ImGui::Text("%s: %02X (%3d)", name, val, val);
			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
				s_regEditId = id;
				snprintf(s_regEditBuf, sizeof(s_regEditBuf),
					hexDigits == 4 ? "%04X" : "%02X", val);
			}
		}
	};

	// 6502 registers (double-click to edit)
	EditableReg("PC", 0, state.mPC, 4);
	ImGui::SameLine(140);
	ImGui::Text("Cycle: %u", state.mCycle);

	ImGui::Separator();

	EditableReg(" A", 1, regs.mA, 2);
	EditableReg(" X", 2, regs.mX, 2);
	EditableReg(" Y", 3, regs.mY, 2);
	EditableReg(" S", 4, regs.mS, 2);

	ImGui::Separator();

	// Processor status flags (double-click to edit)
	uint8 p = regs.mP;
	EditableReg(" P", 5, p, 2);
	ImGui::SameLine();

	const char flags[] = "NV-BDIZC";

	// Color active flags
	for (int i = 0; i < 8; i++) {
		if (i > 0) ImGui::SameLine(0, 0);
		if (p & (0x80 >> i))
			ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "%c", flags[i]);
		else
			ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), ".");
	}

	ImGui::End();
}

// ============= Disassembly Window =============

static void DrawDisassembly() {
	if (!s_showDisassembly)
		return;

	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return;

	ImGui::SetNextWindowSize(ImVec2(450, 400), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Disassembly", &s_showDisassembly)) {
		ImGui::End();
		return;
	}

	IATDebugTarget *target = dbg->GetTarget();
	if (!target || !s_pClient || !s_pClient->mbStateValid) {
		ImGui::TextDisabled("No debug target");
		ImGui::End();
		return;
	}

	const auto& state = s_pClient->mState;
	uint16 pc = state.mPC;

	// Controls
	ImGui::Checkbox("Follow PC", &s_disasmFollowPC);
	ImGui::SameLine();

	ImGui::SetNextItemWidth(80);
	if (ImGui::InputText("##addr", s_disasmAddrBuf, sizeof(s_disasmAddrBuf),
		ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
		unsigned int addr;
		if (sscanf(s_disasmAddrBuf, "%x", &addr) == 1) {
			s_disasmAddr = addr & 0xFFFF;
			s_disasmFollowPC = false;
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Go"))  {
		unsigned int addr;
		if (sscanf(s_disasmAddrBuf, "%x", &addr) == 1) {
			s_disasmAddr = addr & 0xFFFF;
			s_disasmFollowPC = false;
		}
	}

	if (s_disasmFollowPC)
		s_disasmAddr = pc;

	ImGui::Separator();

	// Find a good starting address a few instructions before the target
	uint16 startAddr = ATDisassembleGetFirstAnchor(target, (s_disasmAddr - 32) & 0xFFFF, s_disasmAddr, 0);

	// Walk forward to the target, then display ~30 lines
	uint16 addr = startAddr;
	VDStringA line;

	// Pre-walk to align with target address
	int preLines = 0;
	while (addr < s_disasmAddr && preLines < 32) {
		ATCPUHistoryEntry hent;
		ATDisassembleCaptureInsnContext(target, addr, 0, hent);
		ATDisasmResult result = ATDisassembleInsn(line, target, state.mExecMode, hent,
			true, false, true, true, true, false, false, true, true, false);
		addr = result.mNextPC;
		preLines++;
	}

	// Now addr should be at or very near s_disasmAddr
	addr = s_disasmAddr;

	if (ImGui::BeginChild("##disasmlines", ImVec2(0, 0), ImGuiChildFlags_None)) {
		// Scroll wheel: move by ~3 instructions per notch
		if (ImGui::IsWindowHovered()) {
			float wheel = ImGui::GetIO().MouseWheel;
			if (wheel != 0.0f) {
				int lines = (int)(-wheel) * 3;
				if (lines > 0) {
					// Scroll down: advance address forward
					uint16 a = s_disasmAddr;
					for (int i = 0; i < lines; i++) {
						ATCPUHistoryEntry h;
						ATDisassembleCaptureInsnContext(target, a, 0, h);
						ATDisasmResult r = ATDisassembleInsn(line, target,
							state.mExecMode, h, true, false, true, true, true,
							false, false, true, true, false);
						a = r.mNextPC;
					}
					s_disasmAddr = a;
				} else {
					// Scroll up: approximate by going back N bytes
					s_disasmAddr = (s_disasmAddr + lines) & 0xFFFF;
				}
				s_disasmFollowPC = false;
				snprintf(s_disasmAddrBuf, sizeof(s_disasmAddrBuf), "%04X",
					s_disasmAddr);
			}
		}

		for (int i = 0; i < 30; i++) {
			ATCPUHistoryEntry hent;
			ATDisassembleCaptureInsnContext(target, addr, 0, hent);

			line.clear();
			ATDisasmResult result = ATDisassembleInsn(line, target, state.mExecMode, hent,
				true, false, true, true, true, false, false, true, true, false);

			bool isPC = (addr == pc);
			bool isBP = dbg->IsBreakpointAtPC(addr);

			// Build display: marker + address + instruction
			char marker = ' ';
			if (isPC && isBP) marker = '*';
			else if (isPC) marker = '>';
			else if (isBP) marker = 'o';

			ImVec4 color(0.8f, 0.8f, 0.8f, 1.0f);
			if (isPC)
				color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
			if (isBP)
				color = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
			if (isPC && isBP)
				color = ImVec4(1.0f, 1.0f, 0.3f, 1.0f);

			// Highlight current PC line background
			if (isPC) {
				ImVec2 pos = ImGui::GetCursorScreenPos();
				ImVec2 size(ImGui::GetContentRegionAvail().x, ImGui::GetTextLineHeight());
				ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y),
					IM_COL32(60, 60, 100, 180));
			}

			// Selectable line — click to toggle breakpoint, right-click for context menu
			char label[256];
			snprintf(label, sizeof(label), "%c %s##d%d", marker, line.c_str(), i);

			ImGui::PushStyleColor(ImGuiCol_Text, color);
			if (ImGui::Selectable(label, false, ImGuiSelectableFlags_None)) {
				dbg->ToggleBreakpoint(addr);
			}
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
				s_disasmContextAddr = addr;
				ImGui::OpenPopup("##disasmctx");
			}
			ImGui::PopStyleColor();

			addr = result.mNextPC;
		}
		// Context menu
		if (ImGui::BeginPopup("##disasmctx")) {
			ImGui::Text("$%04X", s_disasmContextAddr);
			ImGui::Separator();

			bool hasBP = dbg->IsBreakpointAtPC(s_disasmContextAddr);
			if (ImGui::MenuItem(hasBP ? "Remove Breakpoint" : "Set Breakpoint")) {
				dbg->ToggleBreakpoint(s_disasmContextAddr);
			}

			if (ImGui::MenuItem("Go to Address")) {
				s_disasmAddr = s_disasmContextAddr;
				s_disasmFollowPC = false;
				snprintf(s_disasmAddrBuf, sizeof(s_disasmAddrBuf), "%04X",
					s_disasmAddr);
			}

			if (ImGui::MenuItem("Go to PC")) {
				s_disasmFollowPC = true;
			}

			if (ImGui::MenuItem("View in Memory")) {
				s_memoryAddr = s_disasmContextAddr;
				snprintf(s_memoryAddrBuf, sizeof(s_memoryAddrBuf), "%04X",
					s_memoryAddr);
				s_showMemory = true;
			}

			ImGui::EndPopup();
		}
	}
	ImGui::EndChild();

	ImGui::End();
}

// ============= Memory Window =============

static void DrawMemory() {
	if (!s_showMemory)
		return;

	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return;

	ImGui::SetNextWindowSize(ImVec2(490, 300), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Memory", &s_showMemory)) {
		ImGui::End();
		return;
	}

	IATDebugTarget *target = dbg->GetTarget();
	if (!target) {
		ImGui::TextDisabled("No debug target");
		ImGui::End();
		return;
	}

	// Address input
	ImGui::SetNextItemWidth(80);
	if (ImGui::InputText("##memaddr", s_memoryAddrBuf, sizeof(s_memoryAddrBuf),
		ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue)) {
		unsigned int addr;
		if (sscanf(s_memoryAddrBuf, "%x", &addr) == 1)
			s_memoryAddr = addr & 0xFFFF;
	}
	ImGui::SameLine();
	if (ImGui::Button("Go##mem")) {
		unsigned int addr;
		if (sscanf(s_memoryAddrBuf, "%x", &addr) == 1)
			s_memoryAddr = addr & 0xFFFF;
	}

	ImGui::Separator();

	// 16x16 hex dump with inline editing
	if (ImGui::BeginChild("##memlines", ImVec2(0, 0), ImGuiChildFlags_None)) {
		// Scroll wheel: move by 1 row (16 bytes) per notch
		if (ImGui::IsWindowHovered()) {
			float wheel = ImGui::GetIO().MouseWheel;
			if (wheel != 0.0f) {
				int delta = (int)(-wheel) * 16;
				s_memoryAddr = (s_memoryAddr + delta) & 0xFFFF;
				snprintf(s_memoryAddrBuf, sizeof(s_memoryAddrBuf), "%04X", s_memoryAddr);
			}
		}

		uint8 buf[256];
		target->DebugReadMemory(s_memoryAddr, buf, 256);

		for (int row = 0; row < 16; row++) {
			uint16 rowAddr = (s_memoryAddr + row * 16) & 0xFFFF;

			// Address — right-click for context menu
			ImGui::Text("%04X:", rowAddr);
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
				s_disasmContextAddr = rowAddr;
				ImGui::OpenPopup("##memctx");
			}

			// Hex bytes — clickable for editing
			for (int col = 0; col < 16; col++) {
				if (col == 8) ImGui::SameLine(0, 8);
				else ImGui::SameLine(0, 4);

				int byteIdx = row * 16 + col;

				if (s_memEditByteIdx == byteIdx) {
					// Inline edit field
					ImGui::SetNextItemWidth(22);
					ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(1, 0));
					bool commit = ImGui::InputText("##memedit", s_memEditBuf,
						sizeof(s_memEditBuf),
						ImGuiInputTextFlags_CharsHexadecimal
						| ImGuiInputTextFlags_EnterReturnsTrue
						| ImGuiInputTextFlags_AutoSelectAll);
					ImGui::PopStyleVar();

					// Auto-commit when 2 chars typed
					if (strlen(s_memEditBuf) >= 2)
						commit = true;

					if (commit) {
						unsigned int val;
						if (sscanf(s_memEditBuf, "%x", &val) == 1) {
							uint16 addr = (s_memoryAddr + byteIdx) & 0xFFFF;
							target->WriteByte(addr, (uint8)val);
						}
						s_memEditByteIdx = -1;
					} else if (!ImGui::IsItemActive() && ImGui::IsItemDeactivated()) {
						// Lost focus without Enter — cancel
						s_memEditByteIdx = -1;
					}
				} else {
					// Clickable hex byte
					char label[16];
					snprintf(label, sizeof(label), "%02X##b%d", buf[byteIdx], byteIdx);
					if (ImGui::Selectable(label, false,
						ImGuiSelectableFlags_None, ImVec2(22, 0))) {
						s_memEditByteIdx = byteIdx;
						snprintf(s_memEditBuf, sizeof(s_memEditBuf), "%02X",
							buf[byteIdx]);
					}
				}
			}

			ImGui::SameLine(0, 12);

			// ASCII
			char ascii[17];
			for (int col = 0; col < 16; col++) {
				uint8 c = buf[row * 16 + col];
				ascii[col] = (c >= 32 && c < 127) ? (char)c : '.';
			}
			ascii[16] = 0;
			ImGui::TextColored(ImVec4(0.6f, 0.8f, 0.6f, 1.0f), "%s", ascii);
		}

		// Context menu for memory rows
		if (ImGui::BeginPopup("##memctx")) {
			ImGui::Text("$%04X", s_disasmContextAddr);
			ImGui::Separator();

			if (ImGui::MenuItem("View in Disassembly")) {
				s_disasmAddr = s_disasmContextAddr;
				s_disasmFollowPC = false;
				snprintf(s_disasmAddrBuf, sizeof(s_disasmAddrBuf), "%04X",
					s_disasmAddr);
				s_showDisassembly = true;
			}

			// Follow pointer: read 2 bytes at this address as little-endian addr
			uint16 ptrAddr = buf[0] | (buf[1] << 8);  // bytes at context addr
			int offset = (s_disasmContextAddr - s_memoryAddr) & 0xFF;
			if (offset + 1 < 256)
				ptrAddr = buf[offset] | (buf[offset + 1] << 8);

			char ptrLabel[32];
			snprintf(ptrLabel, sizeof(ptrLabel), "Follow Pointer ($%04X)", ptrAddr);
			if (ImGui::MenuItem(ptrLabel)) {
				s_memoryAddr = ptrAddr;
				snprintf(s_memoryAddrBuf, sizeof(s_memoryAddrBuf), "%04X",
					s_memoryAddr);
			}

			ImGui::EndPopup();
		}
	}
	ImGui::EndChild();

	ImGui::End();
}

// ============= Console Window =============

static void DrawConsole() {
	if (!s_showConsole)
		return;

	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return;

	ImGui::SetNextWindowSize(ImVec2(600, 250), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Console", &s_showConsole)) {
		ImGui::End();
		return;
	}

	// Copy / Clear buttons
	if (ImGui::SmallButton("Copy All")) {
		std::lock_guard<std::mutex> lock(s_consoleMutex);
		ImGui::SetClipboardText(s_consoleBuffer.c_str());
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Clear")) {
		std::lock_guard<std::mutex> lock(s_consoleMutex);
		s_consoleBuffer.clear();
	}

	// Output area
	float inputHeight = ImGui::GetFrameHeightWithSpacing();
	if (ImGui::BeginChild("##consoleout", ImVec2(0, -inputHeight), ImGuiChildFlags_Border)) {
		std::lock_guard<std::mutex> lock(s_consoleMutex);
		ImGui::TextUnformatted(s_consoleBuffer.c_str(), s_consoleBuffer.c_str() + s_consoleBuffer.size());

		// Auto-scroll to bottom
		if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.0f)
			ImGui::SetScrollHereY(1.0f);
	}
	ImGui::EndChild();

	// Input line
	ImGui::SetNextItemWidth(-1);
	ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue
		| ImGuiInputTextFlags_CallbackHistory;

	if (ImGui::InputText("##cmdinput", s_cmdInputBuf, sizeof(s_cmdInputBuf), inputFlags, ConsoleInputCallback)) {
		if (s_cmdInputBuf[0]) {
			// Add to history
			s_cmdHistory.push_back(s_cmdInputBuf);
			s_cmdHistoryPos = -1;

			// Queue command
			dbg->QueueCommand(s_cmdInputBuf, true);
			s_cmdInputBuf[0] = 0;
		}
		// Re-focus input
		ImGui::SetKeyboardFocusHere(-1);
	}

	ImGui::End();
}

// ============= Breakpoints Window =============

static void DrawBreakpoints() {
	if (!s_showBreakpoints)
		return;

	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return;

	ImGui::SetNextWindowSize(ImVec2(350, 200), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Breakpoints", &s_showBreakpoints)) {
		ImGui::End();
		return;
	}

	// Add breakpoint input
	ImGui::SetNextItemWidth(80);
	bool addBp = ImGui::InputText("##bpaddr", s_bpAddrBuf, sizeof(s_bpAddrBuf),
		ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue);
	ImGui::SameLine();
	if ((ImGui::Button("Add##bp") || addBp) && s_bpAddrBuf[0]) {
		unsigned int addr;
		if (sscanf(s_bpAddrBuf, "%x", &addr) == 1) {
			dbg->ToggleBreakpoint(addr & 0xFFFF);
			s_bpAddrBuf[0] = 0;
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Clear All")) {
		dbg->ClearAllBreakpoints();
	}

	ImGui::Separator();

	vdfastvector<uint32> bps;
	dbg->GetBreakpointList(bps);

	if (bps.empty()) {
		ImGui::TextDisabled("No breakpoints set");
	} else {
		for (uint32 useridx : bps) {
			ATDebuggerBreakpointInfo info;
			if (!dbg->GetBreakpointInfo(useridx, info))
				continue;

			ImGui::PushID((int)useridx);

			// Type indicator
			const char *type = "?";
			if (info.mbBreakOnPC) type = "PC";
			else if (info.mbBreakOnRead) type = "RD";
			else if (info.mbBreakOnWrite) type = "WR";
			else if (info.mbBreakOnInsn) type = "IN";

			ImGui::Text("[%d] %s $%04X", info.mNumber, type, info.mAddress);

			if (info.mLength > 1) {
				ImGui::SameLine();
				ImGui::Text("len=%u", info.mLength);
			}

			ImGui::SameLine();
			if (ImGui::SmallButton("X")) {
				dbg->ClearUserBreakpoint(useridx, true);
			}

			ImGui::PopID();
		}
	}

	ImGui::End();
}

// ============= Watch Window =============

static void DrawWatch() {
	if (!s_showWatch)
		return;

	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return;

	IATDebugTarget *target = dbg->GetTarget();

	ImGui::SetNextWindowSize(ImVec2(320, 200), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Watch", &s_showWatch)) {
		ImGui::End();
		return;
	}

	// Add watch input
	ImGui::SetNextItemWidth(80);
	bool addWatch = ImGui::InputText("##waddr", s_watchAddrBuf, sizeof(s_watchAddrBuf),
		ImGuiInputTextFlags_CharsHexadecimal | ImGuiInputTextFlags_EnterReturnsTrue);
	ImGui::SameLine();

	static int s_watchMode = 0;  // 0=byte, 1=word
	ImGui::SetNextItemWidth(60);
	const char *modes[] = { "Byte", "Word" };
	ImGui::Combo("##wmode", &s_watchMode, modes, 2);
	ImGui::SameLine();

	if ((ImGui::Button("Add##w") || addWatch) && s_watchAddrBuf[0]) {
		unsigned int addr;
		if (sscanf(s_watchAddrBuf, "%x", &addr) == 1) {
			int len = (s_watchMode == 1) ? 2 : 1;
			dbg->AddWatch(addr & 0xFFFF, len);
			s_watchAddrBuf[0] = 0;
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Clear All##w")) {
		dbg->ClearAllWatches();
	}

	ImGui::Separator();

	// Display watches
	for (int idx = 0; idx < 64; idx++) {
		ATDebuggerWatchInfo info;
		if (!dbg->GetWatchInfo(idx, info))
			continue;

		ImGui::PushID(idx);

		uint16 addr = (uint16)info.mAddress;
		if (info.mMode == ATDebuggerWatchMode::ByteAtAddress) {
			uint8 val = target ? target->DebugReadByte(addr) : 0;
			ImGui::Text("[%d] $%04X: %02X (%3d)", idx, addr, val, val);
		} else if (info.mMode == ATDebuggerWatchMode::WordAtAddress) {
			uint8 lo = target ? target->DebugReadByte(addr) : 0;
			uint8 hi = target ? target->DebugReadByte((addr + 1) & 0xFFFF) : 0;
			uint16 val = lo | ((uint16)hi << 8);
			ImGui::Text("[%d] $%04X: %04X (%5d)", idx, addr, val, val);
		} else {
			ImGui::Text("[%d] (expr)", idx);
		}

		ImGui::SameLine();
		if (ImGui::SmallButton("X")) {
			dbg->ClearWatch(idx);
		}

		ImGui::PopID();
	}

	ImGui::End();
}

// ============= Call Stack Window =============

static void DrawCallStack() {
	if (!s_showCallStack)
		return;

	ImGui::SetNextWindowSize(ImVec2(340, 250), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Call Stack", &s_showCallStack)) {
		ImGui::End();
		return;
	}

	IATDebugger *dbg = ATGetDebugger();
	IATDebuggerSymbolLookup *dbs = ATGetDebuggerSymbolLookup();

	if (!dbg || !s_pClient || !s_pClient->mbStateValid) {
		ImGui::TextDisabled("No debugger state available");
		ImGui::End();
		return;
	}

	ATCallStackFrame frames[16];
	uint32 n = dbg->GetCallStack(frames, 16);
	uint32 framePC = dbg->GetFrameExtPC();

	for (uint32 i = 0; i < n; ++i) {
		const ATCallStackFrame& fr = frames[i];

		ATSymbol sym {};
		const char *symname = "";
		if (dbs)
			dbs->LookupSymbol(fr.mPC, kATSymbol_Execute, sym);
		if (sym.mpName)
			symname = sym.mpName;

		bool isCurrent = ((framePC ^ fr.mPC) & 0xFFFF) == 0;
		char label[128];
		snprintf(label, sizeof(label), "%c%04X: %c%04X  %s",
			isCurrent ? '>' : ' ',
			fr.mSP,
			(fr.mP & 0x04) ? '*' : ' ',
			fr.mPC,
			symname);

		if (ImGui::Selectable(label, isCurrent)) {
			dbg->SetFramePC(fr.mPC);
			// Navigate disassembly to this frame
			s_disasmAddr = fr.mPC;
			s_disasmFollowPC = false;
			snprintf(s_disasmAddrBuf, sizeof(s_disasmAddrBuf), "%04X", fr.mPC);
		}
	}

	if (n == 0)
		ImGui::TextDisabled("(empty)");

	ImGui::End();
}

// ============= Visibility accessors =============

bool& ATImGuiDebuggerShowRegisters() { return s_showRegisters; }
bool& ATImGuiDebuggerShowDisassembly() { return s_showDisassembly; }
bool& ATImGuiDebuggerShowMemory() { return s_showMemory; }
bool& ATImGuiDebuggerShowConsole() { return s_showConsole; }
bool& ATImGuiDebuggerShowBreakpoints() { return s_showBreakpoints; }
bool& ATImGuiDebuggerShowWatch() { return s_showWatch; }
bool& ATImGuiDebuggerShowCallStack() { return s_showCallStack; }

// ============= Main Draw =============

void ATImGuiDebuggerDrawWindows() {
	DrawRegisters();
	DrawDisassembly();
	DrawMemory();
	DrawConsole();
	DrawBreakpoints();
	DrawWatch();
	DrawCallStack();
}

void ATImGuiDebuggerDraw() {
	DrawToolbar();
	ATImGuiDebuggerDrawWindows();
}
