# OBS SSP repository cleanup — 2026-10-24

## Current outcome

Own checkout /Users/agent-codex/obs-ssp-macos-fix is on chore/cleanup-generated-dependencies, based pushed7048a64 feat/ssp-multitrack. Operator requested obsoleteworktree/duplicatecleanup in ourrepo only. One active worktree exists, no stale entries in prune dry-run; no worktree/branch removed. Source/code unchanged.

Removed exactlysix own generated .deps items: obs-studio-31.1.1; obs-deps-qt6-2025-07-11-universal;31.1.1.tar.gz;32.2.2.zip;macos-deps-2025-07-11-universal.tar.xz;macos-deps-qt6-2025-07-11-universal.tar.xz. Reclaimed875.9MiB allocatedspace. Compared5033originalOBS31sourcefiles and4399Qt2025files toarchives: identical. OBSextras were generatedbuild_universal/.deps/buildnumber only. Checked activecache/currentSDKand live symlinks before deletion. Exactpaths,size,archiveSHA256,recoveryURLsandvalidation in docs/cleanup-generated-dependencies.json; restorebydownload+hashverify+extract. Generatedoldbuildoutputs reproducible. No unrelatedsource/data swept.

Retained OBS32.2.2/deps2026/Qt2026/currentarchives/build_macos, actual2025FFmpegheaders needed ABIregression, allsourcebranches, logs/privatecaptures/evidence, siblingstagefolders andinstalledpluginbackups. Otherrepos includingactiveCanonproviderworktree andoperatorcheckout untouched. No broadclean/reset/forcedworktreeremoval. No installedplugin/OBS/camerastatechanges.

Validationaftercleanup: cmake--presetmacos configure/generatepassed; test-decoder-incompatible rebuilt;15/15 selectedofflineCTests pass7.58s, syntheticcameraIPCexcluded. Cameraactivitypausefullyrespected. Evidence /Users/agent-codex/obs-ssp-diagnostics/cleanup-{configure,abi-build,tests}.log. Asana1219846611796535.

## Existing delivery protected

OBSpeertransport code7048a64 pushed damienheiser/obs-ssp feat/ssp-multitrack. NormaldiagnosticsOFFplugininstalled/signedpreviously, notchangedbycleanup. Actualservicegenerated2video2audio+latejoin andmixedHEVCMain10I01030000/1001+AVC15000/1001+AAC passed/errors0; standardWebSocketdispatcherstart/status/stop passed. ExternalWSnetwork/frontendStartStream/crosshost/Windows/Linux notclaimedtested. Recovery /Users/hedon/Library/Application Support/obs-studio/plugin-backups/20261023-track-plan-220830 retained. Transporttask1219837003201276 staysopenforremainingintegration.

Separateunfinishedbrokerprovider task1219842039278325/worktree /Users/agent-codex/canon-usb-research/wt-network-camera-provider retained, branchfeat/zcam-network-provider pushed8a4cb92+d273127. NativeownedBGRA/C4KIOSurfacehandoff andCMIOconfiguredownerPIDcallbacktested; Canonagentindependentlyintegratedcallbackonly. StandaloneSSP/RTSPproducer,LUTprocessing,allreportedcameraAPIcontrols,dynamicCMIOmanagementpending. Signingidentity/activationabsent; noOScameraavailabilityclaim. Do noteditCanonoriginalworktree/service/USB. CoordinatewithCanonthread01a16aea-4f40-7112-9e3e-bab9235bec29.

Cameraagentthread01a16a43-8508-77f1-8341-1dc1c4a4e5e7 requestedpauseonmutations/newSSPdialswhilecorrelatinggreenscreen. Readonly09:30-10:53UTCauditfoundnoSSPlifecycleevents; existingOBSRTSPsocketCLOSE_WAIT. Notcausalproof. Loadedhi3559av100_prores verifiedbycameraagent, SDKcodec1006/4profiles; networkProResframesnotqualified, ZRAWunqualified. Capabilitiesendpoint deployed697579e authenticatedTLS11986 schema1, docs initsworktree; consumerintegrationnotimplemented. Preservequalified/source_vs_transport/active distinctions. No newcameraactivityduringcleanup.

PriorRTSPcredentialrotation task1219837730457886 operatorblocked. NeverprintfullOBSlogs/secrets; whitelist+redact. Wikiaccessreadbackblocked/unverified, draftsretained. Mandatorysessionhandoff /Users/hedon/.local/share/codex-agent/handoffs/unknown-session.md mirrorscurrentstate. Cleanupcomplete; resumeonlyauthorizedimplementationandcoordinatecameraaccessafterpauseend.
