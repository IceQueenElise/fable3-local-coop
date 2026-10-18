// Ad-hoc queries against Fable3.exe.
// Usage (headless): -postScript Query.java <outFile> <cmd>...
//   dec:<addr>     decompile the function containing addr
//   refs:<addr>    list references to addr (with containing function)
//   callers:<addr> decompile every function that references addr
//   ext:<filter>   list external symbols whose library or name contains filter
//   lua:<name>     resolve a Lua binding (handler pushed just before the name) and decompile it
//   dis:<addr>[:n[:back]] disassemble n instructions from the instruction containing addr (minus back)
//   scanw:<start>:<end>:<off> list MOV stores to [reg + off] in an address range
//   sym:<addr>     nearest symbol at or before addr (e.g. the vtable owning a slot) and its references
//@category Fable3Coop

import java.io.*;
import java.util.*;

import ghidra.app.decompiler.*;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.mem.*;
import ghidra.program.model.symbol.*;

public class Query extends GhidraScript {

	private DecompInterface decomp;
	private PrintWriter out;
	private final Set<Address> decompiled = new HashSet<>();

	@Override
	public void run() throws Exception {
		String[] args = getScriptArgs();
		out = new PrintWriter(new FileWriter(args[0]));
		decomp = new DecompInterface();
		decomp.openProgram(currentProgram);
		try {
			for (int i = 1; i < args.length; i++) {
				int c = args[i].indexOf(':');
				String cmd = args[i].substring(0, c);
				String arg = args[i].substring(c + 1);
				out.println("\n######## " + args[i]);
				switch (cmd) {
					case "dec" -> decompile(getFunctionContaining(toAddr(arg)), "requested");
					case "refs" -> refs(toAddr(arg), false);
					case "callers" -> refs(toAddr(arg), true);
					case "ext" -> ext(arg);
					case "lua" -> lua(arg);
					case "dis" -> dis(arg);
					case "scanw" -> scanWrites(arg);
					case "fnstore" -> {
						// fnstore:<dataAddr>:<hexOff> — functions that reference dataAddr and store to [reg + off].
						String[] p = arg.split(":");
						String needle = " + 0x" + p[1].toLowerCase() + "],";
						Set<Function> fns = new LinkedHashSet<>();
						for (Reference r : getReferencesTo(toAddr(p[0]))) {
							Function f = getFunctionContaining(r.getFromAddress());
							if (f != null) {
								fns.add(f);
							}
						}
						for (Function f : fns) {
							for (Instruction ins : currentProgram.getListing().getInstructions(f.getBody(), true)) {
								String s = ins.toString();
								if (s.startsWith("MOV ") && s.toLowerCase().contains(needle) && !s.contains("ESP")) {
									out.println("  " + f.getName() + "@" + f.getEntryPoint() + "  " + ins.getAddress() + "  " + s);
								}
							}
						}
					}
					case "copyst" -> {
						// copyst:<hexOff>[:window] — "MOV ECX,0xe" (14-dword event copy) followed within
						// window instructions by a non-stack store to [reg + off].
						String[] p = arg.split(":");
						String needle = " + 0x" + p[0].toLowerCase() + "],";
						int window = p.length > 1 ? Integer.parseInt(p[1]) : 16;
						for (Instruction ins : currentProgram.getListing().getInstructions(true)) {
							if (!ins.toString().equals("MOV ECX,0xe")) {
								continue;
							}
							Instruction j = ins.getNext();
							for (int k = 0; k < window && j != null; k++, j = j.getNext()) {
								String s = j.toString();
								if (s.startsWith("MOV ") && s.toLowerCase().contains(needle) && !s.contains("ESP") && !s.contains("EBP")) {
									Function f = getFunctionContaining(j.getAddress());
									out.println("  " + j.getAddress() + "  " + s + "   in " +
										(f == null ? "<none>" : f.getName() + "@" + f.getEntryPoint()));
								}
							}
						}
					}
					case "vtable" -> {
						// vtable:<addr>:<n> — list n function pointers starting at addr.
						String[] p = arg.split(":");
						Address a = toAddr(p[0]);
						for (int k = 0; k < Integer.parseInt(p[1]); k++, a = a.add(4)) {
							long v = currentProgram.getMemory().getInt(a) & 0xffffffffL;
							Function f = getFunctionAt(toAddr(v));
							out.println(String.format("  [%2d] +0x%02x  %08x  %s", k, k * 4, v,
								f == null ? "" : f.getName() + " size=" + f.getBody().getNumAddresses()));
						}
					}
					case "sym" -> {
						Address a = toAddr(arg);
						Symbol s = currentProgram.getSymbolTable().getPrimarySymbol(a);
						for (Address b = a; s == null && a.subtract(b) < 0x400; b = b.subtract(4)) {
							s = currentProgram.getSymbolTable().getPrimarySymbol(b);
						}
						out.println("  nearest symbol at/before " + a + ": " + (s == null ? "<none>" : s.getName(true) + " @ " + s.getAddress()));
						if (s != null) {
							refs(s.getAddress(), false);
						}
					}
					default -> out.println("unknown command");
				}
			}
		}
		finally {
			decomp.dispose();
			out.close();
		}
	}

