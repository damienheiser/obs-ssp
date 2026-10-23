# macOS fresh-checkout configuration fix

The first local configure left PLUGIN_BUILD_NUMBER unset, dropping a property value in the macOS helper. Default it to 1 while preserving cached increments, CI run IDs, and explicit overrides. Add an actionable missing-mDNS-submodule diagnostic and document initialization alongside the macOS preset commands. No protocol or runtime behavior changes.

Validation: reproduced the fresh-checkout failure before implementation; all seven checks in `python3 cmake/tests/test_buildnumber.py` pass. `cmake --preset macos` and `cmake --build --preset macos` pass on macOS with AppleClang 21 / macOS 27 SDK, producing arm64 and x86_64 binaries. Existing FFmpeg pkg-config and CMake policy warnings remain. OBS camera playback, other platforms, and native SSP mode were not tested.

Delivery: branch `fix/macos-local-configure` for `damienheiser/obs-ssp`. Author and committer requested by operator: Damien Heiser <14894171+damienheiser@users.noreply.github.com>. Operator will file the upstream PR. Next: open that PR from the feature branch; manually validate camera playback in OBS. No build blocker remains.

Asana: https://app.asana.com/1/1198722117693833/project/1198722117693841/task/1219836346553340

Recovery: revert the focused fix commit. Original operator checkout and caches are protected; agent validation checkout and build log are retained for inspection. No temporary worktrees were created.
