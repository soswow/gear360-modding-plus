// Decompile every function in the current program into one text file.
// Usage (headless): -postScript DumpAllDecomp.java <output-path> [timeout-seconds]
// Each function is preceded by a "//== FUNCTION <name> @ <entry> size=<bytes>" header
// so the dump can be searched and sliced with ordinary text tools.
import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionIterator;

import java.io.BufferedWriter;
import java.io.FileWriter;

public class DumpAllDecomp extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args.length < 1) {
            printerr("output path required");
            return;
        }
        int timeoutSeconds = args.length > 1 ? Integer.parseInt(args[1]) : 60;
        // Optional sharding so several headless processes can split one program.
        int shardIndex = args.length > 3 ? Integer.parseInt(args[2]) : 0;
        int shardCount = args.length > 3 ? Integer.parseInt(args[3]) : 1;
        int ordinal = -1;
        DecompInterface decompiler = new DecompInterface();
        DecompileOptions options = new DecompileOptions();
        decompiler.setOptions(options);
        decompiler.toggleCCode(true);
        decompiler.toggleSyntaxTree(false);
        if (!decompiler.openProgram(currentProgram)) {
            printerr("decompiler failed to open program");
            return;
        }
        int count = 0;
        int failed = 0;
        try (BufferedWriter writer = new BufferedWriter(new FileWriter(args[0]))) {
            FunctionIterator functions = currentProgram.getFunctionManager().getFunctions(true);
            while (functions.hasNext() && !monitor.isCancelled()) {
                Function function = functions.next();
                ordinal++;
                if (ordinal % shardCount != shardIndex) {
                    continue;
                }
                long size = function.getBody().getNumAddresses();
                writer.write("//== FUNCTION " + function.getName() + " @ " + function.getEntryPoint()
                        + " size=" + size + "\n");
                DecompileResults results = decompiler.decompileFunction(function, timeoutSeconds, monitor);
                if (results != null && results.decompileCompleted() && results.getDecompiledFunction() != null) {
                    writer.write(results.getDecompiledFunction().getC());
                } else {
                    failed++;
                    String message = results == null ? "null result" : results.getErrorMessage();
                    writer.write("// DECOMPILE FAILED: " + message + "\n");
                }
                writer.write("\n");
                count++;
                if (count % 500 == 0) {
                    println("decompiled " + count + " functions, failed " + failed);
                    writer.flush();
                }
            }
        } finally {
            decompiler.dispose();
        }
        println("done: " + count + " functions, failed " + failed);
    }
}
