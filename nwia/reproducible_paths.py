"""Keep local checkout/package paths out of compile-time __FILE__ strings."""
from pathlib import Path

Import("env")

project = env.subst("$PROJECT_DIR")
framework = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
flags = [f"-ffile-prefix-map={project}=."]
if framework:
    flags.append(f"-ffile-prefix-map={Path(framework).parent}=platformio/packages")
env.Append(CCFLAGS=flags)
