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
static bool s_showHistory = false;

// Console search state
static char s_consoleSearchBuf[128] = "";
static bool s_consoleSearchActive = false;
static bool s_consoleAutoScroll = true;

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
			ImGui::MenuItem("History", nullptr, &s_showHistory);
			ImGui::EndMenu();
		}

		// CPU target selector
		{
			vdfastvector<IATDebugTarget *> targets;
			dbg->GetTargetList(targets);
			if (targets.size() > 1) {
				ImGui::SameLine(0, 16);
				uint32 curIdx = dbg->GetTargetIndex();
				IATDebugTarget *curTarget = dbg->GetTarget();
				const char *curName = curTarget ? curTarget->GetName() : "?";

				ImGui::SetNextItemWidth(100);
				if (ImGui::BeginCombo("##cputarget", curName)) {
					for (uint32 i = 0; i < (uint32)targets.size(); i++) {
						bool selected = (i == curIdx);
						if (ImGui::Selectable(targets[i]->GetName(), selected))
							dbg->SetTarget(i);
						if (selected)
							ImGui::SetItemDefaultFocus();
					}
					ImGui::EndCombo();
				}
			}
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

	// Address or symbol input
	ImGui::SetNextItemWidth(120);
	bool goPressed = ImGui::InputText("##addr", s_disasmAddrBuf, sizeof(s_disasmAddrBuf),
		ImGuiInputTextFlags_EnterReturnsTrue);
	ImGui::SameLine();
	goPressed = ImGui::Button("Go") || goPressed;

	if (goPressed && s_disasmAddrBuf[0]) {
		// Try hex address first, then symbol lookup
		unsigned int addr;
		if (sscanf(s_disasmAddrBuf, "%x", &addr) == 1) {
			s_disasmAddr = addr & 0xFFFF;
			s_disasmFollowPC = false;
		} else {
			sint32 symAddr = dbg->ResolveSymbol(s_disasmAddrBuf, true, true, false);
			if (symAddr >= 0) {
				s_disasmAddr = (uint16)symAddr;
				s_disasmFollowPC = false;
				snprintf(s_disasmAddrBuf, sizeof(s_disasmAddrBuf), "%04X",
					s_disasmAddr);
			}
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

		IATDebuggerSymbolLookup *dbs = ATGetDebuggerSymbolLookup();

		for (int i = 0; i < 30; i++) {
			ATCPUHistoryEntry hent;
			ATDisassembleCaptureInsnContext(target, addr, 0, hent);

			line.clear();
			ATDisasmResult result = ATDisassembleInsn(line, target, state.mExecMode, hent,
				true, false, true, true, true, false, false, true, true, false);

			// Show symbol label if this address has one
			if (dbs) {
				ATSymbol sym {};
				if (dbs->LookupSymbol(addr, kATSymbol_Execute, sym) && sym.mpName
					&& sym.mOffset == addr) {
					ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f),
						"    %s:", sym.mpName);
				}
			}

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
			// Show address and symbol name
			ATSymbol ctxSym {};
			IATDebuggerSymbolLookup *dbs = ATGetDebuggerSymbolLookup();
			if (dbs)
				dbs->LookupSymbol(s_disasmContextAddr, kATSymbol_Execute, ctxSym);
			if (ctxSym.mpName)
				ImGui::Text("$%04X (%s)", s_disasmContextAddr, ctxSym.mpName);
			else
				ImGui::Text("$%04X", s_disasmContextAddr);
			ImGui::Separator();

			bool hasBP = dbg->IsBreakpointAtPC(s_disasmContextAddr);
			if (ImGui::MenuItem(hasBP ? "Remove Breakpoint" : "Set Breakpoint")) {
				dbg->ToggleBreakpoint(s_disasmContextAddr);
			}

			if (ImGui::MenuItem("Run to Cursor", nullptr, false, !dbg->IsRunning())) {
				// Set a one-shot breakpoint and run
				char cmd[64];
				snprintf(cmd, sizeof(cmd), "g %04X", s_disasmContextAddr);
				dbg->QueueCommand(cmd, false);
			}

			if (ImGui::MenuItem("Set PC")) {
				dbg->SetPC(s_disasmContextAddr);
			}

			ImGui::Separator();

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

	// Address or symbol input
	ImGui::SetNextItemWidth(120);
	bool memGo = ImGui::InputText("##memaddr", s_memoryAddrBuf, sizeof(s_memoryAddrBuf),
		ImGuiInputTextFlags_EnterReturnsTrue);
	ImGui::SameLine();
	memGo = ImGui::Button("Go##mem") || memGo;

	if (memGo && s_memoryAddrBuf[0]) {
		unsigned int addr;
		if (sscanf(s_memoryAddrBuf, "%x", &addr) == 1) {
			s_memoryAddr = addr & 0xFFFF;
		} else {
			IATDebugger *dbgMem = ATGetDebugger();
			if (dbgMem) {
				sint32 symAddr = dbgMem->ResolveSymbol(s_memoryAddrBuf, true, true, false);
				if (symAddr >= 0) {
					s_memoryAddr = (uint16)symAddr;
					snprintf(s_memoryAddrBuf, sizeof(s_memoryAddrBuf), "%04X",
						s_memoryAddr);
				}
			}
		}
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

			ImGui::Separator();

			if (ImGui::MenuItem("Break on Read")) {
				dbg->ToggleAccessBreakpoint(s_disasmContextAddr, false);
			}
			if (ImGui::MenuItem("Break on Write")) {
				dbg->ToggleAccessBreakpoint(s_disasmContextAddr, true);
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

	// Copy / Clear / Search buttons
	if (ImGui::SmallButton("Copy All")) {
		std::lock_guard<std::mutex> lock(s_consoleMutex);
		ImGui::SetClipboardText(s_consoleBuffer.c_str());
	}
	ImGui::SameLine();
	if (ImGui::SmallButton("Clear")) {
		std::lock_guard<std::mutex> lock(s_consoleMutex);
		s_consoleBuffer.clear();
	}
	ImGui::SameLine();
	ImGui::Checkbox("Auto-scroll", &s_consoleAutoScroll);
	ImGui::SameLine();
	if (ImGui::SmallButton("Search"))
		s_consoleSearchActive = !s_consoleSearchActive;

	// Search bar
	if (s_consoleSearchActive) {
		ImGui::SetNextItemWidth(200);
		ImGui::InputText("##conssearch", s_consoleSearchBuf, sizeof(s_consoleSearchBuf));
		ImGui::SameLine();
		ImGui::TextDisabled("(filters output)");
	}

	// Output area
	float inputHeight = ImGui::GetFrameHeightWithSpacing();
	if (ImGui::BeginChild("##consoleout", ImVec2(0, -inputHeight), ImGuiChildFlags_Border)) {
		std::lock_guard<std::mutex> lock(s_consoleMutex);

		if (s_consoleSearchActive && s_consoleSearchBuf[0]) {
			// Filtered display: show only lines containing the search string
			const char *p = s_consoleBuffer.c_str();
			const char *end = p + s_consoleBuffer.size();
			size_t needleLen = strlen(s_consoleSearchBuf);

			while (p < end) {
				const char *nl = (const char *)memchr(p, '\n', end - p);
				if (!nl) nl = end;

				// Case-insensitive substring search
				bool match = false;
				size_t lineLen = nl - p;
				if (lineLen >= needleLen) {
					for (size_t i = 0; i <= lineLen - needleLen; i++) {
						if (strncasecmp(p + i, s_consoleSearchBuf, needleLen) == 0) {
							match = true;
							break;
						}
					}
				}

				if (match)
					ImGui::TextUnformatted(p, nl);

				p = (nl < end) ? nl + 1 : end;
			}
		} else {
			ImGui::TextUnformatted(s_consoleBuffer.c_str(), s_consoleBuffer.c_str() + s_consoleBuffer.size());
		}

		// Auto-scroll to bottom
		if (s_consoleAutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.0f)
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

	// Add breakpoint input (hex address or symbol name)
	ImGui::SetNextItemWidth(120);
	bool addBp = ImGui::InputText("##bpaddr", s_bpAddrBuf, sizeof(s_bpAddrBuf),
		ImGuiInputTextFlags_EnterReturnsTrue);
	ImGui::SameLine();

	// Breakpoint type selector
	static int s_bpType = 0;  // 0=PC, 1=Read, 2=Write
	ImGui::SetNextItemWidth(70);
	const char *bpTypes[] = { "PC", "Read", "Write" };
	ImGui::Combo("##bptype", &s_bpType, bpTypes, 3);
	ImGui::SameLine();

	if ((ImGui::Button("Add##bp") || addBp) && s_bpAddrBuf[0]) {
		unsigned int addr;
		sint32 resolved = -1;
		if (sscanf(s_bpAddrBuf, "%x", &addr) == 1) {
			resolved = (sint32)(addr & 0xFFFF);
		} else {
			resolved = dbg->ResolveSymbol(s_bpAddrBuf, true, true, false);
		}

		if (resolved >= 0) {
			if (s_bpType == 0)
				dbg->ToggleBreakpoint((uint16)resolved);
			else
				dbg->ToggleAccessBreakpoint((uint16)resolved, s_bpType == 2);
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

			// Look up symbol name for the address
			ATSymbol sym {};
			IATDebuggerSymbolLookup *dbs = ATGetDebuggerSymbolLookup();
			if (dbs)
				dbs->LookupSymbol(info.mAddress, info.mbBreakOnPC ? kATSymbol_Execute : kATSymbol_Any, sym);

			if (sym.mpName)
				ImGui::Text("[%d] %s $%04X (%s)", info.mNumber, type, info.mAddress, sym.mpName);
			else
				ImGui::Text("[%d] %s $%04X", info.mNumber, type, info.mAddress);

			if (info.mLength > 1) {
				ImGui::SameLine();
				ImGui::Text("len=%u", info.mLength);
			}

			// Click breakpoint address to go to it in disassembly
			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && info.mbBreakOnPC) {
				s_disasmAddr = info.mAddress;
				s_disasmFollowPC = false;
				snprintf(s_disasmAddrBuf, sizeof(s_disasmAddrBuf), "%04X", info.mAddress);
				s_showDisassembly = true;
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

	// Add watch input (accepts hex address, symbol name, or expression)
	ImGui::SetNextItemWidth(120);
	bool addWatch = ImGui::InputText("##waddr", s_watchAddrBuf, sizeof(s_watchAddrBuf),
		ImGuiInputTextFlags_EnterReturnsTrue);
	ImGui::SameLine();

	static int s_watchMode = 0;  // 0=byte, 1=word
	ImGui::SetNextItemWidth(60);
	const char *modes[] = { "Byte", "Word" };
	ImGui::Combo("##wmode", &s_watchMode, modes, 2);
	ImGui::SameLine();

	if ((ImGui::Button("Add##w") || addWatch) && s_watchAddrBuf[0]) {
		int len = (s_watchMode == 1) ? 2 : 1;
		unsigned int addr;
		if (sscanf(s_watchAddrBuf, "%x", &addr) == 1) {
			dbg->AddWatch(addr & 0xFFFF, len);
			s_watchAddrBuf[0] = 0;
		} else {
			// Try symbol lookup
			sint32 symAddr = dbg->ResolveSymbol(s_watchAddrBuf, true, true, false);
			if (symAddr >= 0) {
				dbg->AddWatch((uint16)symAddr, len);
				s_watchAddrBuf[0] = 0;
			}
		}
	}
	ImGui::SameLine();
	if (ImGui::Button("Clear All##w")) {
		dbg->ClearAllWatches();
	}

	// Quick-eval expression
	static char s_evalBuf[128] = "";
	static std::string s_evalResult;
	ImGui::SetNextItemWidth(200);
	if (ImGui::InputText("Eval##weval", s_evalBuf, sizeof(s_evalBuf),
		ImGuiInputTextFlags_EnterReturnsTrue) && s_evalBuf[0]) {
		try {
			sint32 val = dbg->EvaluateThrow(s_evalBuf);
			char res[64];
			snprintf(res, sizeof(res), "= $%04X (%d)", (uint16)(val & 0xFFFF), val);
			s_evalResult = res;
		} catch (...) {
			s_evalResult = "(error)";
		}
	}
	if (!s_evalResult.empty()) {
		ImGui::SameLine();
		ImGui::Text("%s", s_evalResult.c_str());
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
		} else if (info.mpExpr) {
			auto result = dbg->Evaluate(info.mpExpr);
			if (result.first) {
				sint32 val = result.second;
				if (info.mMode == ATDebuggerWatchMode::ExprHex8)
					ImGui::Text("[%d] expr: %02X (%d)", idx, val & 0xFF, val);
				else if (info.mMode == ATDebuggerWatchMode::ExprHex16)
					ImGui::Text("[%d] expr: %04X (%d)", idx, val & 0xFFFF, val);
				else if (info.mMode == ATDebuggerWatchMode::ExprHex32)
					ImGui::Text("[%d] expr: %08X (%d)", idx, val, val);
				else
					ImGui::Text("[%d] expr: %d ($%04X)", idx, val, val & 0xFFFF);
			} else {
				ImGui::TextDisabled("[%d] expr: (error)", idx);
			}
		} else {
			ImGui::TextDisabled("[%d] (unknown)", idx);
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

// ============= History Window =============

static void DrawHistory() {
	if (!s_showHistory)
		return;

	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return;

	IATDebugTarget *target = dbg->GetTarget();
	if (!target)
		return;

	ImGui::SetNextWindowSize(ImVec2(500, 400), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("History", &s_showHistory)) {
		ImGui::End();
		return;
	}

	// Get the history interface
	IATDebugTargetHistory *hist = nullptr;
	if (target)
		hist = static_cast<IATDebugTargetHistory *>(
			target->AsInterface(IATDebugTargetHistory::kTypeID));

	if (!hist) {
		ImGui::TextDisabled("History not available for this target");
		ImGui::End();
		return;
	}

	if (!hist->GetHistoryEnabled()) {
		ImGui::TextDisabled("History recording is disabled");
		ImGui::SameLine();
		if (ImGui::SmallButton("Enable")) {
			hist->SetHistoryEnabled(true);
		}
		ImGui::End();
		return;
	}

	if (ImGui::SmallButton("Disable Recording")) {
		hist->SetHistoryEnabled(false);
	}

	ImGui::Separator();

	auto range = hist->GetHistoryRange();
	uint32 rangeStart = range.first;
	uint32 rangeEnd = range.second;
	uint32 count = rangeEnd - rangeStart;

	if (count == 0) {
		ImGui::TextDisabled("No history recorded yet");
		ImGui::End();
		return;
	}

	ImGui::Text("History: %u entries", count);

	if (ImGui::BeginChild("##histlines", ImVec2(0, 0), ImGuiChildFlags_None)) {
		IATDebuggerSymbolLookup *dbs = ATGetDebuggerSymbolLookup();
		ATDebugDisasmMode disasmMode = target->GetDisasmMode();

		// Show the most recent entries (up to 128)
		uint32 showCount = count < 128 ? count : 128;
		uint32 startIdx = rangeEnd - showCount;

		const ATCPUHistoryEntry *hparray[1];
		VDStringA line;

		for (uint32 i = 0; i < showCount; i++) {
			uint32 idx = startIdx + i;
			if (hist->ExtractHistory(hparray, idx, 1) == 0)
				continue;

			const ATCPUHistoryEntry& he = *hparray[0];

			// Disassemble the instruction
			line.clear();
			ATDisassembleInsn(line, target,
				disasmMode, he,
				true, false, true, true, true, false, false, true, true, false);

			// Look up symbol
			ATSymbol sym {};
			if (dbs)
				dbs->LookupSymbol(he.mPC, kATSymbol_Execute, sym);

			bool isNewest = (i == showCount - 1);
			ImVec4 color = isNewest
				? ImVec4(1.0f, 1.0f, 0.3f, 1.0f)
				: ImVec4(0.7f, 0.7f, 0.7f, 1.0f);

			ImGui::PushStyleColor(ImGuiCol_Text, color);

			char label[320];
			if (sym.mpName && sym.mOffset == he.mPC)
				snprintf(label, sizeof(label), "%6u %s  (%s)##h%u",
					he.mCycle, line.c_str(), sym.mpName, i);
			else
				snprintf(label, sizeof(label), "%6u %s##h%u",
					he.mCycle, line.c_str(), i);

			if (ImGui::Selectable(label, false)) {
				// Click to navigate disassembly to this address
				s_disasmAddr = he.mPC;
				s_disasmFollowPC = false;
				snprintf(s_disasmAddrBuf, sizeof(s_disasmAddrBuf), "%04X", he.mPC);
				s_showDisassembly = true;
			}

			ImGui::PopStyleColor();
		}

		// Auto-scroll to bottom to show most recent
		if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.0f)
			ImGui::SetScrollHereY(1.0f);
	}
	ImGui::EndChild();

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
bool& ATImGuiDebuggerShowHistory() { return s_showHistory; }

// ============= Main Draw =============

void ATImGuiDebuggerDrawWindows() {
	DrawRegisters();
	DrawDisassembly();
	DrawMemory();
	DrawConsole();
	DrawBreakpoints();
	DrawWatch();
	DrawCallStack();
	DrawHistory();
}

void ATImGuiDebuggerDraw() {
	DrawToolbar();
	ATImGuiDebuggerDrawWindows();
}
