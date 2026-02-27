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
#include <vd2/system/filesys.h>
#include <vd2/system/text.h>
#include <vd2/system/time.h>
#include <at/atcore/address.h>
#include <at/atcore/profile.h>
#include <at/atdebugger/target.h>
#include <at/atcpu/execstate.h>
#include <at/atcpu/history.h>

#include <imgui.h>
#include <debugger_imgui.h>

#include "debugger.h"
#include "disasm.h"

// Forward declarations needed by simulator.h transitives
class ATIRQController;

#include "simulator.h"
#include "profiler.h"
#include "printeroutput.h"
#include "trace.h"
#include "debugdisplay.h"
#include "memorymanager.h"
#include "antic.h"
#include "gtia.h"
#include <display_sdl2.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <functional>
#include <iomanip>
#include <map>
#include <mutex>
#include <numeric>
#include <set>
#include <vector>
#include <string>

extern ATSimulator g_sim;

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
		// Detect transition from running to stopped (breakpoint hit)
		if (mbStateValid && mState.mbRunning && !state.mbRunning)
			mbJustBroke = true;

		mState = state;
		mbStateValid = true;
	}

	void OnDebuggerEvent(ATDebugEvent eventId) override {
		if (eventId == kATDebugEvent_BreakpointsChanged)
			mbBreakpointsChanged = true;
		if (eventId == kATDebugEvent_SymbolsChanged)
			mbSymbolsChanged = true;
	}

	ATDebuggerSystemState mState {};
	bool mbStateValid = false;
	bool mbBreakpointsChanged = false;
	bool mbSymbolsChanged = false;
	bool mbJustBroke = false;
};

static ATImGuiDebuggerClient *s_pClient = nullptr;

bool ATImGuiDebuggerDidBreak() {
	if (s_pClient && s_pClient->mbJustBroke) {
		s_pClient->mbJustBroke = false;
		return true;
	}
	return false;
}

// Window visibility state
static bool s_showRegisters = true;
static bool s_showDisassembly = true;
static bool s_showMemory = true;
static bool s_showConsole = true;
static bool s_showBreakpoints = true;
static bool s_showWatch = false;
static bool s_showCallStack = false;
static bool s_showHistory = false;
static bool s_showSourceCode = false;
static bool s_showPrinterOutput = false;

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
static uint32 s_disasmContextEA = 0xFFFFFFFF;  // effective address of context instruction
static char s_disasmContextText[128] = "";     // disassembly text of context line

// Disassembly state
static uint32 s_disasmAddr = 0;
static bool s_disasmFollowPC = true;
static bool s_disasmShowBytes = true;
static bool s_disasmShowLabels = true;
static char s_disasmAddrBuf[16] = "0000";

// Source code window state
struct SourceFileEntry {
	VDStringW mPath;
	uint32 mNumLines;
};
static std::vector<SourceFileEntry> s_sourceFiles;
static int s_sourceSelectedFile = -1;
static std::vector<std::string> s_sourceLines;
static std::map<int, uint32> s_lineToAddr;       // 0-based line → CPU address
static std::map<uint32, int> s_addrToLine;        // CPU address → 0-based line
static uint32 s_sourceModuleId = 0;
static uint16 s_sourceFileId = 0;
static int s_sourcePCLine = -1;                   // 0-based, -1 = none
static int s_sourceFrameLine = -1;
static bool s_sourceNeedsFileList = true;
static bool s_sourceNeedsRebind = true;
static bool s_sourceScrollToPC = false;

// Printer output window state
static std::string s_printerText;
static size_t s_printerLastOffset = 0;
static bool s_printerNeedsRefresh = true;
static int s_printerSelectedOutput = 0;

// Profiler window state
static bool s_showProfiler = false;
static bool s_profilerRunning = false;
static ATProfileMode s_profilerMode = kATProfileMode_Insns;
static ATProfileCounterMode s_profilerC1 = kATProfileCounterMode_None;
static ATProfileCounterMode s_profilerC2 = kATProfileCounterMode_None;
static ATProfileSession s_profilerSession;
static bool s_profilerHasData = false;
static vdrefptr<ATProfileMergedFrame> s_profilerMerged;

struct ProfileEntry {
	uint32 addr;
	std::string sym;
	uint32 calls;
	uint32 insns;
	uint32 cycles;
	uint32 unhaltedCycles;
	uint32 context;
	uint32 counters[2];
};

static std::vector<ProfileEntry> s_profilerEntries;
static int s_profilerSortCol = 4; // default: cycles
static bool s_profilerSortAsc = false;
static std::set<int> s_profilerSelectedRows;
static int s_profilerLastClickedRow = -1;

// Timeline state
static int s_profilerSelStart = 0;
static int s_profilerSelEnd = 0;
static int s_profilerHoverFrame = -1;
static float s_profilerTimelineZoom = 4.0f;
static uint32 s_profilerTimelineVRange = 1;

// Boundary rule state
static ATProfileBoundaryRule s_profilerBoundaryRule = kATProfileBoundaryRule_None;
static char s_profilerBoundaryAddr[64] = "";
static char s_profilerBoundaryAddr2[64] = "";
static bool s_profilerGlobalAddresses = false;

// Profiler detail (function detail) state
static bool s_showProfilerDetail = false;
static uint32 s_profilerDetailAddr = 0;
static std::string s_profilerDetailName;

struct ProfileDetailEntry {
	uint32 addr;
	uint32 cycles;
	uint32 insns;
	uint32 unhaltedCycles;
	std::string disasm;
};
static std::vector<ProfileDetailEntry> s_profilerDetailEntries;
static void ProfilerDetailBuild(uint32 funcAddr);

// Trace viewer state
static bool s_showTrace = false;
static bool s_traceRecording = false;
static bool s_traceHasData = false;
static bool s_traceOptCpu = true;
static bool s_traceOptVideo = true;
static bool s_traceOptBasic = false;
static double s_traceViewStart = 0.0;
static double s_traceViewEnd = 1.0;
static double s_traceTotalDuration = 0.0;
static int s_traceHoverChannel = -1;
static vdrefptr<ATTraceCollection> s_traceCollection;

// Debug display state
static bool s_showDebugDisplay = false;
static ATDebugDisplay *s_debugDisplay = nullptr;
static GLuint s_debugDisplayTex = 0;

// Performance overlay state
static bool s_showPerformance = false;

class ATImGuiProfiler : public IATProfiler {
public:
	static constexpr int kWidth = 256;
	static constexpr int kHeight = 200;

	void OnEvent(ATProfileEvent event) override;
	void BeginRegion(ATProfileRegion region) override;
	void EndRegion(ATProfileRegion region) override;

	// Per-column, per-region accumulated pixel heights for the rolling display
	struct Column {
		int regionPixels[kATProfileRegionCount] {};
	};

	Column mColumns[kWidth] {};
	int mX = 0;  // current write column (wraps at kWidth)
	int mRegionStackHt = 0;
	ATProfileRegion mRegionStack[64] {};
	uint64 mFrameStartTime = 0;
	uint64 mRegionStartTime = 0;
	double mTicksToPixels = 0;
};

static ATImGuiProfiler *s_pProfiler = nullptr;

void ATImGuiProfiler::OnEvent(ATProfileEvent event) {
	if (event != kATProfileEvent_BeginFrame)
		return;

	mRegionStackHt = 0;
	mX = (mX + 1) & (kWidth - 1);
	mFrameStartTime = VDGetPreciseTick();
	mRegionStartTime = mFrameStartTime;
	mTicksToPixels = VDGetPreciseSecondsPerTick() * (double)kHeight * 30.0;

	// Clear the next column
	Column& col = mColumns[mX];
	for (int i = 0; i < kATProfileRegionCount; i++)
		col.regionPixels[i] = 0;
}

void ATImGuiProfiler::BeginRegion(ATProfileRegion region) {
	if (mRegionStackHt < 64) {
		// End timing for the previous region
		if (mRegionStackHt > 0) {
			uint64 now = VDGetPreciseTick();
			int pixels = (int)((double)(now - mRegionStartTime) * mTicksToPixels);
			if (pixels > 0)
				mColumns[mX].regionPixels[mRegionStack[mRegionStackHt - 1]] += pixels;
			mRegionStartTime = now;
		}
		mRegionStack[mRegionStackHt++] = region;
		mRegionStartTime = VDGetPreciseTick();
	}
}

void ATImGuiProfiler::EndRegion(ATProfileRegion region) {
	if (mRegionStackHt > 0) {
		uint64 now = VDGetPreciseTick();
		int pixels = (int)((double)(now - mRegionStartTime) * mTicksToPixels);
		if (pixels > 0)
			mColumns[mX].regionPixels[mRegionStack[mRegionStackHt - 1]] += pixels;
		--mRegionStackHt;
		mRegionStartTime = now;
	}
}
static std::vector<uint32> s_debugDisplayRGBA;
static int s_debugDisplayMode = 0;
static int s_debugDisplayPalette = 0;

void ATImGuiDebuggerInit() {
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return;

	s_pClient = new ATImGuiDebuggerClient;
	dbg->AddClient(s_pClient, true);
}

