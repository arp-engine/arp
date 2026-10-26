"""Exercise selection focus through the actual Interpreter CLI and native Editor."""
import json
import os
from pathlib import Path
import secrets
import socket
import subprocess
import sys
import tempfile
import time
import unittest

EDITOR = Path(sys.argv.pop(1)).resolve()
CLI = Path(sys.argv.pop(1)).resolve()


class SelectionFocusCliTests(unittest.TestCase):
    def test_native_interpreter_focus_and_normal_shutdown(self):
        with tempfile.TemporaryDirectory(prefix="arp-focus-cli-") as project:
            with socket.socket() as listener:
                listener.bind(("127.0.0.1", 0))
                port = listener.getsockname()[1]
            environment = os.environ.copy()
            environment["ARP_EDITOR_REMOTE_TOKEN"] = secrets.token_hex(24)
            url = "http://127.0.0.1:" + str(port)
            startup = None
            editor_startup = None
            if os.name == "nt":
                startup = subprocess.STARTUPINFO()
                startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
                startup.wShowWindow = 0
                # Native preview host discovery requires a visible HWND. Show
                # this short-lived test window without activating the desktop.
                editor_startup = subprocess.STARTUPINFO()
                editor_startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
                editor_startup.wShowWindow = 4  # SW_SHOWNOACTIVATE

            def invoke(*arguments):
                return subprocess.run(
                    [str(CLI), "--url", url, *arguments], env=environment,
                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                    encoding="utf-8", timeout=15, startupinfo=startup)

            def command(*arguments):
                result = invoke(*arguments)
                self.assertEqual(result.returncode, 0, result.stdout)
                response = json.loads(result.stdout)
                self.assertNotEqual(response.get("status"), "failed", result.stdout)
                return response

            with tempfile.TemporaryFile() as log:
                child = subprocess.Popen(
                    [str(EDITOR), "--project-root", project, "--remote-port", str(port)],
                    env=environment, stdout=log, stderr=subprocess.STDOUT, startupinfo=editor_startup)
                try:
                    deadline = time.monotonic() + 45
                    while True:
                        self.assertIsNone(child.poll(), "Editor exited before readiness")
                        readiness = invoke("state")
                        if readiness.returncode == 0:
                            json.loads(readiness.stdout)
                            break
                        self.assertLess(time.monotonic(), deadline, readiness.stdout)
                        time.sleep(0.1)
                    self.assertIn("scene.selection.focus", invoke("--help").stdout)
                    command("activate", "workspace_3D")
                    hierarchy_snapshot = command("scene.hierarchy.get")["sceneHierarchy"]
                    hierarchy = hierarchy_snapshot["entries"]
                    camera = next(entry for entry in hierarchy if entry["name"] == "Camera3D")
                    command("scene.select", str(camera["objectId"]))
                    inspector = command("scene.inspector.get")["sceneInspector"]
                    position = next(prop["value"] for prop in inspector["properties"]
                                    if prop["componentType"] == "TransformComponent"
                                    and prop["path"] == "position")
                    before = command("state")["snapshot"]
                    self.assertNotEqual(before["sceneCamera"]["target"], position)
                    command("scene.selection.focus")
                    focused_state = command("state")
                    after = focused_state["snapshot"]
                    self.assertEqual(after["sceneCamera"]["target"], position)
                    for field in ("yawDegrees", "pitchDegrees", "distance"):
                        self.assertEqual(before["sceneCamera"][field], after["sceneCamera"][field])
                    for field in ("sceneDirty", "scenePath", "sceneRuntimeId"):
                        self.assertEqual(before[field], after[field])
                    self.assertEqual(command("scene.inspector.get")["sceneInspector"], inspector)
                    self.assertEqual(command("scene.hierarchy.get")["sceneHierarchy"],
                                     {**hierarchy_snapshot, "selectedObjectId": camera["objectId"]})
                    command("scene.selection.focus")
                    self.assertEqual(command("state")["stateVersion"], focused_state["stateVersion"])
                    view_baseline = command("state")["snapshot"]
                    object_id = str(camera["objectId"])
                    document_epoch = str(hierarchy_snapshot["documentEpoch"])
                    self.assertIn("scene.object.view.set", invoke("--help").stdout)
                    for hidden, locked in (("true", "false"), ("true", "true"),
                                           ("false", "true"), ("false", "false")):
                        updated = command("scene.object.view.set", object_id, document_epoch, hidden, locked)
                        current = command("scene.hierarchy.get")["sceneHierarchy"]
                        target = next(entry for entry in current["entries"] if entry["objectId"] == camera["objectId"])
                        self.assertEqual(target["hidden"], hidden == "true")
                        self.assertEqual(target["locked"], locked == "true")
                        self.assertEqual(target["effectiveHidden"], hidden == "true")
                        self.assertEqual(target["effectiveLocked"], locked == "true")
                        self.assertEqual(current["selectedObjectId"], camera["objectId"])
                        self.assertEqual(command("scene.object.view.set", object_id, document_epoch, hidden, locked)
                                         ["stateVersion"], updated["stateVersion"])
                        for field in ("sceneDirty", "scenePath", "sceneRuntimeId", "sceneCamera"):
                            self.assertEqual(command("state")["snapshot"][field], view_baseline[field])
                        if locked == "true":
                            denied = invoke("scene.active.set", object_id, "false")
                            self.assertEqual(denied.returncode, 5, denied.stdout)
                            self.assertIn("scene_object_locked", denied.stdout)
                    for args in ((object_id,), (object_id, document_epoch, "true"),
                                 (object_id, document_epoch, "True", "false"),
                                 (object_id, document_epoch, "true", "0"),
                                 ("1,\"hidden\":false", document_epoch, "true", "false"),
                                 (object_id, "1e0", "true", "false"),
                                 (object_id, "0", "true", "false")):
                        rejected_view = invoke("scene.object.view.set", *args)
                        self.assertEqual(rejected_view.returncode, 2, rejected_view.stdout)
                    self.assertEqual(command("scene.hierarchy.get")["sceneHierarchy"],
                                     {**hierarchy_snapshot, "selectedObjectId": camera["objectId"]})
                    print("PASS native Interpreter CLI exact view epoch/flags, no-op, lock barrier, strict syntax and unchanged Scene/selection/camera")
                    rejected = invoke("scene.selection.focus", "unexpected")
                    self.assertEqual(rejected.returncode, 2, rejected.stdout)
                    self.assertIn("takes no arguments", rejected.stdout)
                    # B5's pivot CLI is another view preference over this same
                    # live Scene: no authoring, selection or history changes.
                    stable_scene = command("state")["snapshot"]
                    stable_inspector = command("scene.inspector.get")["sceneInspector"]
                    stable_hierarchy = command("scene.hierarchy.get")["sceneHierarchy"]
                    previous_version = command("state")["stateVersion"]
                    for pivot in ("center", "individual", "active"):
                        changed = command("scene.transform.pivot.set", pivot)
                        self.assertEqual(changed["stateVersion"], previous_version + 1)
                        repeated = command("scene.transform.pivot.set", pivot)
                        self.assertEqual(changed["stateVersion"], repeated["stateVersion"])
                        previous_version = changed["stateVersion"]
                        state = command("state")["snapshot"]
                        for field in ("sceneDirty", "scenePath", "sceneRuntimeId", "sceneCamera"):
                            self.assertEqual(state[field], stable_scene[field])
                        self.assertEqual(command("scene.inspector.get")["sceneInspector"], stable_inspector)
                        self.assertEqual(command("scene.hierarchy.get")["sceneHierarchy"], stable_hierarchy)
                    command("scene.undo")
                    self.assertEqual(command("state")["stateVersion"], previous_version)
                    self.assertEqual(command("scene.inspector.get")["sceneInspector"], stable_inspector)
                    print("PASS Interpreter CLI pivot Active/Center/Individual, no-op version, unchanged Scene/Inspector/hierarchy/history")
                    # Full settings use the same aggregate gate. Preserve the
                    # original decimal tokens across the Interpreter boundary.
                    settings_cases = [
                        ["rotate", "local", "individual", "true", "0.10000000000000002", "0.25", "5e-324"],
                        ["scale", "world", "center", "false", "0.1", "1E-1", "0.125"],
                        ["move", "local", "active", "true", "0.5", "0.125", "0.25"],
                        ["select", "world", "active", "false", "1", "0.25", "0.1"],
                    ]
                    for args in settings_cases:
                        changed = command("scene.transform.settings.set", *args)
                        expected = dict(zip(
                            ("tool", "space", "pivot", "snapEnabled", "moveStep", "rotateStepRadians", "scaleStep"),
                            (*args[:3], args[3] == "true", *map(float, args[4:]))))
                        self.assertEqual(command("scene.transform.settings.get")["sceneTransformSettings"], expected)
                        state = command("state")
                        self.assertEqual(state["snapshot"]["sceneTransformSettings"], expected)
                        repeated = command("scene.transform.settings.set", *args)
                        self.assertEqual(repeated["stateVersion"], changed["stateVersion"])
                        for field in ("sceneDirty", "scenePath", "sceneRuntimeId", "sceneCamera"):
                            self.assertEqual(state["snapshot"][field], stable_scene[field])
                        self.assertEqual(command("scene.inspector.get")["sceneInspector"], stable_inspector)
                        self.assertEqual(command("scene.hierarchy.get")["sceneHierarchy"], stable_hierarchy)
                    before_rejection = command("state")
                    for value in ("1e309", "1e-9999"):
                        rejected = invoke("scene.transform.settings.set", *settings_cases[-1][:4], value, "0.25", "0.1")
                        self.assertEqual(rejected.returncode, 5, rejected.stdout)
                        self.assertIn("REST request rejected", rejected.stdout)
                        after_rejection = command("state")
                        self.assertEqual(after_rejection["snapshot"], before_rejection["snapshot"])
                        self.assertEqual(after_rejection["stateVersion"], before_rejection["stateVersion"])
                    command("activate", "scene_transform_settings")
                    modal_before = command("state")
                    busy = invoke("scene.transform.settings.set", *settings_cases[-1])
                    self.assertEqual(busy.returncode, 5, busy.stdout)
                    busy_response = json.loads(busy.stdout.splitlines()[1])
                    self.assertEqual(busy_response["status"], "failed")
                    self.assertEqual(busy_response["diagnostic"], "workspace_busy")
                    modal_after = command("state")
                    self.assertEqual(modal_after["snapshot"], modal_before["snapshot"])
                    self.assertEqual(modal_after["stateVersion"], modal_before["stateVersion"])
                    self.assertEqual(command("scene.transform.settings.get")["sceneTransformSettings"],
                                     before_rejection["snapshot"]["sceneTransformSettings"])
                    batch_busy = invoke("scene.selection.transform", "move", "world", "active", "0", "0")
                    self.assertEqual(batch_busy.returncode, 5, batch_busy.stdout)
                    self.assertEqual(json.loads(batch_busy.stdout.splitlines()[1])["diagnostic"], "workspace_busy")
                    command("activate", "transform_ui_cancel")
                    previous_version = command("state")["stateVersion"]
                    command("scene.undo")
                    self.assertEqual(command("state")["stateVersion"], previous_version)
                    print("PASS full Interpreter CLI settings: seven fields, exact adjacent double/subnormal, no-op, "
                          "range rejection, modal Busy/get, unchanged Scene/selection/camera/history")
                    # The selection operation consumes an explicit amount even
                    # when a different Snap increment is enabled in preferences.
                    command("scene.transform.settings.set", "select", "world", "active", "true", "100", "1", "1")
                    batch_baseline = command("scene.inspector.get")["sceneInspector"]
                    batch_state = command("state")["snapshot"]
                    baseline_position = next(prop["value"] for prop in batch_baseline["properties"]
                                             if prop["componentType"] == "TransformComponent"
                                             and prop["path"] == "position")
                    moved = command("scene.selection.transform", "move", "world", "center", "0", "-0.125")
                    moved_inspector = command("scene.inspector.get")["sceneInspector"]
                    moved_position = next(prop["value"] for prop in moved_inspector["properties"]
                                          if prop["componentType"] == "TransformComponent"
                                          and prop["path"] == "position")
                    self.assertEqual(moved_position, {**baseline_position, "x": baseline_position["x"] - 0.125})
                    zero = command("scene.selection.transform", "move", "world", "active", "1", "-0")
                    self.assertEqual(zero["stateVersion"], moved["stateVersion"])
                    command("scene.undo")
                    self.assertEqual(command("scene.inspector.get")["sceneInspector"], batch_baseline)
                    zero_before = command("state")["stateVersion"]
                    command("scene.selection.transform", "scale", "local", "individual", "2", "0e0")
                    self.assertEqual(command("state")["stateVersion"], zero_before)
                    command("scene.redo")
                    self.assertEqual(command("scene.inspector.get")["sceneInspector"], moved_inspector)
                    command("scene.undo")
                    for tool, pivot, axis, amount in (("rotate", "individual", "1", "-0.125"),
                                                      ("scale", "center", "2", "0.25")):
                        command("scene.selection.transform", tool, "local", pivot, axis, amount)
                        transformed = command("scene.inspector.get")["sceneInspector"]
                        self.assertNotEqual(transformed, batch_baseline)
                        command("scene.undo")
                        self.assertEqual(command("scene.inspector.get")["sceneInspector"], batch_baseline)
                        command("scene.redo")
                        self.assertEqual(command("scene.inspector.get")["sceneInspector"], transformed)
                        command("scene.undo")
                    reject_before = command("state")
                    overflow = invoke("scene.selection.transform", "move", "world", "active", "0", "1e309")
                    self.assertEqual(overflow.returncode, 5, overflow.stdout)
                    self.assertEqual(command("state")["stateVersion"], reject_before["stateVersion"])
                    self.assertEqual(command("scene.inspector.get")["sceneInspector"], batch_baseline)
                    after_batch = command("state")["snapshot"]
                    for field in ("sceneRuntimeId", "scenePath", "sceneCamera", "sceneTransformSettings", "selection"):
                        self.assertEqual(after_batch[field], batch_state[field])
                    print("PASS Interpreter CLI explicit signed selection transform, radians/scale delta, no implicit snap, "
                          "zero preserves version/Redo, Undo/Redo, modal Busy and range failure isolation")
                    command("editor.exit", "discard")
                    self.assertEqual(child.wait(timeout=20), 0)
                    print("PASS native Interpreter CLI focus, exact Inspector/hierarchy isolation, "
                          "camera pose, no-op version, strict arity and normal process exit")
                finally:
                    if child.poll() is None:
                        child.terminate()
                        child.wait(timeout=10)
                    if sys.exc_info()[0] is not None:
                        log.seek(0)
                        print(log.read().decode("utf-8", errors="replace"))


if __name__ == "__main__":
    unittest.main()
