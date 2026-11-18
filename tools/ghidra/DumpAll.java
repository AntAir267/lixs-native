// Ghidra headless post-script: decompile every function of the program into
// <out>/<addr>.c plus <out>/index.tsv. Arg 0 = output folder.
//@category 1nsane
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.listing.*;
import java.io.*;
import java.nio.file.*;

public class DumpAll extends GhidraScript {
    @Override
    public void run() throws Exception {
        Path out = Paths.get(getScriptArgs()[0]);
        Files.createDirectories(out);
        DecompInterface di = new DecompInterface();
        di.setOptions(new DecompileOptions());
        di.openProgram(currentProgram);
        PrintWriter idx = new PrintWriter(Files.newBufferedWriter(out.resolve("index.tsv")));
        for (Function f : currentProgram.getFunctionManager().getFunctions(true)) {
            if (monitor.isCancelled()) break;
            String file = String.format("%08x.c", f.getEntryPoint().getOffset());
            DecompileResults r = di.decompileFunction(f, 60, monitor);
            String code = (r != null && r.decompileCompleted()) ? r.getDecompiledFunction().getC() : "// decompile failed\n";
            Files.write(out.resolve(file), code.getBytes());
            idx.printf("%08x\t%d\t%s%n", f.getEntryPoint().getOffset(), f.getBody().getNumAddresses(), f.getName());
        }
        idx.close();
    }
}