void ATImGuiDebuggerShutdown() {
	// Stop profiling if running
	if (s_profilerRunning) {
		ATCPUProfiler *prof = g_sim.GetProfiler();
		if (prof && prof->IsRunning())
			prof->End();
		s_profilerRunning = false;
		g_sim.SetProfilingEnabled(false);
	}
	s_profilerMerged.clear();
	s_profilerEntries.clear();

	// Cleanup trace viewer
	if (s_traceRecording) {
		g_sim.StopTracing();
		s_traceRecording = false;
	}
	s_traceHasData = false;
	s_traceCollection.clear();

	// Cleanup performance overlay
	if (s_pProfiler) {
		if (g_pATProfiler == s_pProfiler)
			g_pATProfiler = nullptr;
		delete s_pProfiler;
		s_pProfiler = nullptr;
	}

	// Cleanup debug display
	if (s_debugDisplay) {
		s_debugDisplay->Shutdown();
		delete s_debugDisplay;
		s_debugDisplay = nullptr;
	}
	if (s_debugDisplayTex) {
		glDeleteTextures(1, &s_debugDisplayTex);
		s_debugDisplayTex = 0;
	}
	s_debugDisplayRGBA.clear();

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

	// Track previous register values for change highlighting
	static uint16 s_prevPC = 0;
	static uint8 s_prevA = 0, s_prevX = 0, s_prevY = 0, s_prevS = 0, s_prevP = 0;
	static bool s_prevValid = false;

	// Helper: editable hex register field (double-click to edit)
	static int s_regEditId = -1;
	static char s_regEditBuf[8] = "";

	auto EditableReg = [&](const char *name, int id, uint32 val, int hexDigits, uint32 prevVal) {
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
			bool changed = s_prevValid && (val != prevVal);
			if (changed)
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.5f, 0.2f, 1.0f));
			if (hexDigits == 4)
				ImGui::Text("%s: %04X", name, val);
			else if (hexDigits == 2)
				ImGui::Text("%s: %02X (%3d)", name, val, val);
			if (changed)
				ImGui::PopStyleColor();
			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
				s_regEditId = id;
				snprintf(s_regEditBuf, sizeof(s_regEditBuf),
					hexDigits == 4 ? "%04X" : "%02X", val);
			}
		}
	};

	// 6502 registers (double-click to edit, orange = changed)
	EditableReg("PC", 0, state.mPC, 4, s_prevPC);
	ImGui::SameLine(140);
	ImGui::Text("Cycle: %u", state.mCycle);

	ImGui::Separator();

	EditableReg(" A", 1, regs.mA, 2, s_prevA);
	EditableReg(" X", 2, regs.mX, 2, s_prevX);
	EditableReg(" Y", 3, regs.mY, 2, s_prevY);
	EditableReg(" S", 4, regs.mS, 2, s_prevS);
	// Stack peek: show top 8 bytes above SP
	if (target) {
		ImGui::SameLine(140);
		char stackStr[32];
		int pos = 0;
		for (int i = 1; i <= 6 && (regs.mS + i) <= 0xFF; i++) {
			uint8 b = target->DebugReadByte(0x0100 + regs.mS + i);
			pos += snprintf(stackStr + pos, sizeof(stackStr) - pos, "%02X ", b);
		}
		if (pos > 0)
			ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.8f, 1.0f), "[%s]", stackStr);
	}

	ImGui::Separator();

	// Processor status flags (double-click to edit)
	uint8 p = regs.mP;
	EditableReg(" P", 5, p, 2, s_prevP);
	ImGui::SameLine();

	const char flags[] = "NV-BDIZC";

	// Color active flags (orange if changed since last stop)
	for (int i = 0; i < 8; i++) {
		if (i > 0) ImGui::SameLine(0, 0);
		bool active = (p & (0x80 >> i)) != 0;
		bool flagChanged = s_prevValid && ((p ^ s_prevP) & (0x80 >> i));
		if (flagChanged && active)
			ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "%c", flags[i]);
		else if (active)
			ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.3f, 1.0f), "%c", flags[i]);
		else if (flagChanged)
			ImGui::TextColored(ImVec4(0.7f, 0.4f, 0.2f, 1.0f), ".");
		else
			ImGui::TextColored(ImVec4(0.4f, 0.4f, 0.4f, 1.0f), ".");
	}

	// Snapshot registers when user steps/runs (stopped→running transition)
	// so that when execution stops again, we can highlight what changed.
	static bool s_wasRunning = false;
	bool running = dbg && dbg->IsRunning();
	if (running && !s_wasRunning) {
		// About to run — save current registers as "previous"
		s_prevPC = state.mPC;
		s_prevA = regs.mA;
		s_prevX = regs.mX;
		s_prevY = regs.mY;
		s_prevS = regs.mS;
		s_prevP = regs.mP;
		s_prevValid = true;
	}
	s_wasRunning = running;

	// Hardware register readouts via debug reads
	if (target) {
		ImGui::Separator();
		static bool s_hwExpanded = true;
		if (ImGui::CollapsingHeader("Hardware", s_hwExpanded ? ImGuiTreeNodeFlags_DefaultOpen : 0)) {
			s_hwExpanded = true;
			uint8 vcount = target->DebugReadByte(0xD40B);
			uint8 nmist  = target->DebugReadByte(0xD40F);
			uint8 dmactl = target->DebugReadByte(0xD400);
			uint8 dlistl = target->DebugReadByte(0xD402);
			uint8 dlisth = target->DebugReadByte(0xD403);
			uint16 dlist = dlistl | ((uint16)dlisth << 8);
			ImGui::Text("VCOUNT:%3d  NMIST:%02X  DMA:%02X", vcount, nmist, dmactl);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("ANTIC: VCOUNT=$D40B NMIST=$D40F DMACTL=$D400");
			ImGui::Text("DLIST: %04X", dlist);
			if (ImGui::IsItemHovered()) {
				ImGui::SetTooltip("ANTIC: DLISTL=$D402 DLISTH=$D403\nClick to view in memory");
			}
			if (ImGui::IsItemClicked()) {
				s_memoryAddr = dlist;
				snprintf(s_memoryAddrBuf, sizeof(s_memoryAddrBuf), "%04X", dlist);
				s_showMemory = true;
			}

			uint8 irqst  = target->DebugReadByte(0xD20E);
			uint8 audctl = target->DebugReadByte(0xD208);
			uint8 skstat = target->DebugReadByte(0xD20F);
			ImGui::Text("IRQST: %02X  AUDCTL:%02X  SK:%02X", irqst, audctl, skstat);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("POKEY: IRQST=$D20E AUDCTL=$D208 SKSTAT=$D20F");

			// POKEY audio channels
			if (ImGui::TreeNode("Audio Channels")) {
				for (int ch = 0; ch < 4; ch++) {
					uint8 audf = target->DebugReadByte(0xD200 + ch * 2);
					uint8 audc = target->DebugReadByte(0xD201 + ch * 2);
					uint8 vol = audc & 0x0F;
					uint8 dist = (audc >> 4) & 0x0E;
					bool volOnly = (audc & 0x10) != 0;
					ImGui::Text("CH%d: F=%02X C=%02X (vol=%d dist=%X%s)",
						ch + 1, audf, audc, vol, dist >> 1, volOnly ? " vol-only" : "");
				}
				ImGui::TreePop();
			}

			uint8 porta = target->DebugReadByte(0xD300);
			uint8 portb = target->DebugReadByte(0xD301);
			uint8 pactl = target->DebugReadByte(0xD302);
			ImGui::Text("PORTA: %02X  PORTB:%02X  PAC:%02X", porta, portb, pactl);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("PIA: PORTA=$D300 PORTB=$D301 PACTL=$D302");

			uint8 prior = target->DebugReadByte(0xD01B);
			uint8 vdelay = target->DebugReadByte(0xD01C);
			uint8 gractl = target->DebugReadByte(0xD01D);
			ImGui::Text("PRIOR:%02X  VDLY:%02X  GCTL:%02X", prior, vdelay, gractl);
			if (ImGui::IsItemHovered())
				ImGui::SetTooltip("GTIA: PRIOR=$D01B VDELAY=$D01C GRACTL=$D01D");

			// GTIA color registers
			if (ImGui::TreeNode("Color Registers")) {
				const char *colorNames[] = {
					"COLPM0", "COLPM1", "COLPM2", "COLPM3",
					"COLPF0", "COLPF1", "COLPF2", "COLPF3", "COLBK"
				};
				for (int i = 0; i < 9; i++) {
					uint8 col = target->DebugReadByte(0xD012 + i);
					// Atari color: high nybble = hue, low nybble = luminance
					uint8 hue = (col >> 4) & 0x0F;
					uint8 lum = col & 0x0F;
					ImGui::Text("%s: %02X (hue=%X lum=%X)", colorNames[i], col, hue, lum);
				}
				ImGui::TreePop();
			}
		}
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
	if (ImGui::SmallButton("PC")) {
		s_disasmAddr = pc;
		s_disasmFollowPC = false;
		snprintf(s_disasmAddrBuf, sizeof(s_disasmAddrBuf), "%04X", pc);
	}
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

	// View options
	ImGui::Checkbox("Bytes", &s_disasmShowBytes);
	ImGui::SameLine();
	ImGui::Checkbox("Labels", &s_disasmShowLabels);

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
			true, false, true, s_disasmShowBytes, s_disasmShowLabels, false, false, true, true, false);
		addr = result.mNextPC;
		preLines++;
	}

	// Now addr should be at or very near s_disasmAddr
	addr = s_disasmAddr;

	if (ImGui::BeginChild("##disasmlines", ImVec2(0, 0), ImGuiChildFlags_None)) {
		// Scroll helper: advance N instructions forward
		auto AdvanceAddr = [&](uint16 startA, int count) -> uint16 {
			uint16 a = startA;
			for (int i = 0; i < count; i++) {
				ATCPUHistoryEntry h;
				ATDisassembleCaptureInsnContext(target, a, 0, h);
				ATDisasmResult r = ATDisassembleInsn(line, target,
					state.mExecMode, h, true, false, true, s_disasmShowBytes, s_disasmShowLabels,
					false, false, true, true, false);
				a = r.mNextPC;
			}
			return a;
		};

		// Scroll wheel: move by ~3 instructions per notch
		if (ImGui::IsWindowHovered()) {
			float wheel = ImGui::GetIO().MouseWheel;
			if (wheel != 0.0f) {
				int lines = (int)(-wheel) * 3;
				if (lines > 0)
					s_disasmAddr = AdvanceAddr(s_disasmAddr, lines);
				else
					s_disasmAddr = (s_disasmAddr + lines) & 0xFFFF;
				s_disasmFollowPC = false;
				snprintf(s_disasmAddrBuf, sizeof(s_disasmAddrBuf), "%04X",
					s_disasmAddr);
			}
		}

		// Keyboard navigation
		if (ImGui::IsWindowFocused()) {
			if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
				s_disasmAddr = AdvanceAddr(s_disasmAddr, 1);
				s_disasmFollowPC = false;
				snprintf(s_disasmAddrBuf, sizeof(s_disasmAddrBuf), "%04X", s_disasmAddr);
			}
			if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
				s_disasmAddr = (s_disasmAddr - 1) & 0xFFFF;
				s_disasmFollowPC = false;
				snprintf(s_disasmAddrBuf, sizeof(s_disasmAddrBuf), "%04X", s_disasmAddr);
			}
			if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) {
				s_disasmAddr = AdvanceAddr(s_disasmAddr, 20);
				s_disasmFollowPC = false;
				snprintf(s_disasmAddrBuf, sizeof(s_disasmAddrBuf), "%04X", s_disasmAddr);
			}
			if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) {
				s_disasmAddr = (s_disasmAddr - 40) & 0xFFFF;
				s_disasmFollowPC = false;
				snprintf(s_disasmAddrBuf, sizeof(s_disasmAddrBuf), "%04X", s_disasmAddr);
			}
		}

		IATDebuggerSymbolLookup *dbs = ATGetDebuggerSymbolLookup();
		uint32 lineEA[30];

		for (int i = 0; i < 30; i++) {
			ATCPUHistoryEntry hent;
			ATDisassembleCaptureInsnContext(target, addr, 0, hent);

			line.clear();
			ATDisasmResult result = ATDisassembleInsn(line, target, state.mExecMode, hent,
				true, false, true, s_disasmShowBytes, s_disasmShowLabels, false, false, true, true, false);

			lineEA[i] = hent.mEA;

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
				s_disasmContextEA = lineEA[i];
				snprintf(s_disasmContextText, sizeof(s_disasmContextText), "%s", line.c_str());
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

			// Follow operand effective address (e.g. JMP $addr, LDA $addr)
			if (s_disasmContextEA != 0xFFFFFFFF && (s_disasmContextEA & 0xFFFF) != s_disasmContextAddr) {
				uint16 ea = (uint16)(s_disasmContextEA & 0xFFFF);
				char eaLabel[48];

				ATSymbol eaSym {};
				if (dbs)
					dbs->LookupSymbol(ea, kATSymbol_Any, eaSym);
				if (eaSym.mpName)
					snprintf(eaLabel, sizeof(eaLabel), "Follow $%04X (%s)", ea, eaSym.mpName);
				else
					snprintf(eaLabel, sizeof(eaLabel), "Follow $%04X", ea);

				if (ImGui::MenuItem(eaLabel)) {
					s_disasmAddr = ea;
					s_disasmFollowPC = false;
					snprintf(s_disasmAddrBuf, sizeof(s_disasmAddrBuf), "%04X", ea);
				}

				char memLabel[48];
				if (eaSym.mpName)
					snprintf(memLabel, sizeof(memLabel), "View $%04X in Memory (%s)", ea, eaSym.mpName);
				else
					snprintf(memLabel, sizeof(memLabel), "View $%04X in Memory", ea);

				if (ImGui::MenuItem(memLabel)) {
					s_memoryAddr = ea;
					snprintf(s_memoryAddrBuf, sizeof(s_memoryAddrBuf), "%04X", ea);
					s_showMemory = true;
				}

				// Quick watch
				char wbLabel[48], wwLabel[48];
				snprintf(wbLabel, sizeof(wbLabel), "Watch Byte $%04X", ea);
				snprintf(wwLabel, sizeof(wwLabel), "Watch Word $%04X", ea);
				if (ImGui::MenuItem(wbLabel)) {
					dbg->AddWatch(ea, 1);
					s_showWatch = true;
				}
				if (ImGui::MenuItem(wwLabel)) {
					dbg->AddWatch(ea, 2);
					s_showWatch = true;
				}
			}

			ImGui::Separator();
			char addrStr[8];
			snprintf(addrStr, sizeof(addrStr), "$%04X", s_disasmContextAddr);
			if (ImGui::MenuItem("Copy Address")) {
				ImGui::SetClipboardText(addrStr);
			}
			if (ImGui::MenuItem("Copy Instruction")) {
				ImGui::SetClipboardText(s_disasmContextText);
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

	// Quick-nav buttons for common memory regions
	ImGui::SameLine(0, 8);
	auto QuickNav = [](const char *label, uint16 addr) {
		if (ImGui::SmallButton(label)) {
			s_memoryAddr = addr;
			snprintf(s_memoryAddrBuf, sizeof(s_memoryAddrBuf), "%04X", addr);
		}
		ImGui::SameLine(0, 2);
	};
	QuickNav("ZP", 0x0000);
	QuickNav("Stk", 0x0100);
	QuickNav("HW", 0xD000);
	ImGui::NewLine();

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

	// Memory search (hex byte pattern)
	static char s_memSearchBuf[64] = "";
	static bool s_memSearchActive = false;
	ImGui::SameLine(0, 8);
	if (ImGui::SmallButton("Find"))
		s_memSearchActive = !s_memSearchActive;
	if (s_memSearchActive) {
		ImGui::SetNextItemWidth(160);
		bool doSearch = ImGui::InputText("##memsearch", s_memSearchBuf, sizeof(s_memSearchBuf),
			ImGuiInputTextFlags_EnterReturnsTrue);
		ImGui::SameLine();
		if (ImGui::SmallButton("Next") || doSearch) {
			// Parse hex bytes from search string
			uint8 pattern[32];
			int patLen = 0;
			const char *p = s_memSearchBuf;
			while (*p && patLen < 32) {
				while (*p == ' ') p++;
				unsigned int b;
				if (sscanf(p, "%2x", &b) == 1) {
					pattern[patLen++] = (uint8)b;
					p += 2;
				} else {
					break;
				}
			}
			if (patLen > 0) {
				// Search from current address + 1
				for (uint32 off = 1; off < 0x10000; off++) {
					uint16 testAddr = (s_memoryAddr + off) & 0xFFFF;
					bool match = true;
					for (int j = 0; j < patLen && match; j++) {
						if (target->DebugReadByte((testAddr + j) & 0xFFFF) != pattern[j])
							match = false;
					}
					if (match) {
						s_memoryAddr = testAddr;
						snprintf(s_memoryAddrBuf, sizeof(s_memoryAddrBuf), "%04X", testAddr);
						break;
					}
				}
			}
		}
		ImGui::SameLine();
		ImGui::TextDisabled("hex bytes (e.g. A9 00 8D)");
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

		// Keyboard navigation (when memory child window is focused)
		if (ImGui::IsWindowFocused()) {
			if (ImGui::IsKeyPressed(ImGuiKey_PageDown)) {
				s_memoryAddr = (s_memoryAddr + 256) & 0xFFFF;
				snprintf(s_memoryAddrBuf, sizeof(s_memoryAddrBuf), "%04X", s_memoryAddr);
			}
			if (ImGui::IsKeyPressed(ImGuiKey_PageUp)) {
				s_memoryAddr = (s_memoryAddr - 256) & 0xFFFF;
				snprintf(s_memoryAddrBuf, sizeof(s_memoryAddrBuf), "%04X", s_memoryAddr);
			}
			if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) {
				s_memoryAddr = (s_memoryAddr + 16) & 0xFFFF;
				snprintf(s_memoryAddrBuf, sizeof(s_memoryAddrBuf), "%04X", s_memoryAddr);
			}
			if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) {
				s_memoryAddr = (s_memoryAddr - 16) & 0xFFFF;
				snprintf(s_memoryAddrBuf, sizeof(s_memoryAddrBuf), "%04X", s_memoryAddr);
			}
			if (ImGui::IsKeyPressed(ImGuiKey_Home)) {
				s_memoryAddr = 0;
				snprintf(s_memoryAddrBuf, sizeof(s_memoryAddrBuf), "%04X", s_memoryAddr);
			}
			if (ImGui::IsKeyPressed(ImGuiKey_End)) {
				s_memoryAddr = 0xFF00;
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

			ImGui::Separator();
			{
				char addrStr[8];
				snprintf(addrStr, sizeof(addrStr), "$%04X", s_disasmContextAddr);
				if (ImGui::MenuItem("Copy Address")) {
					ImGui::SetClipboardText(addrStr);
				}
			}

			// Copy hex row (16 bytes from context address)
			if (ImGui::MenuItem("Copy Row (16 bytes)")) {
				uint16 rowBase = s_disasmContextAddr & 0xFFF0;
				char hexRow[128];
				int pos = snprintf(hexRow, sizeof(hexRow), "%04X:", rowBase);
				for (int j = 0; j < 16; j++) {
					uint8 b = target->DebugReadByte((rowBase + j) & 0xFFFF);
					pos += snprintf(hexRow + pos, sizeof(hexRow) - pos, " %02X", b);
				}
				ImGui::SetClipboardText(hexRow);
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

	// Input line with hint and help button
	if (ImGui::SmallButton("?")) {
		ImGui::OpenPopup("CmdHelp");
	}
	if (ImGui::BeginPopup("CmdHelp")) {
		ImGui::TextUnformatted("Common debugger commands:");
		ImGui::Separator();
		ImGui::TextUnformatted("g          - Go (run)");
		ImGui::TextUnformatted("t          - Trace (step into)");
		ImGui::TextUnformatted("o          - Step over");
		ImGui::TextUnformatted("bp <addr>  - Set PC breakpoint");
		ImGui::TextUnformatted("bl         - List breakpoints");
		ImGui::TextUnformatted("bc <n>     - Clear breakpoint");
		ImGui::TextUnformatted("ba r <addr>- Break on read");
		ImGui::TextUnformatted("ba w <addr>- Break on write");
		ImGui::TextUnformatted("d <addr>   - Disassemble");
		ImGui::TextUnformatted("db <addr>  - Dump bytes");
		ImGui::TextUnformatted("e <addr> <val> - Edit byte");
		ImGui::TextUnformatted("r          - Show registers");
		ImGui::TextUnformatted("? <expr>   - Evaluate expression");
		ImGui::TextUnformatted(".modules   - List loaded modules");
		ImGui::TextUnformatted(".help      - Full command list");
		ImGui::EndPopup();
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(-1);
	ImGuiInputTextFlags inputFlags = ImGuiInputTextFlags_EnterReturnsTrue
		| ImGuiInputTextFlags_CallbackHistory;

	if (ImGui::InputTextWithHint("##cmdinput", "Enter debugger command...", s_cmdInputBuf, sizeof(s_cmdInputBuf), inputFlags, ConsoleInputCallback)) {
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

// Breakpoint editing state
static uint32 s_bpEditIdx = 0;
static char s_bpCondBuf[256] = "";
static char s_bpCmdBuf[256] = "";
static bool s_bpLogOnly = false;

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

			char bpText[128];
			if (sym.mpName)
				snprintf(bpText, sizeof(bpText), "[%d] %s $%04X (%s)", info.mNumber, type, info.mAddress, sym.mpName);
			else
				snprintf(bpText, sizeof(bpText), "[%d] %s $%04X", info.mNumber, type, info.mAddress);

			if (info.mLength > 1) {
				int len = (int)strlen(bpText);
				snprintf(bpText + len, sizeof(bpText) - len, " len=%u", info.mLength);
			}

			if (ImGui::Selectable(bpText, false, ImGuiSelectableFlags_None)) {
				if (info.mbBreakOnPC) {
					s_disasmAddr = info.mAddress;
					s_disasmFollowPC = false;
					snprintf(s_disasmAddrBuf, sizeof(s_disasmAddrBuf), "%04X", info.mAddress);
					s_showDisassembly = true;
				} else {
					s_memoryAddr = info.mAddress;
					snprintf(s_memoryAddrBuf, sizeof(s_memoryAddrBuf), "%04X", info.mAddress);
					s_showMemory = true;
				}
			}

			// Right-click opens edit popup
			if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
				s_bpEditIdx = useridx;
				s_bpCondBuf[0] = 0;
				s_bpCmdBuf[0] = 0;
				s_bpLogOnly = info.mbContinueExecution;
				// Pre-fill condition text if available
				if (info.mpCondition)
					snprintf(s_bpCondBuf, sizeof(s_bpCondBuf), "<expr>");
				if (info.mpCommand)
					snprintf(s_bpCmdBuf, sizeof(s_bpCmdBuf), "%s", info.mpCommand);
				ImGui::OpenPopup("##bpedit");
			}

			// Show condition/command annotations on the same line
			if (info.mpCondition || info.mpCommand || info.mbContinueExecution) {
				ImGui::SameLine();
				if (info.mpCondition)
					ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.3f, 1.0f), "[cond]");
				if (info.mpCommand) {
					ImGui::SameLine();
					ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "[cmd]");
				}
				if (info.mbContinueExecution) {
					ImGui::SameLine();
					ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "[log]");
				}
			}

			// Tooltip showing full condition/command text
			if (ImGui::IsItemHovered() && (info.mpCondition || info.mpCommand)) {
				ImGui::BeginTooltip();
				if (info.mpCondition)
					ImGui::Text("Condition: <expr>");
				if (info.mpCommand)
					ImGui::Text("Command: %s", info.mpCommand);
				if (info.mbContinueExecution)
					ImGui::TextDisabled("(log only, no break)");
				ImGui::EndTooltip();
			}

			ImGui::SameLine();
			if (ImGui::SmallButton("X")) {
				dbg->ClearUserBreakpoint(useridx, true);
			}

			ImGui::PopID();
		}
	}

	// Breakpoint edit popup (outside the loop, shared by all entries)
	if (ImGui::BeginPopup("##bpedit")) {
		ImGui::Text("Breakpoint #%u", s_bpEditIdx);
		ImGui::Separator();

		ImGui::TextUnformatted("Condition:");
		ImGui::SetNextItemWidth(250);
		ImGui::InputTextWithHint("##bpcond", "e.g. a=#40, x>10", s_bpCondBuf, sizeof(s_bpCondBuf));

		ImGui::TextUnformatted("Command:");
		ImGui::SetNextItemWidth(250);
		ImGui::InputTextWithHint("##bpcmd", "e.g. r, db $2000", s_bpCmdBuf, sizeof(s_bpCmdBuf));

		ImGui::Checkbox("Log only (don't break)", &s_bpLogOnly);

		ImGui::Separator();
		if (ImGui::Button("Apply")) {
			// Use debugger commands to set condition and command
			// bx <n> <condition> - set condition
			// bk <n> <command> - set command
			// bl <n> - set log-only (continue execution)
			if (s_bpCondBuf[0]) {
				char cmd[320];
				snprintf(cmd, sizeof(cmd), "bx %u %s", s_bpEditIdx, s_bpCondBuf);
				dbg->QueueCommand(cmd, false);
			} else {
				// Clear condition
				char cmd[64];
				snprintf(cmd, sizeof(cmd), "bx %u", s_bpEditIdx);
				dbg->QueueCommand(cmd, false);
			}
			if (s_bpCmdBuf[0]) {
				char cmd[320];
				snprintf(cmd, sizeof(cmd), "bk %u \"%s\"", s_bpEditIdx, s_bpCmdBuf);
				dbg->QueueCommand(cmd, false);
			}
			if (s_bpLogOnly) {
				char cmd[64];
				snprintf(cmd, sizeof(cmd), "bl %u", s_bpEditIdx);
				dbg->QueueCommand(cmd, false);
			}
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) {
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
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

	static int s_watchMode = 0;  // 0=byte, 1=word, 2=expr
	ImGui::SetNextItemWidth(60);
	const char *modes[] = { "Byte", "Word", "Expr" };
	ImGui::Combo("##wmode", &s_watchMode, modes, 3);
	ImGui::SameLine();

	if ((ImGui::Button("Add##w") || addWatch) && s_watchAddrBuf[0]) {
		if (s_watchMode == 2) {
			// Expression watch via debugger command
			char cmd[192];
			snprintf(cmd, sizeof(cmd), "we %s", s_watchAddrBuf);
			dbg->QueueCommand(cmd, false);
			s_watchAddrBuf[0] = 0;
		} else {
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
		ATSymbol wsym {};
		IATDebuggerSymbolLookup *wdbs = ATGetDebuggerSymbolLookup();
		if (wdbs && (info.mMode == ATDebuggerWatchMode::ByteAtAddress || info.mMode == ATDebuggerWatchMode::WordAtAddress))
			wdbs->LookupSymbol(addr, kATSymbol_Any, wsym);

		if (info.mMode == ATDebuggerWatchMode::ByteAtAddress) {
			uint8 val = target ? target->DebugReadByte(addr) : 0;
			if (wsym.mpName)
				ImGui::Text("[%d] %s: %02X (%3d)", idx, wsym.mpName, val, val);
			else
				ImGui::Text("[%d] $%04X: %02X (%3d)", idx, addr, val, val);
		} else if (info.mMode == ATDebuggerWatchMode::WordAtAddress) {
			uint8 lo = target ? target->DebugReadByte(addr) : 0;
			uint8 hi = target ? target->DebugReadByte((addr + 1) & 0xFFFF) : 0;
			uint16 val = lo | ((uint16)hi << 8);
			if (wsym.mpName)
				ImGui::Text("[%d] %s: %04X (%5d)", idx, wsym.mpName, val, val);
			else
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

		// Double-click to view address in memory
		if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)
			&& (info.mMode == ATDebuggerWatchMode::ByteAtAddress || info.mMode == ATDebuggerWatchMode::WordAtAddress)) {
			s_memoryAddr = addr;
			snprintf(s_memoryAddrBuf, sizeof(s_memoryAddrBuf), "%04X", addr);
			s_showMemory = true;
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

	if (n > 0) {
		ImGui::TextDisabled("  SP    PC    Symbol");
		ImGui::Separator();
	}

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
	ImGui::SameLine();
	static bool s_histShowRegs = false;
	ImGui::Checkbox("Registers", &s_histShowRegs);

	if (ImGui::BeginChild("##histlines", ImVec2(0, 0), ImGuiChildFlags_None)) {
		IATDebuggerSymbolLookup *dbs = ATGetDebuggerSymbolLookup();
		ATDebugDisasmMode disasmMode = target->GetDisasmMode();

		// Show the most recent entries (up to 256)
		uint32 showCount = count < 256 ? count : 256;
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

			char label[384];
			if (s_histShowRegs) {
				if (sym.mpName && sym.mOffset == he.mPC)
					snprintf(label, sizeof(label), "%6u A:%02X X:%02X Y:%02X %s  (%s)##h%u",
						he.mCycle, he.mA, he.mX, he.mY, line.c_str(), sym.mpName, i);
				else
					snprintf(label, sizeof(label), "%6u A:%02X X:%02X Y:%02X %s##h%u",
						he.mCycle, he.mA, he.mX, he.mY, line.c_str(), i);
			} else {
				if (sym.mpName && sym.mOffset == he.mPC)
					snprintf(label, sizeof(label), "%6u %s  (%s)##h%u",
						he.mCycle, line.c_str(), sym.mpName, i);
				else
					snprintf(label, sizeof(label), "%6u %s##h%u",
						he.mCycle, line.c_str(), i);
			}

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

// ============= Source Code Window =============

static void RefreshSourceFileList() {
	s_sourceFiles.clear();
	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return;

	dbg->EnumSourceFiles(
		[](const wchar_t *path, uint32 numLines) {
			if (numLines > 0)
				s_sourceFiles.push_back({VDStringW(path), numLines});
		}
	);
	s_sourceNeedsFileList = false;
}

static void LoadSourceFile(int fileIdx) {
	s_sourceLines.clear();
	s_lineToAddr.clear();
	s_addrToLine.clear();
	s_sourcePCLine = -1;
	s_sourceFrameLine = -1;
	s_sourceModuleId = 0;
	s_sourceFileId = 0;

	if (fileIdx < 0 || fileIdx >= (int)s_sourceFiles.size())
		return;

	IATDebuggerSymbolLookup *dbs = ATGetDebuggerSymbolLookup();
	if (!dbs)
		return;

	const VDStringW& path = s_sourceFiles[fileIdx].mPath;

	// Bind the file name to module/file IDs
	uint32 moduleId;
	uint16 fileId;
	if (!dbs->LookupFile(path.c_str(), moduleId, fileId))
		return;

	s_sourceModuleId = moduleId;
	s_sourceFileId = fileId;

	// Get line→address mappings
	vdfastvector<ATSourceLineInfo> lines;
	dbs->GetLinesForFile(moduleId, fileId, lines);

	for (const auto& li : lines) {
		int lineIdx = (int)li.mLine - 1;  // convert 1-based to 0-based
		if (lineIdx >= 0) {
			// Keep the first (lowest) address for each line
			if (s_lineToAddr.find(lineIdx) == s_lineToAddr.end())
				s_lineToAddr[lineIdx] = li.mOffset;
			// Keep the first line for each address
			if (s_addrToLine.find(li.mOffset) == s_addrToLine.end())
				s_addrToLine[li.mOffset] = lineIdx;
		}
	}

	// Try to load the actual source file from disk
	ATDebuggerSourceFileInfo sourceFileInfo;
	if (!dbs->GetSourceFilePath(moduleId, fileId, sourceFileInfo))
		return;

	// Try source path first, then module path
	const wchar_t *tryPaths[] = { sourceFileInfo.mSourcePath.c_str(), sourceFileInfo.mModulePath.c_str() };
	bool loaded = false;

	for (const wchar_t *tryPath : tryPaths) {
		if (!tryPath || !tryPath[0])
			continue;

		// Convert wchar_t path to narrow string for ifstream
		VDStringA narrowPath;
		for (const wchar_t *p = tryPath; *p; ++p) {
			if (*p < 128)
				narrowPath += (char)*p;
			else
				narrowPath += '?';
		}

		std::ifstream ifs(narrowPath.c_str());
		if (!ifs.is_open())
			continue;

		std::string line;
		while (std::getline(ifs, line))
			s_sourceLines.push_back(std::move(line));

		loaded = true;
		break;
	}

	if (!loaded) {
		// Show placeholder lines indicating the file couldn't be loaded
		s_sourceLines.push_back("(Source file not found on disk)");
		s_sourceLines.push_back("");
		// Still useful: show line numbers that have address mappings
		if (!s_lineToAddr.empty()) {
			int maxLine = s_lineToAddr.rbegin()->first;
			s_sourceLines.resize(maxLine + 1);
		}
	}

	s_sourceNeedsRebind = false;
}

static void DrawSourceCode() {
	if (!s_showSourceCode)
		return;

	IATDebugger *dbg = ATGetDebugger();
	if (!dbg)
		return;

	ImGui::SetNextWindowSize(ImVec2(600, 500), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Source Code", &s_showSourceCode)) {
		ImGui::End();
		return;
	}

	// Refresh file list on symbol changes
	if (s_pClient && s_pClient->mbSymbolsChanged) {
		s_pClient->mbSymbolsChanged = false;
		s_sourceNeedsFileList = true;
		s_sourceNeedsRebind = true;
	}

	if (s_sourceNeedsFileList)
		RefreshSourceFileList();

	if (s_sourceFiles.empty()) {
		ImGui::TextDisabled("No source files loaded");
		ImGui::TextDisabled("Use Debug > Load Symbols to load a .lst/.lbl file");
		ImGui::End();
		return;
	}

	// File selector combo
	{
		// Build display label for current selection
		const char *preview = (s_sourceSelectedFile >= 0 && s_sourceSelectedFile < (int)s_sourceFiles.size())
			? nullptr : "(select file)";

		VDStringA previewStr;
		if (!preview && s_sourceSelectedFile >= 0) {
			const VDStringW& wp = s_sourceFiles[s_sourceSelectedFile].mPath;
			// Extract just the filename for display
			const wchar_t *slash = wcsrchr(wp.c_str(), '/');
			const wchar_t *bslash = wcsrchr(wp.c_str(), '\\');
			const wchar_t *name = wp.c_str();
			if (slash && slash > name) name = slash + 1;
			if (bslash && bslash > name) name = bslash + 1;
			for (const wchar_t *p = name; *p; ++p)
				previewStr += (*p < 128) ? (char)*p : '?';
			preview = previewStr.c_str();
		}

		ImGui::SetNextItemWidth(300);
		if (ImGui::BeginCombo("##srcfile", preview)) {
			for (int i = 0; i < (int)s_sourceFiles.size(); i++) {
				const VDStringW& wp = s_sourceFiles[i].mPath;
				VDStringA display;
				for (const wchar_t *p = wp.c_str(); *p; ++p)
					display += (*p < 128) ? (char)*p : '?';

				char label[512];
				snprintf(label, sizeof(label), "%s (%u lines)##sf%d",
					display.c_str(), s_sourceFiles[i].mNumLines, i);

				if (ImGui::Selectable(label, i == s_sourceSelectedFile)) {
					s_sourceSelectedFile = i;
					s_sourceNeedsRebind = true;
				}
			}
			ImGui::EndCombo();
		}
	}

	if (s_sourceNeedsRebind && s_sourceSelectedFile >= 0)
		LoadSourceFile(s_sourceSelectedFile);

	if (s_sourceLines.empty()) {
		ImGui::TextDisabled("Select a source file above");
		ImGui::End();
		return;
	}

	// Update PC line from debugger state
	s_sourcePCLine = -1;
	s_sourceFrameLine = -1;
	if (s_pClient && s_pClient->mbStateValid && !s_pClient->mState.mbRunning) {
		const auto& st = s_pClient->mState;
		if (st.mPCModuleId == s_sourceModuleId && st.mPCFileId == s_sourceFileId && st.mPCLine > 0) {
			s_sourcePCLine = (int)st.mPCLine - 1;
			s_sourceScrollToPC = true;
		} else {
			// Try reverse lookup: find PC address in our addr→line map
			uint16 pc = st.mPC;
			auto it = s_addrToLine.find(pc);
			if (it != s_addrToLine.end()) {
				s_sourcePCLine = it->second;
				s_sourceScrollToPC = true;
			}
		}

		// Frame PC for call stack navigation
		uint32 framePC = dbg->GetFrameExtPC() & 0xFFFF;
		if (framePC != st.mPC) {
			auto fit = s_addrToLine.find(framePC);
			if (fit != s_addrToLine.end())
				s_sourceFrameLine = fit->second;
		}
	}

	// Source code display
	float lineHeight = ImGui::GetTextLineHeightWithSpacing();
	int totalLines = (int)s_sourceLines.size();

	if (ImGui::BeginChild("##srclines", ImVec2(0, 0), ImGuiChildFlags_None)) {
		ImGuiListClipper clipper;
		clipper.Begin(totalLines, lineHeight);

		// Scroll to PC line if needed
		if (s_sourceScrollToPC && s_sourcePCLine >= 0) {
			float scrollY = (float)s_sourcePCLine * lineHeight - ImGui::GetWindowHeight() * 0.3f;
			if (scrollY < 0) scrollY = 0;
			ImGui::SetScrollY(scrollY);
			s_sourceScrollToPC = false;
		}

		while (clipper.Step()) {
			for (int line = clipper.DisplayStart; line < clipper.DisplayEnd; line++) {
				ImGui::PushID(line);

				// Background highlight for PC/frame lines
				ImVec2 cursorPos = ImGui::GetCursorScreenPos();
				float width = ImGui::GetContentRegionAvail().x;

				if (line == s_sourcePCLine) {
					ImGui::GetWindowDrawList()->AddRectFilled(
						cursorPos,
						ImVec2(cursorPos.x + width, cursorPos.y + lineHeight),
						IM_COL32(100, 100, 0, 80));
				} else if (line == s_sourceFrameLine) {
					ImGui::GetWindowDrawList()->AddRectFilled(
						cursorPos,
						ImVec2(cursorPos.x + width, cursorPos.y + lineHeight),
						IM_COL32(0, 60, 120, 60));
				}

				// Breakpoint indicator
				auto addrIt = s_lineToAddr.find(line);
				bool hasMappedAddr = (addrIt != s_lineToAddr.end());

				if (hasMappedAddr && dbg->IsBreakpointAtPC(addrIt->second)) {
					ImGui::GetWindowDrawList()->AddCircleFilled(
						ImVec2(cursorPos.x + 6, cursorPos.y + lineHeight * 0.5f),
						4.0f, IM_COL32(220, 40, 40, 255));
				}

				// PC arrow indicator
				if (line == s_sourcePCLine) {
					ImVec2 arrowPos(cursorPos.x + 14, cursorPos.y + lineHeight * 0.5f);
					ImGui::GetWindowDrawList()->AddTriangleFilled(
						ImVec2(arrowPos.x - 3, arrowPos.y - 4),
						ImVec2(arrowPos.x - 3, arrowPos.y + 4),
						ImVec2(arrowPos.x + 4, arrowPos.y),
						IM_COL32(255, 255, 0, 255));
				}

				// Line number and address columns
				ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 20);

				if (hasMappedAddr) {
					ImGui::TextDisabled("%5d %04X ", line + 1, addrIt->second);
				} else {
					ImGui::TextDisabled("%5d      ", line + 1);
				}

				ImGui::SameLine();

				// Source text
				if (hasMappedAddr) {
					if (line == s_sourcePCLine)
						ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 1.0f, 0.3f, 1.0f));

					ImGui::TextUnformatted(
						line < (int)s_sourceLines.size() ? s_sourceLines[line].c_str() : "");

					if (line == s_sourcePCLine)
						ImGui::PopStyleColor();
				} else {
					ImGui::TextDisabled("%s",
						line < (int)s_sourceLines.size() ? s_sourceLines[line].c_str() : "");
				}

				// Click to navigate disassembly
				if (hasMappedAddr && ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) {
					s_disasmAddr = addrIt->second;
					s_disasmFollowPC = false;
					snprintf(s_disasmAddrBuf, sizeof(s_disasmAddrBuf), "%04X", addrIt->second);
					s_showDisassembly = true;
				}

				// Right-click context menu
				if (hasMappedAddr && ImGui::BeginPopupContextItem("##srcctx")) {
					uint32 addr = addrIt->second;
					if (ImGui::MenuItem("Toggle Breakpoint"))
						dbg->ToggleBreakpoint(addr);
					if (ImGui::MenuItem("Run to Cursor", nullptr, false, !dbg->IsRunning())) {
						char cmd[64];
						snprintf(cmd, sizeof(cmd), "g %04X", addr);
						dbg->QueueCommand(cmd, false);
					}
					if (ImGui::MenuItem("Go to Disassembly")) {
						s_disasmAddr = addr;
						s_disasmFollowPC = false;
						snprintf(s_disasmAddrBuf, sizeof(s_disasmAddrBuf), "%04X", addr);
						s_showDisassembly = true;
					}
					ImGui::EndPopup();
				}

				ImGui::PopID();
			}
		}
	}
	ImGui::EndChild();

	ImGui::End();
}

// Navigate source code window to a specific address
void ATImGuiDebuggerNavigateSource(uint32 addr) {
	IATDebuggerSymbolLookup *dbs = ATGetDebuggerSymbolLookup();
	if (!dbs)
		return;

	// Look up which file/line this address maps to
	uint32 moduleId;
	ATSourceLineInfo lineInfo;
	if (!dbs->LookupLine(addr, false, moduleId, lineInfo))
		return;

	// Ensure file list is populated
	if (s_sourceNeedsFileList)
		RefreshSourceFileList();

	// Find the matching file in our list
	ATDebuggerSourceFileInfo sourceFileInfo;
	if (!dbs->GetSourceFilePath(moduleId, lineInfo.mFileId, sourceFileInfo))
		return;

	// Find this file in s_sourceFiles by trying LookupFile on each entry
	for (int i = 0; i < (int)s_sourceFiles.size(); i++) {
		uint32 testModId;
		uint16 testFileId;
		if (dbs->LookupFile(s_sourceFiles[i].mPath.c_str(), testModId, testFileId)) {
			if (testModId == moduleId && testFileId == lineInfo.mFileId) {
				if (s_sourceSelectedFile != i) {
					s_sourceSelectedFile = i;
					LoadSourceFile(i);
				}
				s_sourcePCLine = (int)lineInfo.mLine - 1;
				s_sourceScrollToPC = true;
				s_showSourceCode = true;
				return;
			}
		}
	}
}

// Navigate source code window to a specific file path + line number
void ATImGuiDebuggerNavigateSourceLine(const wchar_t *path, int line) {
	if (s_sourceNeedsFileList)
		RefreshSourceFileList();

	const wchar_t *pathName = VDFileSplitPath(path);

	for (int i = 0; i < (int)s_sourceFiles.size(); i++) {
		const wchar_t *entryName = VDFileSplitPath(s_sourceFiles[i].mPath.c_str());
		if (VDFileIsPathEqual(pathName, entryName)) {
			if (s_sourceSelectedFile != i) {
				s_sourceSelectedFile = i;
				LoadSourceFile(i);
			}
			s_sourcePCLine = line;
			s_sourceScrollToPC = true;
			s_showSourceCode = true;
			return;
		}
	}
}

// ============= Printer Output Window =============

static void DrawPrinterOutput() {
	if (!s_showPrinterOutput)
		return;

	ImGui::SetNextWindowSize(ImVec2(500, 350), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Printer Output", &s_showPrinterOutput)) {
		ImGui::End();
		return;
	}

	ATPrinterOutputManager *mgr = static_cast<ATPrinterOutputManager *>(
		&g_sim.GetPrinterOutputManager());

	uint32 outputCount = mgr->GetOutputCount();

	if (outputCount == 0) {
		ImGui::TextDisabled("No printer outputs active");
		ImGui::TextDisabled("Add a printer device via System > Devices");
		ImGui::End();
		return;
	}

	// Output selector
	if (s_printerSelectedOutput >= (int)outputCount)
		s_printerSelectedOutput = 0;

	if (outputCount > 1) {
		ImGui::SetNextItemWidth(200);
		if (ImGui::BeginCombo("##printersel", nullptr)) {
			for (uint32 i = 0; i < outputCount; i++) {
				ATPrinterOutput& out = mgr->GetOutput(i);
				const wchar_t *wname = out.GetName();
				VDStringA name;
				for (const wchar_t *p = wname; *p; ++p)
					name += (*p < 128) ? (char)*p : '?';

				char label[128];
				snprintf(label, sizeof(label), "%s##po%u", name.c_str(), i);
				if (ImGui::Selectable(label, (int)i == s_printerSelectedOutput)) {
					s_printerSelectedOutput = (int)i;
					s_printerLastOffset = 0;
					s_printerText.clear();
					s_printerNeedsRefresh = true;
				}
			}
			ImGui::EndCombo();
		}
		ImGui::SameLine();
	}

	// Clear button
	if (ImGui::Button("Clear")) {
		ATPrinterOutput& out = mgr->GetOutput(s_printerSelectedOutput);
		out.Clear();
		s_printerLastOffset = 0;
		s_printerText.clear();
	}

	ImGui::SameLine();
	ImGui::TextDisabled("(%zu chars)", s_printerText.size());

	ImGui::Separator();

	// Refresh text from output
	{
		ATPrinterOutput& out = mgr->GetOutput(s_printerSelectedOutput);
		size_t currentLen = out.GetLength();

		if (currentLen > s_printerLastOffset || s_printerNeedsRefresh) {
			if (s_printerNeedsRefresh) {
				s_printerLastOffset = 0;
				s_printerText.clear();
				s_printerNeedsRefresh = false;
			}

			// Read new text
			if (currentLen > s_printerLastOffset) {
				const wchar_t *ptr = out.GetTextPointer(s_printerLastOffset);
				size_t newChars = currentLen - s_printerLastOffset;

				// Convert wchar_t to UTF-8
				for (size_t i = 0; i < newChars; i++) {
					wchar_t ch = ptr[i];
					if (ch < 0x80) {
						s_printerText += (char)ch;
					} else if (ch < 0x800) {
						s_printerText += (char)(0xC0 | (ch >> 6));
						s_printerText += (char)(0x80 | (ch & 0x3F));
					} else {
						s_printerText += (char)(0xE0 | (ch >> 12));
						s_printerText += (char)(0x80 | ((ch >> 6) & 0x3F));
						s_printerText += (char)(0x80 | (ch & 0x3F));
					}
				}

				s_printerLastOffset = currentLen;
			}

			out.Revalidate();
		}
	}

	// Display text
	if (s_printerText.empty()) {
		ImGui::TextDisabled("(no output)");
	} else {
		if (ImGui::BeginChild("##printertext", ImVec2(0, 0), ImGuiChildFlags_None)) {
			ImGui::TextUnformatted(s_printerText.c_str(), s_printerText.c_str() + s_printerText.size());

			// Auto-scroll to bottom
			if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20.0f)
				ImGui::SetScrollHereY(1.0f);
		}
		ImGui::EndChild();
	}

	ImGui::End();
}

// ============= Profiler Window =============

static const char *ProfilerContextName(uint32 ctx) {
	switch (ctx) {
		case kATProfileContext_Main:      return "Main";
		case kATProfileContext_Interrupt:  return "Int";
		case kATProfileContext_IRQ:        return "IRQ";
		case kATProfileContext_VBI:        return "VBI";
		case kATProfileContext_DLI:        return "DLI";
		default:                          return "?";
	}
}

static void ProfilerBuildEntries() {
	s_profilerEntries.clear();
	s_profilerSelectedRows.clear();
	if (!s_profilerMerged)
		return;

	IATDebuggerSymbolLookup *dbs = ATGetDebuggerSymbolLookup();

	for (const ATProfileRecord& rec : s_profilerMerged->mRecords) {
		ProfileEntry e;
		e.addr = rec.mAddress;
		e.calls = rec.mCalls;
		e.insns = rec.mInsns;
		e.cycles = rec.mCycles;
		e.unhaltedCycles = rec.mUnhaltedCycles;
		e.context = rec.mContext;
		e.counters[0] = rec.mCounters[0];
		e.counters[1] = rec.mCounters[1];

		if (dbs) {
			ATSymbol sym {};
			if (dbs->LookupSymbol(rec.mAddress, kATSymbol_Execute, sym) && sym.mpName)
				e.sym = sym.mpName;
		}

		s_profilerEntries.push_back(std::move(e));
	}
}

static void ProfilerSort() {
	std::sort(s_profilerEntries.begin(), s_profilerEntries.end(),
		[](const ProfileEntry& a, const ProfileEntry& b) {
			switch (s_profilerSortCol) {
				case 0: return s_profilerSortAsc ? a.context < b.context : a.context > b.context;
				case 1: return s_profilerSortAsc ? a.addr < b.addr : a.addr > b.addr;
				case 2: return s_profilerSortAsc ? a.sym < b.sym : a.sym > b.sym;
				case 3: return s_profilerSortAsc ? a.calls < b.calls : a.calls > b.calls;
				case 4: return s_profilerSortAsc ? a.cycles < b.cycles : a.cycles > b.cycles;
				case 5: return s_profilerSortAsc ? a.insns < b.insns : a.insns > b.insns;
				case 6: return s_profilerSortAsc ? a.unhaltedCycles < b.unhaltedCycles : a.unhaltedCycles > b.unhaltedCycles;
				case 7: { // DMA%
					float da = a.cycles ? 100.0f * (1.0f - (float)a.unhaltedCycles / (float)a.cycles) : 0;
					float db = b.cycles ? 100.0f * (1.0f - (float)b.unhaltedCycles / (float)b.cycles) : 0;
					return s_profilerSortAsc ? da < db : da > db;
				}
				case 8: { // CPI
					float ca = a.insns ? (float)a.cycles / (float)a.insns : 0;
					float cb = b.insns ? (float)b.cycles / (float)b.insns : 0;
					return s_profilerSortAsc ? ca < cb : ca > cb;
				}
				case 9:  return s_profilerSortAsc ? a.counters[0] < b.counters[0] : a.counters[0] > b.counters[0];
				case 10: return s_profilerSortAsc ? a.counters[1] < b.counters[1] : a.counters[1] > b.counters[1];
				default: return false;
			}
		});
}

static void ProfilerRemerge(uint32 start, uint32 end) {
	if (s_profilerSession.mpFrames.empty())
		return;

	uint32 n = (uint32)s_profilerSession.mpFrames.size();
	if (start >= n) start = n - 1;
	if (end > n) end = n;
	if (end <= start) end = start + 1;

	ATProfileMergedFrame *merged = nullptr;
	ATProfileMergeFrames(s_profilerSession, start, end, &merged);
	s_profilerMerged.clear();
	s_profilerMerged.set(merged);

	ProfilerBuildEntries();
	ProfilerSort();
}

static void ProfilerStart() {
	g_sim.SetProfilingEnabled(true);
	ATCPUProfiler *prof = g_sim.GetProfiler();
	if (prof) {
		// Apply boundary rules
		uint32 param = 0, param2 = 0;
		if (s_profilerBoundaryRule == kATProfileBoundaryRule_PCAddress ||
			s_profilerBoundaryRule == kATProfileBoundaryRule_PCAddressFunction) {
			IATDebugger *dbg = ATGetDebugger();
			if (dbg && s_profilerBoundaryAddr[0]) {
				sint32 resolved = dbg->ResolveSymbol(s_profilerBoundaryAddr, true, true, false);
				if (resolved >= 0)
					param = (uint32)resolved;
			}
			if (s_profilerBoundaryRule == kATProfileBoundaryRule_PCAddress && s_profilerBoundaryAddr2[0]) {
				IATDebugger *dbg2 = ATGetDebugger();
				if (dbg2) {
					sint32 resolved2 = dbg2->ResolveSymbol(s_profilerBoundaryAddr2, true, true, false);
					if (resolved2 >= 0)
						param2 = (uint32)resolved2;
					else
						param2 = (uint32)0 - 1;
				}
			} else {
				param2 = (uint32)0 - 1;
			}
		}
		prof->SetBoundaryRule(s_profilerBoundaryRule, param, param2);
		prof->SetGlobalAddressesEnabled(s_profilerGlobalAddresses);
		prof->Start(s_profilerMode, s_profilerC1, s_profilerC2);
		s_profilerRunning = true;
		s_profilerHasData = false;
		s_profilerEntries.clear();
		s_profilerSelectedRows.clear();
		s_profilerMerged.clear();
	}
}

static void ProfilerStop() {
	ATCPUProfiler *prof = g_sim.GetProfiler();
	if (prof && prof->IsRunning()) {
		prof->End();
		prof->GetSession(s_profilerSession);

		uint32 frameCount = (uint32)s_profilerSession.mpFrames.size();
		if (frameCount > 0) {
			// Compute truncated mean for timeline vertical range
			if (frameCount >= 4) {
				std::vector<uint32> durations(frameCount);
				for (uint32 i = 0; i < frameCount; i++)
					durations[i] = s_profilerSession.mpFrames[i]->mTotalCycles;
				std::sort(durations.begin(), durations.end());
				size_t n4 = frameCount / 4;
				size_t n2 = frameCount - n4 * 2;
				uint64_t sum = std::accumulate(durations.begin() + n4, durations.end() - n4, (uint64_t)0);
				s_profilerTimelineVRange = (uint32)((sum * 2 + n2 / 2) / n2);
			} else {
				uint32 maxCyc = 0;
				for (uint32 i = 0; i < frameCount; i++)
					maxCyc = std::max(maxCyc, s_profilerSession.mpFrames[i]->mTotalCycles);
				s_profilerTimelineVRange = maxCyc > 0 ? maxCyc * 2 : 1;
			}

			s_profilerSelStart = 0;
			s_profilerSelEnd = (int)frameCount;

			ATProfileMergedFrame *merged = nullptr;
			ATProfileMergeFrames(s_profilerSession, 0, frameCount, &merged);
			s_profilerMerged.clear();
			s_profilerMerged.set(merged);

			ProfilerBuildEntries();
			ProfilerSort();
			s_profilerHasData = true;
		}
	}
	s_profilerRunning = false;
	g_sim.SetProfilingEnabled(false);
}

static const char *kProfileModeNames[] = {
	"Instructions",
	"Functions",
	"Call Graph",
	"Basic Block",
	"Basic Lines",
};

static const char *kCounterModeNames[] = {
	"None",
	"Branch Taken",
	"Branch Not Taken",
	"Page Crossing",
	"Redundant Op",
};

// --- Phase 2: Frame Timeline ---
static void DrawProfilerTimeline() {
	uint32 frameCount = (uint32)s_profilerSession.mpFrames.size();
	if (frameCount <= 1)
		return;

	ImGui::Text("Timeline:");
	float zoom = s_profilerTimelineZoom;
	float totalWidth = frameCount * zoom;
	float barHeight = 80.0f;

	if (ImGui::BeginChild("##timeline", ImVec2(0, barHeight + 4), ImGuiChildFlags_Borders, ImGuiWindowFlags_HorizontalScrollbar)) {
		ImVec2 canvasPos = ImGui::GetCursorScreenPos();
		ImVec2 canvasSize = ImVec2(totalWidth, barHeight);
		ImDrawList *dl = ImGui::GetWindowDrawList();

		// Invisible button to capture input over the whole area
		ImGui::InvisibleButton("##tlarea", canvasSize);
		bool hovered = ImGui::IsItemHovered();
		bool clicked = ImGui::IsItemActive();

		// Zoom with Ctrl+scroll
		if (hovered && ImGui::GetIO().KeyCtrl) {
			float wheel = ImGui::GetIO().MouseWheel;
			if (wheel != 0) {
				float oldZoom = s_profilerTimelineZoom;
				s_profilerTimelineZoom = std::clamp(s_profilerTimelineZoom * (wheel > 0 ? 1.25f : 0.8f), 1.0f, 32.0f);
				// Adjust scroll to keep mouse position stable
				if (s_profilerTimelineZoom != oldZoom) {
					float mouseX = ImGui::GetIO().MousePos.x - canvasPos.x + ImGui::GetScrollX();
					float ratio = s_profilerTimelineZoom / oldZoom;
					ImGui::SetScrollX(mouseX * ratio - (ImGui::GetIO().MousePos.x - canvasPos.x));
				}
			}
		}

		uint32 vrange = s_profilerTimelineVRange > 0 ? s_profilerTimelineVRange : 1;

		// Mouse interaction
		if (hovered) {
			float mx = ImGui::GetIO().MousePos.x - canvasPos.x + ImGui::GetScrollX();
			int frame = (int)(mx / zoom);
			if (frame >= 0 && frame < (int)frameCount) {
				s_profilerHoverFrame = frame;

				// Tooltip
				uint32 cyc = s_profilerSession.mpFrames[frame]->mTotalCycles;
				uint32 ins = s_profilerSession.mpFrames[frame]->mTotalInsns;
				ImGui::SetTooltip("Frame %d: %u cycles, %u insns", frame, cyc, ins);
			} else {
				s_profilerHoverFrame = -1;
			}

			// Click/drag to select range
			if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && s_profilerHoverFrame >= 0) {
				s_profilerSelStart = s_profilerHoverFrame;
				s_profilerSelEnd = s_profilerHoverFrame + 1;
			}
			if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && s_profilerHoverFrame >= 0) {
				int dragStart = s_profilerSelStart;
				int dragEnd = s_profilerHoverFrame + 1;
				if (dragEnd <= dragStart) {
					s_profilerSelStart = dragEnd - 1;
					s_profilerSelEnd = dragStart + 1;
				} else {
					s_profilerSelEnd = dragEnd;
				}
			}
			if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
				ProfilerRemerge(s_profilerSelStart, s_profilerSelEnd);
			}
		} else {
			s_profilerHoverFrame = -1;
		}

		// Draw bars
		for (uint32 i = 0; i < frameCount; i++) {
			float x0 = canvasPos.x + i * zoom;
			float x1 = x0 + std::max(zoom - 1.0f, 1.0f);
			uint32 cyc = s_profilerSession.mpFrames[i]->mTotalCycles;
			float h = std::min((float)cyc / (float)vrange, 1.0f) * barHeight;
			float y1 = canvasPos.y + barHeight;
			float y0 = y1 - h;

			ImU32 color;
			if ((int)i == s_profilerHoverFrame)
				color = IM_COL32(255, 80, 80, 255);
			else if ((int)i >= s_profilerSelStart && (int)i < s_profilerSelEnd)
				color = IM_COL32(100, 150, 255, 255);
			else
				color = IM_COL32(128, 128, 128, 200);

			dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), color);
		}
	}
	ImGui::EndChild();
}

