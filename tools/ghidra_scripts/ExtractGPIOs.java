// Find GPIO / UART pin arguments passed to ESP-IDF functions.
// @category openextraflame

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Instruction;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.DataIterator;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.ReferenceIterator;
import ghidra.program.model.listing.Function;

public class ExtractGPIOs extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] needles = {
            "gpio_set_level", "gpio_config", "gpio_set_direction", "gpio_get_level",
            "uart_set_pin", "uart_param_config",
            "LED_POWER", "LED_BLE", "LED_WIFI", "LED_SERVER",
            "resetButton", "ResetButton",
            "SERIAL_UPGRADE_TX", "SERIAL_UPGRADE_RX",
            "SERIAL_UPGRADE_PORT"
        };
        for (String s : needles) {
            analyze(s);
        }
    }

    private Address findString(String needle) {
        Address found = currentProgram.getMemory().findBytes(
            null,
            (needle + "\0").getBytes(),
            null,
            true,
            monitor);
        return found;
    }

    private void analyze(String needle) {
        Address sAddr = findString(needle);
        println("\n==== " + needle + " ====");
        if (sAddr == null) {
            println("  string not found");
            return;
        }
        println("  string @ " + sAddr);
        Listing listing = currentProgram.getListing();
        ReferenceIterator refs = currentProgram.getReferenceManager().getReferencesTo(sAddr);
        int count = 0;
        while (refs.hasNext()) {
            Reference ref = refs.next();
            Address from = ref.getFromAddress();
            Function f = currentProgram.getFunctionManager().getFunctionContaining(from);
            String fn = f != null ? f.getName() : "?";
            println("  ref from " + from + "  (fn: " + fn + ")");
            Instruction ins = listing.getInstructionBefore(from);
            java.util.List<String> lines = new java.util.ArrayList<>();
            for (int i = 0; i < 12 && ins != null; i++) {
                lines.add(0, "    " + ins.getAddress() + "  " + ins.toString());
                ins = listing.getInstructionBefore(ins.getAddress());
            }
            for (String l : lines) println(l);
            if (++count >= 3) break;
        }
    }
}