	private void refs(Address a, boolean decompileCallers) {
		Set<Function> fns = new LinkedHashSet<>();
		for (Reference r : getReferencesTo(a)) {
			Function f = getFunctionContaining(r.getFromAddress());
			out.println("  " + r.getFromAddress() + " " + r.getReferenceType() + " in " +
				(f == null ? "<none>" : f.getName() + "@" + f.getEntryPoint()));
			if (f != null) {
				fns.add(f);
			}
		}
		if (decompileCallers) {
			for (Function f : fns) {
				decompile(f, "references " + a);
			}
		}
	}

	private void ext(String filter) {
		String fl = filter.toLowerCase();
		ExternalManager em = currentProgram.getExternalManager();
		for (Symbol s : currentProgram.getSymbolTable().getExternalSymbols()) {
			ExternalLocation loc = em.getExternalLocation(s);
			String lib = loc == null ? "?" : loc.getLibraryName();
			if (!(lib.toLowerCase().contains(fl) || s.getName().toLowerCase().contains(fl))) {
				continue;
			}
			out.println("  " + lib + "!" + s.getName() + " ord=" + (loc == null ? "?" : loc.getOriginalImportedName()) +
				" refs=" + s.getReferences().length);
			for (Reference r : s.getReferences()) {
				Address from = r.getFromAddress();
				out.println("     ref " + from);
				// IAT slot: show who reads it.
				for (Reference r2 : getReferencesTo(from)) {
					Function f = getFunctionContaining(r2.getFromAddress());
					out.println("        " + r2.getFromAddress() + " " + r2.getReferenceType() + " in " +
						(f == null ? "<none>" : f.getName() + "@" + f.getEntryPoint()));
				}
			}
		}
	}

	private void lua(String name) throws Exception {
		byte[] needle = (name + "\0").getBytes("US-ASCII");
		Memory mem = currentProgram.getMemory();
		Address s = mem.findBytes(mem.getMinAddress(), needle, null, true, monitor);
		while (s != null) {
			// Make sure it is the start of the string, not a suffix of a longer one.
			if (mem.getByte(s.subtract(1)) == 0) {
				for (Reference r : getReferencesTo(s)) {
					Instruction ins = getInstructionAt(r.getFromAddress());
					// Walk back to the nearest PUSH of a function address.
					for (int i = 0; i < 4 && ins != null; i++) {
						ins = ins.getPrevious();
						if (ins == null || !ins.getMnemonicString().equals("PUSH")) {
							continue;
						}
						for (Reference r2 : ins.getReferencesFrom()) {
							Function h = getFunctionAt(r2.getToAddress());
							if (h != null) {
								out.println("  " + name + " -> " + h.getName() + "@" + h.getEntryPoint() +
									" (registered at " + r.getFromAddress() + ")");
								decompile(h, "lua " + name);
								i = 99;
							}
						}
					}
				}
			}
			s = mem.findBytes(s.add(1), needle, null, true, monitor);
		}
	}

	// scanw:<start>:<end>:<hexOffset>[:any] — instructions in [start,end) that store to [reg + offset];
	// with ":any", every instruction whose first operand is [reg + offset].
	private void scanWrites(String arg) {
		String[] p = arg.split(":");
		AddressSet range = new AddressSet(toAddr(p[0]), toAddr(p[1]));
		String needle = " + 0x" + p[2].toLowerCase() + (p.length > 3 ? "]" : "],");
		for (Instruction ins : currentProgram.getListing().getInstructions(range, true)) {
			String s = ins.toString();
			if ((s.startsWith("MOV ") || p.length > 3) && s.toLowerCase().contains(needle)) {
				Function f = getFunctionContaining(ins.getAddress());
				out.println("  " + ins.getAddress() + "  " + s + "   in " +
					(f == null ? "<none>" : f.getName() + "@" + f.getEntryPoint()));
			}
		}
	}

	// dis:<addr>[:<count>[:<back>]] — disassemble count instructions starting back
	// instructions before the instruction containing addr.
	private void dis(String arg) {
		String[] p = arg.split(":");
		int n = p.length > 1 ? Integer.parseInt(p[1]) : 40;
		int back = p.length > 2 ? Integer.parseInt(p[2]) : 0;
		Instruction ins = getInstructionContaining(toAddr(p[0]));
		if (ins == null) {
			ins = getInstructionBefore(toAddr(p[0]));
		}
		for (int i = 0; i < back && ins != null && ins.getPrevious() != null; i++) {
			ins = ins.getPrevious();
		}
		for (int i = 0; i < n && ins != null; i++, ins = ins.getNext()) {
			StringBuilder sb = new StringBuilder("  " + ins.getAddress() + "  " + ins);
			for (Reference r : ins.getReferencesFrom()) {
				Function f = getFunctionAt(r.getToAddress());
				if (f != null) {
					sb.append("   ; ").append(f.getName());
				}
			}
			out.println(sb);
		}
	}

	private void decompile(Function f, String why) {
		if (f == null) {
			out.println("  no function");
			return;
		}
		if (!decompiled.add(f.getEntryPoint())) {
			out.println("  (already decompiled " + f.getName() + ")");
			return;
		}
		out.println("\n  ==== " + f.getName() + "@" + f.getEntryPoint() + "  [" + why + "]  size=" + f.getBody().getNumAddresses());
		if (f.getBody().getNumAddresses() > 40000) {
			out.println("  (too large, skipped)");
			return;
		}
		DecompileResults res = decomp.decompileFunction(f, 90, monitor);
		out.println(res.decompileCompleted() ? res.getDecompiledFunction().getC() : "  decompile failed: " + res.getErrorMessage());
	}
}