// --- Phase 3: Call Graph Tree View ---
static void DrawProfilerCallGraphTree() {
	if (!s_profilerMerged || s_profilerSession.mContexts.empty())
		return;

	const auto& contexts = s_profilerSession.mContexts;
	const auto& cgRecords = s_profilerMerged->mCallGraphRecords;
	const auto& inclRecords = s_profilerMerged->mInclusiveRecords;
	uint32 numContexts = (uint32)contexts.size();

	if (cgRecords.size() < numContexts || inclRecords.size() < numContexts)
		return;

	uint32 totalCycles = s_profilerMerged->mTotalCycles;
	uint32 totalInsns = s_profilerMerged->mTotalInsns;
	float cyclePctScale = totalCycles ? 100.0f / (float)totalCycles : 0;
	float insnPctScale = totalInsns ? 100.0f / (float)totalInsns : 0;

	IATDebuggerSymbolLookup *dbs = ATGetDebuggerSymbolLookup();

	// Build first-child / next-sibling lists
	std::vector<int> firstChild(numContexts, -1);
	std::vector<int> nextSibling(numContexts, -1);

	// Children are sorted by inclusive cycles descending, so we collect then sort
	std::vector<std::vector<int>> childLists(numContexts);
	for (uint32 i = 0; i < numContexts; i++) {
		uint32 parent = contexts[i].mParent;
		if (parent < numContexts && parent != i)
			childLists[parent].push_back((int)i);
	}
	for (uint32 i = 0; i < numContexts; i++) {
		auto& ch = childLists[i];
		if (ch.empty()) continue;
		std::sort(ch.begin(), ch.end(), [&](int a, int b) {
			return inclRecords[a].mInclusiveCycles > inclRecords[b].mInclusiveCycles;
		});
		firstChild[i] = ch[0];
		for (size_t j = 0; j + 1 < ch.size(); j++)
			nextSibling[ch[j]] = ch[j + 1];
	}

	// Recursive tree drawing lambda
	std::function<void(int)> drawNode = [&](int idx) {
		if (idx < 0 || idx >= (int)numContexts)
			return;

		const auto& cgr = cgRecords[idx];
		const auto& cgir = inclRecords[idx];

		// Skip roots with zero data
		if (cgir.mInclusiveInsns == 0 && cgir.mInclusiveCycles == 0)
			return;

		// Build label
		char label[256];
		const char *rootNames[] = { "Main", "Interrupt", "IRQ", "VBI", "DLI" };
		if (idx < 5) {
			snprintf(label, sizeof(label), "%s", rootNames[idx]);
		} else {
			uint32 addr = contexts[idx].mAddress;
			char addrStr[32];
			if ((addr & kATAddressSpaceMask) == kATAddressSpace_CPU) {
				if (addr >= 0x10000)
					snprintf(addrStr, sizeof(addrStr), "%02X:%04X", addr >> 16, addr & 0xffff);
				else
					snprintf(addrStr, sizeof(addrStr), "$%04X", addr & 0xffff);
			} else {
				snprintf(addrStr, sizeof(addrStr), "%s%04X", ATAddressGetSpacePrefix(addr), addr & kATAddressOffsetMask);
			}

			ATSymbol sym {};
			if (dbs && dbs->LookupSymbol(addr, kATSymbol_Execute, sym) && sym.mpName && sym.mOffset == addr)
				snprintf(label, sizeof(label), "%s (%s)", addrStr, sym.mpName);
			else
				snprintf(label, sizeof(label), "%s", addrStr);
		}

		// Format: label [xN]: inclusive cycles (%), inclusive insns (%)
		char text[512];
		snprintf(text, sizeof(text), "%s [x%u]: %u cyc (%.1f%%), %u cpu, %u insns (%.1f%%)",
			label, cgr.mCalls,
			cgir.mInclusiveCycles, (float)cgir.mInclusiveCycles * cyclePctScale,
			cgir.mInclusiveUnhaltedCycles,
			cgir.mInclusiveInsns, (float)cgir.mInclusiveInsns * insnPctScale);

		bool hasChildren = (firstChild[idx] >= 0);
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAvailWidth;
		if (!hasChildren)
			flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

		bool open = ImGui::TreeNodeEx((void*)(intptr_t)idx, flags, "%s", text);

		// Click to navigate disassembly (non-root nodes)
		if (ImGui::IsItemClicked() && idx >= 5) {
			uint32 addr = contexts[idx].mAddress;
			s_disasmAddr = addr & 0xFFFF;
			s_disasmFollowPC = false;
			snprintf(s_disasmAddrBuf, sizeof(s_disasmAddrBuf), "%04X", addr & 0xFFFF);
			s_showDisassembly = true;
		}

		if (hasChildren && open) {
			int child = firstChild[idx];
			while (child >= 0) {
				drawNode(child);
				child = nextSibling[child];
			}
			ImGui::TreePop();
		}
	};

	// Draw root nodes: Main(0), Interrupt(1), IRQ(2), VBI(3), DLI(4)
	for (int i = 0; i < 5 && i < (int)numContexts; i++) {
		drawNode(i);
	}
}

