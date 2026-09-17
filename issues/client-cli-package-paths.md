# Client kernel package paths depend on working directory

Status: open; existing bootstrap policy, outside the client main refactor.

Observed during final CLI validation: running the built client from build/tests
with its copied stdlib/nwn1 and the executable's tools/client/stdlib/nwn1 causes
JSON import to exit 1 before importing. Runtime reports multiple directories
providing the selected package nwn1. client_runtime::register_smalls_packages
adds the executable package path, while kernel service creation adds the cwd
stdlib package path. Both registrations existed before final root extraction.
The executable-directory CLI check and invalid arguments are separate gates;
this failure is not counted as a successful import.

Reproduce with the real DockerDemo.mod fixture, a new /tmp project path,
SDL_VIDEODRIVER=client-cli-invalid-driver, NWN_ROOT set to the dedicated-server
nwn directory and NWN_HOME set to build/tests/test_data/user; run
../tools/client/rollnw-client import --json test_data/user/modules/DockerDemo.mod
/tmp/client-cli-path-repro from build/tests. No window is involved.

Before changing policy, decide whether current-directory packages or installed
executable packages are authoritative for this client. Add a failing production
subprocess regression for the chosen policy, then repair only that bootstrap
registration path. Preserve the runtime's duplicate-package rejection for other
callers. Cost: startup filesystem/package resolution and a focused regression;
no new package registry, per-event work or performance claim is warranted.

Repository-root JSON import also aborts on module reload: startup adds executable
packages, load_module shuts down/recreates services, and kernel creation registers
only cwd/stdlib paths, which do not exist there. Exception: selected package nwn1
not found. Both cwd/executable registrations and module-reload policy match source
baseline 6182a1943. Actual JSON/legacy imports and expected 0/1/2 exits passed from
the executable directory, where package paths coincide; arbitrary-cwd support is
not claimed. The smallest fix must make client package roots survive reload without
changing process cwd or adding a global handler registry; characterize both missing
roots and duplicated roots before selecting that policy.
