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

// Memory window state
static uint32 s_memoryAddr = 0;
static char s_memoryAddrBuf[16] = "0000";
static int s_memEditByteIdx = -1;  // -1 = no byte being edited
static char s_memEditBuf[4] = "";

// Breakpoint add state
static char s_bpAddrBuf[16] = "";

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

	const auto& state = s_pClient->mState;
	const auto& regs = state.mExecState.m6502;

	// 6502 registers
	ImGui::Text("PC: %04X", state.mPC);
	ImGui::SameLine(140);
	ImGui::Text("Cycle: %u", state.mCycle);

	ImGui::Separator();

	ImGui::Text(" A: %02X (%3d)", regs.mA, regs.mA);
	ImGui::Text(" X: %02X (%3d)", regs.mX, regs.mX);
	ImGui::Text(" Y: %02X (%3d)", regs.mY, regs.mY);
	ImGui::Text(" S: %02X", regs.mS);

	ImGui::Separator();

	// Processor status flags
	uint8 p = regs.mP;
	ImGui::Text("P: %02X  ", p);
	ImGui::SameLine();

	const char flags[] = "NV-BDIZC";
	char flagStr[9];
	for (int i = 0; i < 8; i++) {
		flagStr[i] = (p & (0x80 >> i)) ? flags[i] : '.';
	}
	flagStr[8] = 0;

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

			// Selectable line — click to toggle breakpoint
			char label[256];
			snprintf(label, sizeof(label), "%c %s", marker, line.c_str());

			ImGui::PushStyleColor(ImGuiCol_Text, color);
			if (ImGui::Selectable(label, false, ImGuiSelectableFlags_None)) {
				dbg->ToggleBreakpoint(addr);
			}
			ImGui::PopStyleColor();

			addr = result.mNextPC;
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

			// Address
			ImGui::Text("%04X:", rowAddr);

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
					char label[8];
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

// ============= Visibility accessors =============

bool& ATImGuiDebuggerShowRegisters() { return s_showRegisters; }
bool& ATImGuiDebuggerShowDisassembly() { return s_showDisassembly; }
bool& ATImGuiDebuggerShowMemory() { return s_showMemory; }
bool& ATImGuiDebuggerShowConsole() { return s_showConsole; }
bool& ATImGuiDebuggerShowBreakpoints() { return s_showBreakpoints; }

// ============= Main Draw =============

void ATImGuiDebuggerDrawWindows() {
	DrawRegisters();
	DrawDisassembly();
	DrawMemory();
	DrawConsole();
	DrawBreakpoints();
}

void ATImGuiDebuggerDraw() {
	DrawToolbar();
	ATImGuiDebuggerDrawWindows();
}