// --- Phase 5: CSV Export ---
static void ProfilerExportCSV() {
	const char *home = getenv("HOME");
	std::string path;
	if (home) {
		path = std::string(home) + "/.config/altirra/profile_export.csv";
	} else {
		path = "profile_export.csv";
	}

	std::ofstream f(path);
	if (!f.is_open())
		return;

	bool hasC1 = (s_profilerC1 != kATProfileCounterMode_None);
	bool hasC2 = (s_profilerC2 != kATProfileCounterMode_None);

	// Header
	f << "Context,Address,Symbol,Calls,Cycles,Cycles%,Insns,Insns%,CPU Cycles,DMA%,CPI";
	if (hasC1) f << "," << kCounterModeNames[(int)s_profilerC1];
	if (hasC2) f << "," << kCounterModeNames[(int)s_profilerC2];
	f << "\n";

	uint32 totalCycles = s_profilerMerged ? s_profilerMerged->mTotalCycles : 1;
	uint32 totalInsns = s_profilerMerged ? s_profilerMerged->mTotalInsns : 1;
	if (totalCycles == 0) totalCycles = 1;
	if (totalInsns == 0) totalInsns = 1;

	for (const ProfileEntry& e : s_profilerEntries) {
		float cyclePct = (float)e.cycles * 100.0f / (float)totalCycles;
		float insnPct = (float)e.insns * 100.0f / (float)totalInsns;
		float dmaPct = e.cycles ? 100.0f * (1.0f - (float)e.unhaltedCycles / (float)e.cycles) : 0;
		float cpi = e.insns ? (float)e.cycles / (float)e.insns : 0;

		// Escape symbol for CSV
		std::string sym = e.sym;
		if (sym.find(',') != std::string::npos || sym.find('"') != std::string::npos) {
			std::string escaped;
			escaped += '"';
			for (char c : sym) {
				if (c == '"') escaped += '"';
				escaped += c;
			}
			escaped += '"';
			sym = escaped;
		}

		char addrStr[16];
		snprintf(addrStr, sizeof(addrStr), "$%04X", e.addr);

		f << ProfilerContextName(e.context) << ","
		  << addrStr << ","
		  << sym << ","
		  << e.calls << ","
		  << e.cycles << ","
		  << std::fixed << std::setprecision(2) << cyclePct << ","
		  << e.insns << ","
		  << std::fixed << std::setprecision(2) << insnPct << ","
		  << e.unhaltedCycles << ","
		  << std::fixed << std::setprecision(1) << dmaPct << ","
		  << std::fixed << std::setprecision(2) << cpi;
		if (hasC1) f << "," << e.counters[0];
		if (hasC2) f << "," << e.counters[1];
		f << "\n";
	}
}

