#pragma once

// What `GNLinkInputService.exe --uninstall-service` tells the uninstaller about pre-login access
// (prelogin-ui r10, T1). The uninstaller says each in its own words; none of them is "removed" as if
// nothing were left.

namespace remote60::native_poc::prelogin {

// Not withdrawn here, but the "must be off" mark is down: access stays off, and any later service start
// (a reinstall) finishes the withdrawal before it serves anything.
constexpr int kUninstallRevokeOwed = 2001;
// Not withdrawn here AND the mark could not be written: whether access is off is NOT known.
constexpr int kUninstallRevokeUnconfirmed = 2002;
// Access withdrawn, but the service itself could not be deleted.
constexpr int kUninstallServiceNotRemoved = 2003;

}  // namespace remote60::native_poc::prelogin
