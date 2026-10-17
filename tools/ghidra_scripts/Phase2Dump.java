// Phase 2 static questions for Fable3.exe (see docs/findings.md).
// Usage (headless): -postScript Phase2Dump.java <outFile>
//@category Fable3Coop

import java.io.*;
import java.util.*;

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;

public class Phase2Dump extends GhidraScript {

	// Lua-bound names and debug strings tied to the couch co-op path.
	private static final String[] STRINGS = {
		"IsInCouchGame", "SetSharedScreenModeToggle", "AllowHenchman", "IsInMultiplayer",
		"IsInLiveGame", "GetLocalHero", "IsControllerConnected", "IsControllerAvailable",
		"CreateNewSaveForLocalPlayer", "AddLocalPlayers()", "JoinCoop",
		"+++--- No local players when we're starting this session! ---+++",
	};

	// xlive.dll ordinals: XShowSigninUI, XUserGetXUID, XUserGetSigninState,
	// XUserGetName, XUserGetSigninInfo. XINPUT1_3 ordinals: GetState, SetState, GetCapabilities.
	private static final String[][] IMPORTS = {
		{"xlive.dll", "5260"}, {"xlive.dll", "5261"}, {"xlive.dll", "5262"},
		{"xlive.dll", "5263"}, {"xlive.dll", "5267"},
		{"XINPUT1_3.dll", "2"}, {"XINPUT1_3.dll", "3"}, {"XINPUT1_3.dll", "4"},
	};

	private DecompInterface decomp;
	private PrintWriter out;
	private final Set<Address> decompiled = new HashSet<>();

	@Override
	public void run() throws Exception {
		String[] args = getScriptArgs();
		out = new PrintWriter(new FileWriter(args.length > 0 ? args[0] : "phase2.txt"));
		decomp = new DecompInterface();
		decomp.openProgram(currentProgram);
		try {
			for (String s : STRINGS) {
				dumpString(s);
			}
			for (String[] imp : IMPORTS) {
				dumpImport(imp[0], imp[1]);
			}
		}
		finally {
			decomp.dispose();
			out.close();
		}
	}

	private void dumpString(String text) throws Exception {
		out.println("\n######## STRING \"" + text + "\"");
		byte[] needle = (text + "\0").getBytes("US-ASCII");
		Memory mem = currentProgram.getMemory();
		Address a = mem.getMinAddress();
		int hits = 0;
		while (a != null && hits < 8) {
			a = mem.findBytes(a, needle, null, true, monitor);
			if (a == null) {
				break;
			}
			hits++;
			out.println("  at " + a);
			for (Reference r : getReferencesTo(a)) {
				Address from = r.getFromAddress();
				Function f = getFunctionContaining(from);
				out.println("    xref " + from + " in " + (f == null ? "<none>" : f.getName() + "@" + f.getEntryPoint()));
				dumpNeighbourhood(from);
			}
			a = a.add(1);
		}
	}

	// Lua bindings are registered as pushes of (name, handler); show nearby code
	// and decompile any function pointer pushed within a few instructions.
	private void dumpNeighbourhood(Address from) throws Exception {
		Listing listing = currentProgram.getListing();
		Instruction ins = listing.getInstructionAt(from);
		if (ins == null) {
			return;
		}
		for (int i = 0; i < 4 && ins.getPrevious() != null; i++) {
			ins = ins.getPrevious();
		}
		List<Function> targets = new ArrayList<>();
		for (int i = 0; i < 10 && ins != null; i++, ins = ins.getNext()) {
			out.println("      " + ins.getAddress() + "  " + ins);
			for (Reference r : ins.getReferencesFrom()) {
				Function t = getFunctionAt(r.getToAddress());
				if (t != null && !r.getReferenceType().isCall()) {
					targets.add(t);
				}
			}
		}
		for (Function t : targets) {
			decompile(t, "handler candidate");
		}
	}

	private void dumpImport(String dll, String ordinal) throws Exception {
		out.println("\n######## IMPORT " + dll + " #" + ordinal);
		SymbolTable st = currentProgram.getSymbolTable();
		Symbol sym = null;
		for (Symbol s : st.getExternalSymbols()) {
			ExternalLocation loc = currentProgram.getExternalManager().getExternalLocation(s);
			if (loc != null && loc.getLibraryName().equalsIgnoreCase(dll) &&
				(s.getName().equals("Ordinal_" + ordinal) || s.getName().endsWith("_" + ordinal))) {
				sym = s;
				break;
			}
		}
		if (sym == null) {
			out.println("  not found");
			return;
		}
		Set<Function> callers = new LinkedHashSet<>();
		collectCallers(sym.getAddress(), callers, 0);
		out.println("  " + sym.getName() + ": " + callers.size() + " calling functions");
		for (Function f : callers) {
			out.println("    " + f.getName() + "@" + f.getEntryPoint());
		}
		for (Function f : callers) {
			decompile(f, "caller of " + dll + " #" + ordinal);
		}
	}

	// Calls go through the IAT pointer and often a thunk; follow both.
	private void collectCallers(Address target, Set<Function> callers, int depth) {
		for (Reference r : getReferencesTo(target)) {
			Address from = r.getFromAddress();
			Function f = getFunctionContaining(from);
			if (f == null) {
				// IAT slot: follow references to the slot itself.
				if (depth < 2) {
					collectCallers(from, callers, depth + 1);
				}
				continue;
			}
			if (f.isThunk() && depth < 3) {
				collectCallers(f.getEntryPoint(), callers, depth + 1);
			}
			else {
				callers.add(f);
			}
		}
	}

	private void decompile(Function f, String why) {
		if (!decompiled.add(f.getEntryPoint())) {
			out.println("  (already decompiled " + f.getName() + ")");
			return;
		}
		out.println("\n  ==== " + f.getName() + "@" + f.getEntryPoint() + "  [" + why + "]  size=" + f.getBody().getNumAddresses());
		if (f.getBody().getNumAddresses() > 6000) {
			out.println("  (too large, skipped)");
			return;
		}
		DecompileResults res = decomp.decompileFunction(f, 60, monitor);
		if (res.decompileCompleted()) {
			out.println(res.getDecompiledFunction().getC());
		}
		else {
			out.println("  decompile failed: " + res.getErrorMessage());
		}
	}
}