static void DrawProfiler() {
	if (!s_showProfiler)
		return;

	ImGui::SetNextWindowSize(ImVec2(800, 550), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Profiler", &s_showProfiler)) {
		ImGui::End();
		return;
	}

	// Mode selection (disabled while running)
	ImGui::Text("Mode:");
	ImGui::SameLine();
	for (int i = 0; i < kATProfileModeCount; i++) {
		if (i > 0)
			ImGui::SameLine();
		ImGui::BeginDisabled(s_profilerRunning);
		if (ImGui::RadioButton(kProfileModeNames[i], (int)s_profilerMode == i))
			s_profilerMode = (ATProfileMode)i;
		ImGui::EndDisabled();
	}

	// Counter mode dropdowns
	ImGui::BeginDisabled(s_profilerRunning);
	ImGui::SetNextItemWidth(140);
	if (ImGui::BeginCombo("Counter 1", kCounterModeNames[(int)s_profilerC1])) {
		for (int i = 0; i <= (int)kATProfileCounterMode_RedundantOp; i++) {
			bool sel = ((int)s_profilerC1 == i);
			if (ImGui::Selectable(kCounterModeNames[i], sel))
				s_profilerC1 = (ATProfileCounterMode)i;
			if (sel)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	ImGui::SameLine();
	ImGui::SetNextItemWidth(140);
	if (ImGui::BeginCombo("Counter 2", kCounterModeNames[(int)s_profilerC2])) {
		for (int i = 0; i <= (int)kATProfileCounterMode_RedundantOp; i++) {
			bool sel = ((int)s_profilerC2 == i);
			if (ImGui::Selectable(kCounterModeNames[i], sel))
				s_profilerC2 = (ATProfileCounterMode)i;
			if (sel)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
	ImGui::EndDisabled();

	// --- Phase 4: Boundary rules (collapsible, disabled while recording) ---
	ImGui::BeginDisabled(s_profilerRunning);
	if (ImGui::CollapsingHeader("Options")) {
		ImGui::Text("Frame Boundary:");
		int rule = (int)s_profilerBoundaryRule;
		if (ImGui::RadioButton("None", &rule, (int)kATProfileBoundaryRule_None))
			s_profilerBoundaryRule = (ATProfileBoundaryRule)rule;
		ImGui::SameLine();
		if (ImGui::RadioButton("VBlank", &rule, (int)kATProfileBoundaryRule_VBlank))
			s_profilerBoundaryRule = (ATProfileBoundaryRule)rule;
		ImGui::SameLine();
		if (ImGui::RadioButton("PC Address", &rule, (int)kATProfileBoundaryRule_PCAddress))
			s_profilerBoundaryRule = (ATProfileBoundaryRule)rule;
		ImGui::SameLine();
		if (ImGui::RadioButton("PC Addr (Func Return)", &rule, (int)kATProfileBoundaryRule_PCAddressFunction))
			s_profilerBoundaryRule = (ATProfileBoundaryRule)rule;

		if (s_profilerBoundaryRule == kATProfileBoundaryRule_PCAddress ||
			s_profilerBoundaryRule == kATProfileBoundaryRule_PCAddressFunction) {
			ImGui::SetNextItemWidth(120);
			ImGui::InputText("Address##boundary", s_profilerBoundaryAddr, sizeof(s_profilerBoundaryAddr));
			if (s_profilerBoundaryRule == kATProfileBoundaryRule_PCAddress) {
				ImGui::SameLine();
				ImGui::SetNextItemWidth(120);
				ImGui::InputText("End Addr##boundary2", s_profilerBoundaryAddr2, sizeof(s_profilerBoundaryAddr2));
			}
		}

		ImGui::Checkbox("Global Addresses (65C816)", &s_profilerGlobalAddresses);
	}
	ImGui::EndDisabled();

	// Start/Stop button
	if (s_profilerRunning) {
		if (ImGui::Button("Stop Profiling")) {
			ProfilerStop();
		}
		ImGui::SameLine();
		ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "RECORDING");
	} else {
		if (ImGui::Button("Start Profiling")) {
			ProfilerStart();
		}
	}

	// Export CSV button
	if (s_profilerHasData && !s_profilerRunning) {
		ImGui::SameLine();
		if (ImGui::Button("Export CSV")) {
			ProfilerExportCSV();
			extern void ATImGuiShowToast(const char *message);
			ATImGuiShowToast("Profile exported to ~/.config/altirra/profile_export.csv");
		}
	}

	// Summary line
	if (s_profilerHasData && s_profilerMerged) {
		uint32 frameCount = (uint32)s_profilerSession.mpFrames.size();
		if (s_profilerSelEnd - s_profilerSelStart < (int)frameCount && frameCount > 1)
			ImGui::Text("Frames: %u (selected %d-%d)  |  Cycles: %u  |  Insns: %u  |  Entries: %u",
				frameCount, s_profilerSelStart, s_profilerSelEnd - 1,
				s_profilerMerged->mTotalCycles, s_profilerMerged->mTotalInsns,
				(uint32)s_profilerEntries.size());
		else
			ImGui::Text("Frames: %u  |  Cycles: %u  |  Insns: %u  |  Entries: %u",
				frameCount, s_profilerMerged->mTotalCycles, s_profilerMerged->mTotalInsns,
				(uint32)s_profilerEntries.size());
	}

	// --- Phase 2: Timeline ---
	if (s_profilerHasData) {
		DrawProfilerTimeline();
	}

	ImGui::Separator();

	// --- Phase 3: Call Graph tree (when in call graph mode) ---
	if (s_profilerHasData && s_profilerMode == kATProfileMode_CallGraph
		&& s_profilerMerged && !s_profilerSession.mContexts.empty()) {
		DrawProfilerCallGraphTree();
		ImGui::Separator();
	}

	// --- Phase 1: Extended results table ---
	if (s_profilerHasData && !s_profilerEntries.empty()) {
		uint32 totalCycles = s_profilerMerged ? s_profilerMerged->mTotalCycles : 1;
		uint32 totalInsns = s_profilerMerged ? s_profilerMerged->mTotalInsns : 1;
		if (totalCycles == 0) totalCycles = 1;
		if (totalInsns == 0) totalInsns = 1;

		bool hasC1 = (s_profilerC1 != kATProfileCounterMode_None);
		bool hasC2 = (s_profilerC2 != kATProfileCounterMode_None);
		int numCols = 9; // Context, Addr, Symbol, Calls, Cycles, Insns, CPU Cyc, DMA%, CPI
		if (hasC1) numCols++;
		if (hasC2) numCols++;

		// Reserve space for selection status bar below the table
		float statusBarHeight = s_profilerSelectedRows.empty() ? 0 : ImGui::GetTextLineHeightWithSpacing() + 4;

		if (ImGui::BeginTable("##profresults", numCols,
			ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY
				| ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable
				| ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_Hideable,
			ImVec2(0, -statusBarHeight)))
		{
			ImGui::TableSetupColumn("Ctx", ImGuiTableColumnFlags_DefaultHide, 0.5f);
			ImGui::TableSetupColumn("Address", 0, 1.0f);
			ImGui::TableSetupColumn("Symbol", 0, 2.0f);
			ImGui::TableSetupColumn("Calls", 0, 0.8f);
			ImGui::TableSetupColumn("Cycles", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending, 1.5f);
			ImGui::TableSetupColumn("Insns", 0, 1.0f);
			ImGui::TableSetupColumn("CPU Cyc", ImGuiTableColumnFlags_DefaultHide, 1.0f);
			ImGui::TableSetupColumn("DMA%", ImGuiTableColumnFlags_DefaultHide, 0.7f);
			ImGui::TableSetupColumn("CPI", ImGuiTableColumnFlags_DefaultHide, 0.6f);
			if (hasC1)
				ImGui::TableSetupColumn(kCounterModeNames[(int)s_profilerC1], ImGuiTableColumnFlags_DefaultHide, 0.8f);
			if (hasC2)
				ImGui::TableSetupColumn(kCounterModeNames[(int)s_profilerC2], ImGuiTableColumnFlags_DefaultHide, 0.8f);
			ImGui::TableSetupScrollFreeze(0, 1);
			ImGui::TableHeadersRow();

			// Handle sorting
			if (ImGuiTableSortSpecs *sorts = ImGui::TableGetSortSpecs()) {
				if (sorts->SpecsDirty && sorts->SpecsCount > 0) {
					s_profilerSortCol = sorts->Specs[0].ColumnIndex;
					s_profilerSortAsc = (sorts->Specs[0].SortDirection == ImGuiSortDirection_Ascending);
					ProfilerSort();
					sorts->SpecsDirty = false;
				}
			}

			// Use clipper for large result sets
			ImGuiListClipper clipper;
			clipper.Begin((int)s_profilerEntries.size());
			while (clipper.Step()) {
				for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
					const ProfileEntry& e = s_profilerEntries[row];
					float cyclePct = (float)e.cycles * 100.0f / (float)totalCycles;
					float insnPct = (float)e.insns * 100.0f / (float)totalInsns;
					float dmaPct = e.cycles ? 100.0f * (1.0f - (float)e.unhaltedCycles / (float)e.cycles) : 0;
					float cpi = e.insns ? (float)e.cycles / (float)e.insns : 0;

					ImGui::TableNextRow();

					// Context column
					ImGui::TableNextColumn();
					bool isSelected = s_profilerSelectedRows.count(row) > 0;

					// Row ID for selection
					char rowId[32];
					snprintf(rowId, sizeof(rowId), "##pr%d", row);

					if (ImGui::Selectable(rowId, isSelected,
						ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap
							| ImGuiSelectableFlags_AllowDoubleClick)) {
						if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
							// Double-click: open function detail window
							s_profilerDetailAddr = e.addr;
							s_profilerDetailName = e.sym;
							ProfilerDetailBuild(e.addr);
							s_showProfilerDetail = true;
						} else if (ImGui::GetIO().KeyCtrl) {
							// Ctrl+click: toggle
							if (isSelected)
								s_profilerSelectedRows.erase(row);
							else
								s_profilerSelectedRows.insert(row);
						} else if (ImGui::GetIO().KeyShift && s_profilerLastClickedRow >= 0) {
							// Shift+click: range select
							int lo = std::min(s_profilerLastClickedRow, row);
							int hi = std::max(s_profilerLastClickedRow, row);
							for (int r = lo; r <= hi; r++)
								s_profilerSelectedRows.insert(r);
						} else {
							// Plain click: single select + navigate
							s_profilerSelectedRows.clear();
							s_profilerSelectedRows.insert(row);
							s_disasmAddr = e.addr;
							s_disasmFollowPC = false;
							snprintf(s_disasmAddrBuf, sizeof(s_disasmAddrBuf), "%04X", e.addr);
							s_showDisassembly = true;
						}
						s_profilerLastClickedRow = row;
					}
					ImGui::SameLine();
					ImGui::TextUnformatted(ProfilerContextName(e.context));

					// Address
					ImGui::TableNextColumn();
					ImGui::Text("$%04X", e.addr);

					// Symbol
					ImGui::TableNextColumn();
					if (!e.sym.empty())
						ImGui::Text("%s", e.sym.c_str());
					else
						ImGui::TextDisabled("---");

					// Calls
					ImGui::TableNextColumn();
					ImGui::Text("%u", e.calls);

					// Cycles (with %)
					ImGui::TableNextColumn();
					ImGui::Text("%u (%.1f%%)", e.cycles, cyclePct);

					// Insns (with %)
					ImGui::TableNextColumn();
					ImGui::Text("%u (%.1f%%)", e.insns, insnPct);

					// CPU Cycles (unhalted)
					ImGui::TableNextColumn();
					ImGui::Text("%u", e.unhaltedCycles);

					// DMA%
					ImGui::TableNextColumn();
					ImGui::Text("%.1f%%", dmaPct);

					// CPI
					ImGui::TableNextColumn();
					ImGui::Text("%.2f", cpi);

					// Counter 1
					if (hasC1) {
						ImGui::TableNextColumn();
						ImGui::Text("%u", e.counters[0]);
					}

					// Counter 2
					if (hasC2) {
						ImGui::TableNextColumn();
						ImGui::Text("%u", e.counters[1]);
					}
				}
			}

			ImGui::EndTable();
		}

		// --- Phase 6: Selection status bar ---
		if (!s_profilerSelectedRows.empty()) {
			uint64_t selCycles = 0, selInsns = 0;
			for (int r : s_profilerSelectedRows) {
				if (r >= 0 && r < (int)s_profilerEntries.size()) {
					selCycles += s_profilerEntries[r].cycles;
					selInsns += s_profilerEntries[r].insns;
				}
			}
			float selCyclePct = (float)selCycles * 100.0f / (float)totalCycles;
			float selInsnPct = (float)selInsns * 100.0f / (float)totalInsns;
			ImGui::Text("Selected %d items: %llu cycles (%.1f%%), %llu insns (%.1f%%)",
				(int)s_profilerSelectedRows.size(),
				(unsigned long long)selCycles, selCyclePct,
				(unsigned long long)selInsns, selInsnPct);
		}
	} else if (!s_profilerRunning) {
		ImGui::TextDisabled("No profiling data. Click \"Start Profiling\" to begin.");
	}

	ImGui::End();
}

// ============= Trace Viewer =============

static void DrawTraceViewer() {
	if (!s_showTrace)
		return;

	ImGui::SetNextWindowSize(ImVec2(800, 400), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Trace Viewer", &s_showTrace)) {
		ImGui::End();
		return;
	}

	// Controls bar
	if (!s_traceRecording) {
		if (ImGui::Button("Start Recording")) {
			ATTraceSettings settings {};
			settings.mbTraceCpuInsns = s_traceOptCpu;
			settings.mbTraceVideo = s_traceOptVideo;
			settings.mbTraceBasic = s_traceOptBasic;
			g_sim.StartTracing(settings);
			s_traceRecording = true;
			s_traceHasData = false;
		}
	} else {
		if (ImGui::Button("Stop Recording")) {
			// Capture the collection BEFORE StopTracing destroys the context
			s_traceCollection = g_sim.GetTraceCollection();
			g_sim.StopTracing();
			s_traceRecording = false;

			if (s_traceCollection && s_traceCollection->GetGroupCount() > 0) {
				s_traceHasData = true;

				// Compute total duration across all channels
				s_traceTotalDuration = 0.0;
				for (size_t gi = 0; gi < s_traceCollection->GetGroupCount(); ++gi) {
					ATTraceGroup *grp = s_traceCollection->GetGroup(gi);
					double d = grp->GetDuration();
					if (d > s_traceTotalDuration)
						s_traceTotalDuration = d;
				}

				s_traceViewStart = 0.0;
				s_traceViewEnd = s_traceTotalDuration > 0 ? s_traceTotalDuration : 1.0;
			}
		}
	}

	ImGui::SameLine();
	ImGui::BeginDisabled(s_traceRecording);
	ImGui::Checkbox("CPU", &s_traceOptCpu);
	ImGui::SameLine();
	ImGui::Checkbox("Video", &s_traceOptVideo);
	ImGui::SameLine();
	ImGui::Checkbox("BASIC", &s_traceOptBasic);
	ImGui::EndDisabled();

	if (s_traceHasData) {
		ImGui::SameLine();
		if (ImGui::Button("Clear")) {
			s_traceHasData = false;
			s_traceTotalDuration = 0.0;
			s_traceCollection.clear();
		}
		ImGui::SameLine();
		if (ImGui::Button("Zoom Fit")) {
			s_traceViewStart = 0.0;
			s_traceViewEnd = s_traceTotalDuration > 0 ? s_traceTotalDuration : 1.0;
		}
	}

	if (s_traceRecording) {
		ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Recording...");
		ImGui::End();
		return;
	}

	if (!s_traceHasData) {
		ImGui::TextDisabled("No trace data. Click \"Start Recording\" to begin.");
		ImGui::End();
		return;
	}

	ATTraceCollection *tc = s_traceCollection;
	if (!tc) {
		ImGui::TextDisabled("Trace collection not available.");
		ImGui::End();
		return;
	}

	// Timeline child area
	ImGui::BeginChild("##timeline", ImVec2(0, 0), ImGuiChildFlags_None,
		ImGuiWindowFlags_HorizontalScrollbar);

	ImDrawList *dl = ImGui::GetWindowDrawList();
	ImVec2 canvasPos = ImGui::GetCursorScreenPos();
	ImVec2 canvasSize = ImGui::GetContentRegionAvail();
	if (canvasSize.x < 100) canvasSize.x = 100;

	double viewDuration = s_traceViewEnd - s_traceViewStart;
	if (viewDuration <= 0) viewDuration = 1.0;
	double pixelsPerSec = canvasSize.x / viewDuration;

	// Handle zoom/pan with mouse
	ImGuiIO& io = ImGui::GetIO();
	ImVec2 mousePos = io.MousePos;
	bool hovered = ImGui::IsWindowHovered();

	if (hovered && io.MouseWheel != 0.0f) {
		double mouseTime = s_traceViewStart + (mousePos.x - canvasPos.x) / pixelsPerSec;
		double zoomFactor = io.KeyCtrl ? 1.3 : 1.0;

		if (io.KeyCtrl) {
			// Ctrl+wheel: zoom
			if (io.MouseWheel > 0) {
				double newDuration = viewDuration / zoomFactor;
				double ratio = (mouseTime - s_traceViewStart) / viewDuration;
				s_traceViewStart = mouseTime - ratio * newDuration;
				s_traceViewEnd = s_traceViewStart + newDuration;
			} else {
				double newDuration = viewDuration * zoomFactor;
				double ratio = (mouseTime - s_traceViewStart) / viewDuration;
				s_traceViewStart = mouseTime - ratio * newDuration;
				s_traceViewEnd = s_traceViewStart + newDuration;
			}
		} else {
			// Wheel: horizontal scroll
			double scrollAmt = viewDuration * 0.1 * (-io.MouseWheel);
			s_traceViewStart += scrollAmt;
			s_traceViewEnd += scrollAmt;
		}

		// Clamp
		if (s_traceViewStart < 0) {
			s_traceViewEnd -= s_traceViewStart;
			s_traceViewStart = 0;
		}
		if (s_traceViewEnd > s_traceTotalDuration) {
			s_traceViewStart -= (s_traceViewEnd - s_traceTotalDuration);
			s_traceViewEnd = s_traceTotalDuration;
			if (s_traceViewStart < 0) s_traceViewStart = 0;
		}
	}

	// Middle-drag pan
	if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
		double dx = io.MouseDelta.x / pixelsPerSec;
		s_traceViewStart -= dx;
		s_traceViewEnd -= dx;
	}

	// Timescale ruler (24px)
	const float rulerH = 24.0f;
	dl->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + rulerH),
		IM_COL32(32, 32, 32, 255));

	// Draw tick marks
	{
		double tickSpacing = 1.0;
		const char *tickFmt = "%.0fs";
		double pixPerTick = tickSpacing * pixelsPerSec;
		if (pixPerTick < 60) { tickSpacing = 0.1; tickFmt = "%.1fs"; pixPerTick = tickSpacing * pixelsPerSec; }
		if (pixPerTick < 60) { tickSpacing = 0.01; tickFmt = "%.2fs"; pixPerTick = tickSpacing * pixelsPerSec; }
		if (pixPerTick < 60) { tickSpacing = 0.001; tickFmt = "%.3fs"; pixPerTick = tickSpacing * pixelsPerSec; }
		if (pixPerTick > 300) { tickSpacing *= 5; pixPerTick = tickSpacing * pixelsPerSec; }

		double firstTick = std::ceil(s_traceViewStart / tickSpacing) * tickSpacing;
		for (double t = firstTick; t <= s_traceViewEnd; t += tickSpacing) {
			float x = canvasPos.x + (float)((t - s_traceViewStart) * pixelsPerSec);
			dl->AddLine(ImVec2(x, canvasPos.y + rulerH - 6), ImVec2(x, canvasPos.y + rulerH), IM_COL32(180, 180, 180, 255));
			char buf[32];
			snprintf(buf, sizeof(buf), tickFmt, t);
			dl->AddText(ImVec2(x + 2, canvasPos.y + 2), IM_COL32(200, 200, 200, 255), buf);
		}
	}

	// Channel rows
	const float groupH = 22.0f;
	const float chanH = 20.0f;
	float y = canvasPos.y + rulerH;

	s_traceHoverChannel = -1;
	int channelIdx = 0;
	char tooltipBuf[256] = {};

	for (size_t gi = 0; gi < tc->GetGroupCount(); ++gi) {
		ATTraceGroup *grp = tc->GetGroup(gi);
		VDStringA groupName = VDTextWToU8(VDStringW(grp->GetName()));

		// Group header
		dl->AddRectFilled(ImVec2(canvasPos.x, y), ImVec2(canvasPos.x + canvasSize.x, y + groupH),
			IM_COL32(50, 50, 60, 255));
		dl->AddText(ImVec2(canvasPos.x + 4, y + 3), IM_COL32(220, 220, 255, 255), groupName.c_str());
		y += groupH;

		for (size_t ci = 0; ci < grp->GetChannelCount(); ++ci) {
			IATTraceChannel *ch = grp->GetChannel(ci);
			VDStringA chName = VDTextWToU8(VDStringW(ch->GetName()));

			// Channel background
			ImU32 bgCol = (channelIdx & 1) ? IM_COL32(34, 36, 36, 255) : IM_COL32(38, 40, 40, 255);
			dl->AddRectFilled(ImVec2(canvasPos.x, y), ImVec2(canvasPos.x + canvasSize.x, y + chanH), bgCol);

			// Channel name (left side)
			dl->AddText(ImVec2(canvasPos.x + 2, y + 2), IM_COL32(160, 160, 160, 255), chName.c_str());

			// Draw events
			double threshold = viewDuration / canvasSize.x;
			ch->StartIteration(s_traceViewStart, s_traceViewEnd, threshold);

			ATTraceEvent ev;
			while (ch->GetNextEvent(ev)) {
				float x0 = canvasPos.x + (float)((ev.mEventStart - s_traceViewStart) * pixelsPerSec);
				float x1 = canvasPos.x + (float)((ev.mEventStop - s_traceViewStart) * pixelsPerSec);

				if (x1 < canvasPos.x || x0 > canvasPos.x + canvasSize.x)
					continue;
				if (x0 < canvasPos.x) x0 = canvasPos.x;
				if (x1 > canvasPos.x + canvasSize.x) x1 = canvasPos.x + canvasSize.x;
				if (x1 - x0 < 1.0f) x1 = x0 + 1.0f;

				// Convert trace colors (0xRRGGBB) to ImGui colors
				uint32 bg = ev.mBgColor;
				ImU32 col = IM_COL32((bg >> 16) & 0xFF, (bg >> 8) & 0xFF, bg & 0xFF, 220);
				dl->AddRectFilled(ImVec2(x0, y + 1), ImVec2(x1, y + chanH - 1), col);

				// Draw text label if bar is wide enough
				if (x1 - x0 > 40.0f && ev.mpName) {
					VDStringA label = VDTextWToU8(VDStringW(ev.mpName));
					ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
					if (textSize.x < x1 - x0 - 4) {
						uint32 fg = ev.mFgColor;
						ImU32 fgCol = IM_COL32((fg >> 16) & 0xFF, (fg >> 8) & 0xFF, fg & 0xFF, 255);
						dl->AddText(ImVec2(x0 + 2, y + 2), fgCol, label.c_str());
					}
				}

				// Tooltip on hover
				if (mousePos.y >= y && mousePos.y < y + chanH &&
					mousePos.x >= x0 && mousePos.x < x1 && hovered) {
					VDStringA name = ev.mpName ? VDTextWToU8(VDStringW(ev.mpName)) : VDStringA("(unnamed)");
					snprintf(tooltipBuf, sizeof(tooltipBuf),
						"%s\nStart: %.6fs\nEnd: %.6fs\nDuration: %.6fs",
						name.c_str(), ev.mEventStart, ev.mEventStop,
						ev.mEventStop - ev.mEventStart);
				}
			}

			y += chanH;
			++channelIdx;
		}
	}

	// Set dummy to establish scroll range
	ImGui::Dummy(ImVec2(canvasSize.x, y - canvasPos.y));

	ImGui::EndChild();

	if (tooltipBuf[0] && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
		ImGui::BeginTooltip();
		ImGui::TextUnformatted(tooltipBuf);
		ImGui::EndTooltip();
	}

	ImGui::End();
}

