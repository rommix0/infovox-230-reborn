// Apply the Infovox 230 reverse-engineering knowledge base to Ivx230nt.dll.
// Idempotent: safe to re-run after editing ivx_types.h (via gen_types.py) or symbols.tsv.
//   1. parse ivx_types.h into category /ivx (replacing older versions)
//   2. apply symbols.tsv: functions (create/rename/prototype/comment), data, labels
//   3. tag every function by address range (CRT, LICENSE, SAPI_COM, ENGINE, RULSYS + source module)
// Arg 0 (optional): directory containing ivx_types.h and symbols.tsv.
// @category Infovox
import ghidra.app.script.GhidraScript;
import ghidra.app.cmd.function.ApplyFunctionSignatureCmd;
import ghidra.app.util.cparser.C.CParser;
import ghidra.app.util.parser.FunctionSignatureParser;
import ghidra.util.data.DataTypeParser;
import ghidra.util.data.DataTypeParser.AllowedDataTypes;
import ghidra.program.model.address.*;
import ghidra.program.model.data.*;
import ghidra.program.model.listing.*;
import ghidra.program.model.symbol.*;
import java.nio.file.*;
import java.util.*;

public class IvxApply extends GhidraScript {
    static final String DEFAULT_DIR = "C:/Users/abart/Desktop/infovox_disasm/tools/ghidra";
    static final CategoryPath CAT = new CategoryPath("/ivx");
    int nErr = 0;

