// Untimed drain at the warm-up/timed boundary (the Java analogue of node's WTB_DRAIN_MS).
// Loaded in BOTH arms:  --plugin <ptjdrain.jar>!PtjDrain --with-arg <first timed op index> --with-arg <bound ms>
// Before operation <at> (after its set-up, outside the measured interval) it calls the native drain(), which
// the Fast arm's JVMTI agent exports (Java_PtjDrain_drain); in the vanilla arm there is no agent, the lookup
// fails, and it returns at once.  Prints one line: {"ptjdrain":{...}} on stdout.
import org.renaissance.Plugin;

public final class PtjDrain implements Plugin, Plugin.AfterOperationSetUpListener {
  private static native String drain(long ms);
  private final int at;
  private final long ms;

  public PtjDrain() { this(new String[0]); }

  public PtjDrain(String[] args) {
    at = args.length > 0 ? Integer.parseInt(args[0]) : -1;
    ms = args.length > 1 ? Long.parseLong(args[1]) : 120000L;
  }

  @Override
  public void afterOperationSetUp(String benchmark, int opIndex, boolean isLastOp) {
    if (opIndex != at || at < 0) return;
    long t0 = System.nanoTime();
    String r;
    try {
      r = drain(ms);
    } catch (UnsatisfiedLinkError e) {
      r = "{\"agent\":0}";
    }
    System.out.println("{\"ptjdrain\":{\"bench\":\"" + benchmark + "\",\"op\":" + opIndex + ",\"java_ms\":"
        + String.format("%.1f", (System.nanoTime() - t0) / 1e6) + ",\"r\":" + r + "}}");
    System.out.flush();
  }
}