// ============= Debug Display (ANTIC) =============

static void DrawDebugDisplay() {
	if (!s_showDebugDisplay)
		return;

	ImGui::SetNextWindowSize(ImVec2(400, 310), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Debug Display", &s_showDebugDisplay)) {
		// Cleanup when closed
		if (!s_showDebugDisplay && s_debugDisplay) {
			s_debugDisplay->Shutdown();
			delete s_debugDisplay;
			s_debugDisplay = nullptr;
			if (s_debugDisplayTex) {
				glDeleteTextures(1, &s_debugDisplayTex);
				s_debugDisplayTex = 0;
			}
			s_debugDisplayRGBA.clear();
		}
		ImGui::End();
		return;
	}

	// Lazy init
	if (!s_debugDisplay) {
		s_debugDisplay = new ATDebugDisplay;
		s_debugDisplay->Init(g_sim.GetMemoryManager(), &g_sim.GetAntic(), &g_sim.GetGTIA(), nullptr);
		s_debugDisplayRGBA.resize(376 * 240);

		glGenTextures(1, &s_debugDisplayTex);
		glBindTexture(GL_TEXTURE_2D, s_debugDisplayTex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 376, 240, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	// Controls
	const char *modeNames[] = { "ANTIC History", "ANTIC History Start" };
	ImGui::Combo("Mode", &s_debugDisplayMode, modeNames, 2);

	const char *paletteNames[] = { "Registers", "Analysis" };
	ImGui::SameLine();
	ImGui::Combo("Palette", &s_debugDisplayPalette, paletteNames, 2);

	// Update display
	s_debugDisplay->SetMode((ATDebugDisplay::Mode)s_debugDisplayMode);
	s_debugDisplay->SetPaletteMode((ATDebugDisplay::PaletteMode)s_debugDisplayPalette);
	s_debugDisplay->Update();

	// Convert Pal8 to RGBA
	const VDPixmapBuffer& buf = s_debugDisplay->GetFrameBuffer();
	if (buf.data && buf.palette) {
		for (int row = 0; row < 240; ++row) {
			const uint8 *src = (const uint8 *)buf.data + buf.pitch * row;
			uint32 *dst = s_debugDisplayRGBA.data() + 376 * row;
			for (int x = 0; x < 376; ++x) {
				uint32 pal = buf.palette[src[x]];
				// Palette is 0x00RRGGBB, convert to 0xFFRRGGBB (RGBA for GL)
				dst[x] = (pal & 0x000000FF) << 16    // B -> pos 16
						| (pal & 0x0000FF00)          // G stays
						| (pal & 0x00FF0000) >> 16    // R -> pos 0
						| 0xFF000000u;                // A=255
			}
		}

		glBindTexture(GL_TEXTURE_2D, s_debugDisplayTex);
		glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 376, 240, GL_RGBA, GL_UNSIGNED_BYTE, s_debugDisplayRGBA.data());
		glBindTexture(GL_TEXTURE_2D, 0);
	}

	ImGui::Image((ImTextureID)(uintptr_t)s_debugDisplayTex, ImVec2(376, 240));

	ImGui::End();
}

// ============= Performance Overlay =============

static void DrawPerformanceOverlay() {
	if (!s_showPerformance)
		return;

	// Register profiler on open
	if (!s_pProfiler) {
		s_pProfiler = new ATImGuiProfiler;
		s_pProfiler->mFrameStartTime = VDGetPreciseTick();
		s_pProfiler->mTicksToPixels = VDGetPreciseSecondsPerTick() * (double)ATImGuiProfiler::kHeight * 30.0;
		g_pATProfiler = s_pProfiler;
	}

	constexpr int kW = ATImGuiProfiler::kWidth;
	constexpr int kH = ATImGuiProfiler::kHeight;

	ImGui::SetNextWindowSize(ImVec2(280, 340), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("Performance", &s_showPerformance)) {
		ImGui::End();
		return;
	}

	// Deregister on close
	if (!s_showPerformance) {
		if (s_pProfiler) {
			if (g_pATProfiler == s_pProfiler)
				g_pATProfiler = nullptr;
			delete s_pProfiler;
			s_pProfiler = nullptr;
		}
		ImGui::End();
		return;
	}

	// Colors matching the Windows profiler
	static constexpr ImU32 kRegionColors[kATProfileRegionCount] = {
		IM_COL32(128, 128, 128, 255),  // Idle - gray
		IM_COL32(255, 255, 255, 255),  // IdleFrameDelay - white
		IM_COL32(64, 96, 224, 255),    // Simulation - blue
		IM_COL32(224, 32, 16, 255),    // NativeEvents - red
		IM_COL32(0, 0, 0, 0),          // NativeMessage - not shown
		IM_COL32(0, 0, 0, 0),          // DisplayPost - not shown
		IM_COL32(32, 224, 16, 255),    // DisplayTick - green
		IM_COL32(255, 224, 16, 255),   // DisplayPresent - yellow
	};

	static const char *kRegionNames[kATProfileRegionCount] = {
		"Idle",
		"Idle (frame delay)",
		"Simulation",
		"Native events",
		nullptr,
		nullptr,
		"Display tick",
		"Display present",
	};

	ImVec2 canvasPos = ImGui::GetCursorScreenPos();
	ImVec2 canvasSize = ImVec2((float)kW, (float)kH);
	ImDrawList *dl = ImGui::GetWindowDrawList();

	// Background
	dl->AddRectFilled(canvasPos, ImVec2(canvasPos.x + kW, canvasPos.y + kH), IM_COL32(0, 0, 0, 192));

	// Draw stacked columns — iterate from oldest to newest
	int curX = s_pProfiler->mX;
	for (int col = 0; col < kW; col++) {
		int idx = (curX + 1 + col) & (kW - 1);
		const auto& c = s_pProfiler->mColumns[idx];

		float x0 = canvasPos.x + (float)col;
		float x1 = x0 + 1.0f;
		float y = canvasPos.y + (float)kH;  // bottom

		for (int r = 0; r < kATProfileRegionCount; r++) {
			if (c.regionPixels[r] <= 0)
				continue;
			if (kRegionColors[r] == 0)
				continue;

			float h = (float)c.regionPixels[r];
			if (h > (float)kH)
				h = (float)kH;
			float y0 = y - h;
			dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y), kRegionColors[r]);
			y = y0;
		}
	}

	// Border
	dl->AddRect(canvasPos, ImVec2(canvasPos.x + kW, canvasPos.y + kH), IM_COL32(255, 255, 255, 80));

	ImGui::Dummy(canvasSize);

	// Legend
	ImGui::Spacing();
	for (int i = 0; i < kATProfileRegionCount; i++) {
		if (!kRegionNames[i] || kRegionColors[i] == 0)
			continue;

		ImVec2 p = ImGui::GetCursorScreenPos();
		dl->AddRectFilled(p, ImVec2(p.x + 12, p.y + 12), kRegionColors[i]);
		ImGui::Dummy(ImVec2(16, 12));
		ImGui::SameLine();
		ImGui::Text("%s", kRegionNames[i]);
	}

	ImGui::End();
}

