// Export decompiled C for all functions carrying a given tag (or all RULSYS) to a file.
// Args: <out-file> [tag ...]   e.g.  C:/tmp/tp_op.c src_tp_op src_opbatch
// @category Infovox
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import java.io.*;
import java.util.*;

public class IvxExport extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] a = getScriptArgs();
        String out = a[0];
        Set<String> tags = new HashSet<>(Arrays.asList(a).subList(1, a.length));
        if (tags.isEmpty()) tags.add("RULSYS");
        DecompInterface d = new DecompInterface();
        d.openProgram(currentProgram);
        int n = 0;
        try (PrintWriter w = new PrintWriter(new FileWriter(out))) {
            w.println("/* decompiled from Ivx230nt.dll, tags " + tags + " */");
            w.println("#include \"ivx_types.h\"");
            for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
                boolean hit = false;
                for (FunctionTag t : f.getTags()) if (tags.contains(t.getName())) hit = true;
                if (!hit) continue;
                DecompileResults r = d.decompileFunction(f, 60, monitor);
                w.println();
                w.println("/* ==== " + f.getEntryPoint() + " " + f.getName() + " ==== */");
                if (r.decompileCompleted()) w.print(r.getDecompiledFunction().getC());
                else w.println("/* decompile failed: " + r.getErrorMessage() + " */");
                n++;
            }
        }
        d.dispose();
        println("exported " + n + " functions to " + out);
    }
}
