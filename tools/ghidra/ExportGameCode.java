import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;
import ghidra.program.model.symbol.Namespace;

import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;

public class ExportGameCode extends GhidraScript {

	private static final Set<String> ENGINE = new HashSet<>(Arrays.asList(
		"lang", "io", "math", "gr", "hgr", "lua", "audio", "img", "framework", "game", "pf",
		"std", "__gnu_cxx", "__cxxabiv1"));

	private static final String[] LIB_PREFIXES = {
		"lua", "mpg123", "synth_", "frame_", "do_", "init_", "bad_", "feed_", "fi_", "id3", "icy",
		"I_step", "II_step", "ntom_", "dct", "make_", "read_frame", "open_", "head_", "compute_",
		"get_songlen", "set_pointer", "set_synth", "store_id3", "process_extra", "promote_", "reset_",
		"clear_icy", "exit_id3", "next_text", "prepare_decode", "parse_new_id3", "samples_to", "bytes_to",
		"check_decoders", "decclass", "dectype", "defdec", "invalidate_format", "safe_realloc",
		"_init", "_fini", "_start", "__", "FUN_", "thunk_", "entry", "frame_dummy", "call_"
	};

	private static boolean isLibraryName(String n) {
		for (String p : LIB_PREFIXES) if (n.startsWith(p)) return true;
		return false;
	}

	private static String topNamespace(Namespace ns) {
		if (ns == null || ns.isGlobal()) return null;
		while (ns.getParentNamespace() != null && !ns.getParentNamespace().isGlobal()) ns = ns.getParentNamespace();
		return ns.getName();
	}

	private static String fileKey(Namespace ns) {
		if (ns == null || ns.isGlobal()) return "_globales";
		return ns.getName(true).replace("::", "__").replaceAll("[^A-Za-z0-9_]", "_");
	}

	@Override
	public void run() throws Exception {
		String[] args = getScriptArgs();
		File out = new File(args.length > 0 ? args[0] : "decomp");
		List<String> extra = new ArrayList<>();
		if (args.length > 1 && !args[1].isEmpty()) extra.addAll(Arrays.asList(args[1].split(",")));
		if (!out.isDirectory() && !out.mkdirs()) throw new IOException("cannot create " + out);

		DecompInterface dec = new DecompInterface();
		dec.setOptions(new DecompileOptions());
		dec.toggleCCode(true);
		dec.toggleSyntaxTree(false);
		dec.setSimplificationStyle("decompile");
		if (!dec.openProgram(currentProgram)) throw new IOException("the decompiler could not open the program: " + dec.getLastMessage());

		Map<String, StringBuilder> files = new TreeMap<>();
		Map<String, Integer> counts = new TreeMap<>();
		int total = 0, failed = 0;

		FunctionIterator it = currentProgram.getFunctionManager().getFunctions(true);
		while (it.hasNext() && !monitor.isCancelled()) {
			Function f = it.next();
			if (f.isExternal() || f.isThunk()) continue;
			String full = f.getName(true);
			Namespace ns = f.getParentNamespace();
			String top = topNamespace(ns);
			boolean forced = false;
			for (String p : extra) if (full.startsWith(p)) forced = true;
			if (!forced) {
				if (top == null && isLibraryName(f.getName())) continue;
				if (top != null && ENGINE.contains(top)) continue;
			}

			String key = fileKey(ns);
			monitor.setMessage("Decompilando " + full);
			DecompileResults r = dec.decompileFunction(f, 120, monitor);
			StringBuilder sb = files.computeIfAbsent(key, k -> new StringBuilder(
				"// Decompiled from Bounce Evolution (bin/bounce) with Ghidra. Reference for game/; does not compile as is.\n// Class: " + k.replace("__", "::") + "\n\n"));
			sb.append("// ==================================================================\n");
			sb.append("// ").append(full).append("  @ ").append(f.getEntryPoint()).append('\n');
			sb.append("// signature: ").append(f.getPrototypeString(true, false)).append('\n');
			if (r != null && r.decompileCompleted() && r.getDecompiledFunction() != null) {
				sb.append(r.getDecompiledFunction().getC()).append('\n');
			} else {
				sb.append("// decompile ERROR: ").append(r == null ? "no result" : r.getErrorMessage()).append("\n\n");
				++failed;
			}
			counts.merge(key, 1, Integer::sum);
			++total;
		}
		dec.dispose();

		for (Map.Entry<String, StringBuilder> e : files.entrySet()) {
			try (FileWriter w = new FileWriter(new File(out, e.getKey() + ".c"))) { w.write(e.getValue().toString()); }
		}
		StringBuilder idx = new StringBuilder("class\tfunctions\n");
		for (Map.Entry<String, Integer> e : counts.entrySet()) idx.append(e.getKey().replace("__", "::")).append('\t').append(e.getValue()).append('\n');
		idx.append("\nTOTAL\t").append(total).append("\nERRORES\t").append(failed).append('\n');
		try (FileWriter w = new FileWriter(new File(out, "_indice.txt"))) { w.write(idx.toString()); }
		println("Exported " + total + " functions in " + files.size() + " files (" + failed + " with errors) in " + out.getAbsolutePath());
	}
}
