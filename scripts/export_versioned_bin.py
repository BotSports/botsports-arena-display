Import("env")
import os
import re
import shutil

def copy_versioned_bin(source, target, env):
    project_dir = env.subst("$PROJECT_DIR")
    config_path = os.path.join(project_dir, "config.h")
    version = "unknown"
    if os.path.exists(config_path):
        with open(config_path, "r", encoding="utf-8") as f:
            for line in f:
                m = re.match(r'^#define\s+FIRMWARE_VERSION\s+"([^"]+)"', line.strip())
                if m:
                    version = m.group(1)
                    break

    firmware_bin = str(target[0])
    dist_dir = os.path.join(project_dir, "dist")
    os.makedirs(dist_dir, exist_ok=True)

    dest_bin = os.path.join(dist_dir, f"botsports-arena-display-v{version}.bin")
    shutil.copyfile(firmware_bin, dest_bin)
    print(f"[Versioned Build] Exported OTA binary: {dest_bin}")

env.AddPostAction("$BUILD_DIR/${PROGNAME}.bin", copy_versioned_bin)
