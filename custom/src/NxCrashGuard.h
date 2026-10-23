#pragma once

/// Tells after a restart whether the last run of NX Rover ended normally, and on Windows saves a crash dump.
///
/// A run is marked in the settings when it starts and unmarked on a normal exit; a mark left over means the
/// last run ended some other way (a crash, an ended task, the computer turned off). On Windows a crash also
/// writes NX-Rover-crash-<time>.dmp into the CrashLogs folder, so a dump newer than the leftover mark means a
/// crash. The next start says so in the log and in a message.
namespace NxCrashGuard {
/// Once the application is initialised: installs the crash handlers, checks the last run, marks this one.
void start();

/// After a normal exit, before the log is closed.
void stop();
}  // namespace NxCrashGuard