    // [lo, hi] inclusive -> tag.  Module split inside RULSYS follows the SCCS link order (REFERENCE.md §2.1).
    static final Object[][] RANGES = {
        {0x10001000L, 0x100121afL, "SAPI_COM"},
        {0x100121b0L, 0x1001977fL, "ENGINE"},
        {0x10019780L, 0x100250d1L, "RULSYS"},
        {0x10019780L, 0x10019f4fL, "src_opbatch"},
        {0x10019f50L, 0x1001b04fL, "src_ttsctrl"},
        {0x1001b050L, 0x1001c4efL, "src_tpchar"},       // split by content + SCCS order [LIKELY]
        {0x1001c4f0L, 0x1001dc3fL, "src_opsmooth"},
        {0x1001dc40L, 0x1001e88fL, "src_opstart"},
        {0x1001e890L, 0x1001eeefL, "src_tpelem"},
        {0x1001eef0L, 0x1001fe8fL, "src_tpstr"},
        {0x1001fe90L, 0x100207ffL, "src_contwork"},
        {0x10020800L, 0x100208ffL, "src_cwflow"},
        {0x10020900L, 0x100216dfL, "src_cwtest"},
        {0x100216e0L, 0x10021b2fL, "src_cwelem"},
        {0x10021b30L, 0x10021f7fL, "src_cwmath"},
        {0x10021f80L, 0x10022c9fL, "src_cwvars"},
        {0x10022ca0L, 0x10022cafL, "src_cwmisc"},
        {0x10022cb0L, 0x100250d1L, "src_synth"},
        {0x100250f0L, 0x1002cf9fL, "LICENSE"},          // Crypkey + Sentinel client libs (cracked, skip)
        // 0x100250d2-0x100250ef: comdlg32 import thunks (GetFileTitleA, GetSaveFileNameA, GetOpenFileNameA), untagged
        {0x1002cfa0L, 0x1003ffffL, "CRT"},              // MSVC 5 runtime
    };
    // licensing code inside the ENGINE range (strings: Crypkey / RNBOspro / IVX230.LIC)
    static final long[] LICENSE_FUNCS = {0x100145d0L, 0x10014830L, 0x10014980L, 0x10014b50L,
                                         0x100150b0L, 0x10015ba0L};
    static final String[] OBSOLETE_TAGS = {"src_tp_op"};
    static final String[] TYPES = {"SegAnchor", "SegList", "Seg", "ItemBlk", "PhRec", "IvxBody", "Track", "Core", "OpBatch",
        "Resonator", "SynthMapEntry", "FrameCoef0", "FrameCoef1", "SynthState0", "SynthRun", "SynthTables", "Synth"};

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        Path dir = Paths.get(args.length > 0 && !args[0].isEmpty() ? args[0] : DEFAULT_DIR);
        importTypes(dir.resolve("ivx_types.h"));
        applySymbols(dir.resolve("symbols.tsv"));
        applyTags();
        println("IvxApply done, errors: " + nErr);
    }

    void err(String s) { nErr++; println("ERROR " + s); }

    void importTypes(Path h) throws Exception {
        String src = new String(Files.readAllBytes(h), "UTF-8");
        DataTypeManager dtm = currentProgram.getDataTypeManager();
        StandAloneDataTypeManager tmp = new StandAloneDataTypeManager("ivx_tmp");
        int tx = tmp.startTransaction("parse");
        try {
            new CParser(tmp, true, null).parse(src);
            Map<String, DataType> byName = new HashMap<>();
            Iterator<DataType> it = tmp.getAllDataTypes();
            while (it.hasNext()) { DataType d = it.next(); byName.put(d.getName(), d); }
            for (String n : TYPES) {
                DataType d = byName.get(n);
                if (d == null) { err("type not parsed: " + n); continue; }
                d.setCategoryPath(CAT);
            }
            for (String n : TYPES) {
                DataType d = byName.get(n);
                if (d == null) continue;
                DataType r = dtm.resolve(d, DataTypeConflictHandler.REPLACE_HANDLER);
                println(String.format("type %-14s size 0x%x", r.getPathName(), r.getLength()));
            }
        } finally {
            tmp.endTransaction(tx, true);
            tmp.close();
        }
    }

    void applySymbols(Path tsv) throws Exception {
        DataTypeManager dtm = currentProgram.getDataTypeManager();
        FunctionSignatureParser sp = new FunctionSignatureParser(dtm, null);
        DataTypeParser tp = new DataTypeParser(dtm, dtm, null, AllowedDataTypes.ALL);
        int nf = 0, nd = 0, nl = 0;
        for (String line : Files.readAllLines(tsv)) {
            if (line.isEmpty() || line.startsWith("#")) continue;
            String[] c = line.split("\t", -1);
            if (c.length < 5) { err("bad line: " + line); continue; }
            Address a = toAddr(Long.parseLong(c[0], 16));
            String kind = c[1], name = c[2], proto = c[3].trim(), com = c[4].trim();
            try {
                if (kind.equals("nofunc")) {
                    // bogus function (e.g. a switch case split off by analysis): delete it and
                    // let the containing function re-absorb the code
                    Function f = getFunctionAt(a);
                    if (f != null) {
                        // remove tags first: FunctionManagerDB.doRemoveFunction throws
                        // ConcurrentModificationException on tagged functions
                        for (FunctionTag ft : new ArrayList<>(f.getTags())) f.removeTag(ft.getName());
                        removeFunction(f);
                        Function parent = getFunctionContaining(a.subtract(1));
                        if (parent != null)
                            ghidra.app.cmd.function.CreateFunctionCmd.fixupFunctionBody(currentProgram, parent, monitor);
                    }
                    nl++;
                } else if (kind.equals("func")) {
                    Function f = getFunctionAt(a);
                    if (f == null) {
                        Function in = getFunctionContaining(a);
                        if (in != null && !in.getEntryPoint().equals(a)) {
                            err(name + ": " + a + " is inside " + in.getName()); continue;
                        }
                        disassemble(a);
                        f = createFunction(a, name);
                        if (f == null) { err("could not create function " + name + " at " + a); continue; }
                    }
                    f.setName(name, SourceType.USER_DEFINED);
                    if (!proto.equals("-")) {
                        FunctionDefinitionDataType fd = sp.parse(f.getSignature(), proto);
                        ApplyFunctionSignatureCmd cmd = new ApplyFunctionSignatureCmd(a, fd, SourceType.USER_DEFINED);
                        if (!cmd.applyTo(currentProgram)) err(name + " prototype: " + cmd.getStatusMsg());
                    }
                    if (!com.equals("-") && !com.isEmpty()) f.setComment(com);
                    nf++;
                } else if (kind.equals("data")) {
                    setLabel(a, name);
                    if (!proto.equals("-")) {
                        DataType dt = tp.parse(proto.replace(" ", ""));
                        clearListing(a, a.add(dt.getLength() - 1));
                        createData(a, dt);
                    }
                    if (!com.equals("-") && !com.isEmpty()) setEOLComment(a, com);
                    nd++;
                } else if (kind.equals("label")) {
                    setLabel(a, name);
                    if (!com.equals("-") && !com.isEmpty()) setEOLComment(a, com);
                    nl++;
                } else err("unknown kind " + kind);
            } catch (Exception e) {
                err(name + ": " + e);
            }
        }
        println("symbols: " + nf + " functions, " + nd + " data, " + nl + " labels");
    }

    void setLabel(Address a, String name) throws Exception {
        SymbolTable st = currentProgram.getSymbolTable();
        for (Symbol s : st.getSymbols(a)) if (s.getName().equals(name)) { s.setPrimary(); return; }
        Symbol s = st.createLabel(a, name, SourceType.USER_DEFINED);
        s.setPrimary();
    }

    void applyTags() throws Exception {
        FunctionManager fm = currentProgram.getFunctionManager();
        FunctionTagManager tm = fm.getFunctionTagManager();
        Map<String, Integer> count = new TreeMap<>();
        for (String t : OBSOLETE_TAGS) {
            FunctionTag ft = tm.getFunctionTag(t);
            if (ft != null) ft.delete();
        }
        Set<String> managed = new HashSet<>();
        for (Object[] r : RANGES) managed.add((String) r[2]);
        managed.add("LICENSE");
        Set<Long> lic = new HashSet<>();
        for (long l : LICENSE_FUNCS) lic.add(l);
        for (Function f : fm.getFunctions(true)) {
            long a = f.getEntryPoint().getOffset();
            List<String> tags = new ArrayList<>();
            for (Object[] r : RANGES)
                if (a >= (Long) r[0] && a <= (Long) r[1]) tags.add((String) r[2]);
            if (lic.contains(a)) { tags.remove("ENGINE"); tags.add("LICENSE"); }
            // drop managed tags that no longer apply (after a range change)
            for (FunctionTag ft : new ArrayList<>(f.getTags()))
                if (managed.contains(ft.getName()) && !tags.contains(ft.getName())) f.removeTag(ft.getName());
            for (String t : tags) {
                if (tm.getFunctionTag(t) == null) tm.createFunctionTag(t, "");
                f.addTag(t);
                count.merge(t, 1, Integer::sum);
            }
        }
        println("tags: " + count);
    }
}
