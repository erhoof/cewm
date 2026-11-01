"""Run the actual Java call policies on the laptop, using SDK constants only.

Usage: JAVA_HOME=... python3 phone_policy_test.py /path/to/platforms/android-34/android.jar
"""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

source = (Path(__file__).resolve().parents[1] / "android/app/src/main/java/ru/erhoof/cewm/PhoneService.java").read_text()
methods = []
for name in ("useProximity", "showNativeIncoming"):
    methods.append(re.search(r"    static boolean " + name + r"\([^}]+?\n    }", source).group())
test = """
import android.telecom.Call;
import android.telecom.CallAudioState;
public class PhonePolicyTest {
%s
    public static void main(String[] args) {
        for (int state = 0; state <= 13; state++) {
            boolean talking = state == Call.STATE_CONNECTING || state == Call.STATE_DIALING || state == Call.STATE_ACTIVE;
            assert useProximity(state, CallAudioState.ROUTE_EARPIECE) == talking;
            for (int route : new int[]{0, CallAudioState.ROUTE_SPEAKER, CallAudioState.ROUTE_BLUETOOTH, CallAudioState.ROUTE_WIRED_HEADSET})
                assert !useProximity(state, route);
            for (boolean visible : new boolean[]{false, true})
                for (boolean running : new boolean[]{false, true})
                    assert showNativeIncoming(visible, running, state) == (state == Call.STATE_RINGING && (!visible || !running));
        }
    }
}
""" % "\n".join(methods)
jdk = Path(os.environ["JAVA_HOME"]) / "bin"
with tempfile.TemporaryDirectory(prefix="cewm-phone-test-") as work:
    path = Path(work) / "PhonePolicyTest.java"
    path.write_text(test)
    subprocess.run([str(jdk / "javac"), "-cp", sys.argv[1], str(path)], check=True)
    subprocess.run([str(jdk / "java"), "-ea", "-cp", work, "PhonePolicyTest"], check=True)
print("PASS: proximity states/routes and incoming UI visibility/running states")