// ============= Profiler Detail Window =============

static void ProfilerDetailBuild(uint32 funcAddr) {
	s_profilerDetailEntries.clear();
	if (!s_profilerMerged)
		return;

	IATDebuggerSymbolLookup *dbs = ATGetDebuggerSymbolLookup();

	// Determine address range from symbol table or use a default 256-byte range
	uint32 startAddr = funcAddr;
	uint32 endAddr = funcAddr + 0x100;

	if (dbs) {
		ATSymbol sym {};
		if (dbs->LookupSymbol(funcAddr, kATSymbol_Execute, sym) && sym.mOffset == funcAddr) {
			startAddr = sym.mOffset;

			// Find the next symbol to determine the end of this function
			ATSymbol nextSym {};
			if (dbs->LookupSymbol(startAddr + 1, kATSymbol_Execute, nextSym)
				&& nextSym.mOffset > startAddr && nextSym.mOffset != startAddr)
				endAddr = nextSym.mOffset;
		}
	}

	// Collect records matching this address range
	for (const ATProfileRecord& rec : s_profilerMerged->mRecords) {
		uint32 addr = rec.mAddress;
		if (addr >= startAddr && addr < endAddr) {
			ProfileDetailEntry e;
			e.addr = addr;
			e.cycles = rec.mCycles;
			e.insns = rec.mInsns;
			e.unhaltedCycles = rec.mUnhaltedCycles;

			// Disassemble the instruction at this address
			{
				VDStringA buf;
				ATDisassembleInsn(buf, (uint16)addr, false);
				e.disasm = std::string(buf.c_str(), buf.size());
			}

			s_profilerDetailEntries.push_back(std::move(e));
		}
	}

	// Sort by address
	std::sort(s_profilerDetailEntries.begin(), s_profilerDetailEntries.end(),
		[](const ProfileDetailEntry& a, const ProfileDetailEntry& b) {
			return a.addr < b.addr;
		});
}

