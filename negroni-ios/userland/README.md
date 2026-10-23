# iOS 16 wallpaper provider registration

The live 20H392 root has ten Apple wallpaper-provider bundles, but its mobile
LaunchServices cache contains none of their identifiers. PosterBoard's extension
store is empty, and repeated prewarm requests trap in
`-[PBFPosterExtensionDataStore _stateLock_setupSwitcherConfigurationForFirstTime]`
on `!_stateLock_switcherConfiguration`.

`poster-register` requests registration of only those existing Apple providers
using `LSApplicationWorkspace registerPlugin:` and verifies their identifiers with
`installedPlugins`. Both APIs were confirmed in the target iOS 16 CoreServices
cache. Its logs distinguish accepted requests from verified registrations. It
does not delete databases, reset wallpaper selections, patch assertions or respring.
These changes are a candidate fix; a device run must still show registered
providers, extension launches and a working gallery before claiming success.

Build: the `negroni-userland` GitHub Actions workflow compiles and ad-hoc signs an
ARM64 executable targeting iOS 16. Download artifact `negroni-ios-userland`.

With a **copy of the current iOS root** mounted at `MOUNTED_ROOT`, install as root:

```sh
python3 negroni-ios/tools/install-poster-registration.py MOUNTED_ROOT SIGNED_HELPER
```

The installer adds missing provider metadata to launchd's AppExtensions table
(including Weather after its move from staged apps) and a one-shot mobile job.
The job waits 30 seconds for LaunchServices and registers only missing providers.
Console output starts `NEGRONI POSTER:`. Exit 0 requires every manifest provider
to appear in LaunchServices, but does not itself prove the wallpaper renders.
No phone flashing is performed by the installer.

The API/entitlement reference is
[Procursus uikittools-ng](https://github.com/ProcursusTeam/uikittools-ng/tree/6b84d79261186d070b54efa9a39850f108b52239).
No Procursus implementation is copied into the helper.
