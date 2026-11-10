// Pre-analysis options for the headerless RTOS carrier: the image has no symbols,
// so function starts must be found by the aggressive ARM instruction finder.
import ghidra.app.script.GhidraScript;

public class RtosAnalysisOptions extends GhidraScript {
    @Override
    public void run() throws Exception {
        setAnalysisOption(currentProgram, "ARM Aggressive Instruction Finder", "true");
        setAnalysisOption(currentProgram, "Function Start Search", "true");
        setAnalysisOption(currentProgram, "Decompiler Parameter ID", "false");
        setAnalysisOption(currentProgram, "Non-Returning Functions - Discovered", "true");
        println("RTOS analysis options applied");
    }
}