static void DrawProfilerDetail() {
	if (!s_showProfilerDetail)
		return;

	char title[256];
	if (!s_profilerDetailName.empty())
		snprintf(title, sizeof(title), "Function: %s ($%04X)###profdetail", s_profilerDetailName.c_str(), s_profilerDetailAddr);
	else
		snprintf(title, sizeof(title), "Function: $%04X###profdetail", s_profilerDetailAddr);

	ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin(title, &s_showProfilerDetail)) {
		ImGui::End();
		return;
	}

	if (s_profilerDetailEntries.empty()) {
		ImGui::TextDisabled("No instruction data for this address range.");
		ImGui::End();
		return;
	}

	// Compute totals for percentage display
	uint32 totalCycles = 0, totalInsns = 0;
	for (const auto& e : s_profilerDetailEntries) {
		totalCycles += e.cycles;
		totalInsns += e.insns;
	}
	if (totalCycles == 0) totalCycles = 1;
	if (totalInsns == 0) totalInsns = 1;

	ImGui::Text("Total: %u cycles, %u insns", totalCycles, totalInsns);
	ImGui::Separator();

	if (ImGui::BeginTable("##profdetail", 6,
		ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY
			| ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp))
	{
		ImGui::TableSetupColumn("Address", 0, 1.0f);
		ImGui::TableSetupColumn("Cycles", 0, 1.2f);
		ImGui::TableSetupColumn("Insns", 0, 0.8f);
		ImGui::TableSetupColumn("CPI", 0, 0.6f);
		ImGui::TableSetupColumn("DMA%", 0, 0.6f);
		ImGui::TableSetupColumn("Disassembly", 0, 3.0f);
		ImGui::TableSetupScrollFreeze(0, 1);
		ImGui::TableHeadersRow();

		ImGuiListClipper clipper;
		clipper.Begin((int)s_profilerDetailEntries.size());
		while (clipper.Step()) {
			for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++) {
				const auto& e = s_profilerDetailEntries[row];
				float cyclePct = (float)e.cycles * 100.0f / (float)totalCycles;
				float cpi = e.insns ? (float)e.cycles / (float)e.insns : 0;
				float dmaPct = e.cycles ? 100.0f * (1.0f - (float)e.unhaltedCycles / (float)e.cycles) : 0;

				ImGui::TableNextRow();

				ImGui::TableNextColumn();
				ImGui::Text("$%04X", e.addr);

				ImGui::TableNextColumn();
				ImGui::Text("%u (%.1f%%)", e.cycles, cyclePct);

				ImGui::TableNextColumn();
				ImGui::Text("%u", e.insns);

				ImGui::TableNextColumn();
				ImGui::Text("%.2f", cpi);

				ImGui::TableNextColumn();
				ImGui::Text("%.1f%%", dmaPct);

				ImGui::TableNextColumn();
				ImGui::TextUnformatted(e.disasm.c_str());
			}
		}

		ImGui::EndTable();
	}

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
bool& ATImGuiDebuggerShowSourceCode() { return s_showSourceCode; }
bool& ATImGuiDebuggerShowPrinterOutput() { return s_showPrinterOutput; }
bool& ATImGuiDebuggerShowProfiler() { return s_showProfiler; }
bool& ATImGuiDebuggerShowTrace() { return s_showTrace; }
bool& ATImGuiDebuggerShowDebugDisplay() { return s_showDebugDisplay; }
bool& ATImGuiDebuggerShowPerformance() { return s_showPerformance; }

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
	DrawSourceCode();
	DrawPrinterOutput();
	DrawProfiler();
	DrawProfilerDetail();
	DrawTraceViewer();
	DrawDebugDisplay();
	DrawPerformanceOverlay();
}

void ATImGuiDebuggerDraw() {
	DrawToolbar();
	ATImGuiDebuggerDrawWindows();
}
