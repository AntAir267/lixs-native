// Ghidra headless post-script: decompile the function containing each address (hex, comma separated, arg 1)
// into <arg 0>/<entry>.c, with a header listing callers and callees.
//@category li2
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.*;
import ghidra.program.model.address.*;
import ghidra.program.model.listing.*;
import java.nio.file.*;
import java.util.*;

public class DecompContaining extends GhidraScript {
    @Override
    public void run() throws Exception {
        Path out = Paths.get(getScriptArgs()[0]);
        Files.createDirectories(out);
        AddressSpace sp = currentProgram.getAddressFactory().getDefaultAddressSpace();
        DecompInterface di = new DecompInterface();
        di.setOptions(new DecompileOptions());
        di.openProgram(currentProgram);
        for (String s : getScriptArgs()[1].split(",")) {
            Address a = sp.getAddress(Long.parseLong(s, 16));
            Function f = getFunctionContaining(a);
            if (f == null) { disassemble(a); f = createFunction(a, null); }
            if (f == null) { println("no function at " + s); continue; }
            StringBuilder sb = new StringBuilder();
            sb.append("// ").append(f.getName()).append(" @ ").append(f.getEntryPoint()).append(" (asked ").append(s).append(")\n");
            for (Function c : f.getCallingFunctions(monitor)) sb.append("// caller ").append(c.getEntryPoint()).append('\n');
            for (Function c : f.getCalledFunctions(monitor)) sb.append("// callee ").append(c.getEntryPoint()).append('\n');
            DecompileResults r = di.decompileFunction(f, 120, monitor);
            sb.append((r != null && r.decompileCompleted()) ? r.getDecompiledFunction().getC() : "// decompile failed\n");
            Files.write(out.resolve(String.format("%08x.c", f.getEntryPoint().getOffset())), sb.toString().getBytes());
        }
    }
}
