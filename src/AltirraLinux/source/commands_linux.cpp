//	Altirra - Atari 800/800XL/5200 emulator
//	Linux port - command manager and registration
//
//	This program is free software; you can redistribute it and/or modify
//	it under the terms of the GNU General Public License as published by
//	the Free Software Foundation; either version 2 of the License, or
//	(at your option) any later version.
//
//	This program is distributed in the hope that it will be useful,
//	but WITHOUT ANY WARRANTY; without even the implied warranty of
//	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//	GNU General Public License for more details.
//
//	You should have received a copy of the GNU General Public License along
//	with this program. If not, see <http://www.gnu.org/licenses/>.

// Command manager instance and initialization for the Linux port.
// All command registrations come from the upstream cmds.cpp +
// per-domain cmd*.cpp files via ATUIInitCommandMappings().

#include <at/atui/uicommandmanager.h>

// Global command manager instance (referenced throughout the codebase)
ATUICommandManager g_ATUICommandMgr;

// Defined in cmds.cpp — registers all ~400+ commands from
// kATCommands[] + per-domain ATUIInitCommandMappings*() functions.
extern void ATUIInitCommandMappings(ATUICommandManager& cmdMgr);

void ATLinuxInitCommands() {
	ATUIInitCommandMappings(g_ATUICommandMgr);
}
