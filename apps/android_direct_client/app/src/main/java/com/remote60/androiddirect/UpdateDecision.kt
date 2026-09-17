package com.remote60.androiddirect

/**
 * What to do about an update on Android, decided apart from doing it.
 *
 * Everything here is a plain function over plain data, so the rules that matter can be exercised
 * on a JVM with no device and no package installer. The part that cannot be -- asking the system
 * to install an APK, and what the user says to that -- lives in UpdateInstaller.kt.
 *
 * Two things drive the shape of this file.
 *
 * `versionCode` is the authority, not `versionName`. Android compares versionCode when it decides
 * whether a package may replace another, so an update whose versionName looks newer but whose
 * versionCode is not greater will be refused by the system after the user has already been asked
 * to approve it. Better to know before asking.
 *
 * Declining is a normal ending. The permission to install packages can be refused, and the system
 * install dialog can be cancelled. Neither is a fault, and neither should produce an error: the
 * user still has a working app, which is exactly why they were free to say no.
 *
 * Design: docs/업데이트_기능_설계.md 3.5, 4.2, docs/업데이트_배선_계획.md W7.
 */
object UpdateDecision {

    /** Whether this manifest describes something worth installing here. */
    enum class Verdict {
        Install,
        /** Already at or beyond it. Not a failure, and not worth telling the user about. */
        UpToDate,
        /**
         * The manifest describes an older build.
         *
         * Reported separately from UpToDate because it means something different: the server is
         * offering a downgrade, which Android would refuse anyway, and which is worth a log line
         * because it usually means a publishing mistake.
         */
        Downgrade,
        /** The manifest is for something else -- another platform, or no APK in it. */
        NotForUs,
    }

    data class Evaluation(
        val verdict: Verdict,
        /** The artifact to fetch, when there is one. */
        val artifact: UpdateManifest.Artifact? = null,
        val detail: String = "",
    ) {
        fun shouldInstall(): Boolean = verdict == Verdict.Install
    }

    /**
     * Picks the APK out of a release, if there is exactly one.
     *
     * Exactly one on purpose. A release with none is not for this platform; a release with
     * several gives no basis for choosing, and picking the first would be inventing a rule the
     * signature does not express.
     */
    fun apkArtifact(fields: UpdateManifest.Fields): UpdateManifest.Artifact? {
        val apks = fields.artifacts.filter { it.name.endsWith(".apk", ignoreCase = true) }
        return if (apks.size == 1) apks[0] else null
    }

    /**
     * Judges a verified manifest against what is installed.
     *
     * `installedVersionCode` is this app's own versionCode. The comparison is on versionCode
     * alone: it is what Android enforces, and a version NAME that reads as newer while the code
     * is not greater produces a package the system refuses after the user has approved it.
     */
    fun evaluate(fields: UpdateManifest.Fields, installedVersionCode: Long): Evaluation {
        if (!fields.platform.equals("android", ignoreCase = true)) {
            return Evaluation(Verdict.NotForUs, null, "platform is ${fields.platform}")
        }
        if (fields.versionCode <= 0L) {
            // Without it there is nothing to compare, and installing on the strength of a version
            // name alone would be guessing at the one thing Android will not guess at.
            return Evaluation(Verdict.NotForUs, null, "the manifest carries no versionCode")
        }
        val apk = apkArtifact(fields)
            ?: return Evaluation(Verdict.NotForUs, null, "the release does not contain exactly one apk")

        return when {
            fields.versionCode > installedVersionCode ->
                Evaluation(Verdict.Install, apk, "versionCode ${fields.versionCode} > $installedVersionCode")
            fields.versionCode == installedVersionCode ->
                Evaluation(Verdict.UpToDate, null, "versionCode ${fields.versionCode} is installed")
            else ->
                Evaluation(Verdict.Downgrade, null,
                    "the server offers versionCode ${fields.versionCode}, older than $installedVersionCode")
        }
    }

    /** Why a downloaded file was rejected, or that it was not. */
    enum class DownloadVerdict { Ok, WrongSize, WrongHash }

    /**
     * Checks a downloaded file against what the manifest promised.
     *
     * Size first, because it is free and a truncated download is the common case; the hash then
     * settles identity. Both come from the same signed document, so neither can be moved without
     * the other.
     */
    fun verifyDownload(
        actualSize: Long,
        actualSha256: String,
        artifact: UpdateManifest.Artifact,
    ): DownloadVerdict = when {
        actualSize != artifact.size -> DownloadVerdict.WrongSize
        // Compared case-insensitively on the ACTUAL value only: the manifest's own hash was
        // already required to be lowercase when it was parsed, so accepting either spelling here
        // does not loosen the document's rules -- it just avoids failing on how a digest was
        // formatted after the fact.
        !actualSha256.equals(artifact.sha256, ignoreCase = true) -> DownloadVerdict.WrongHash
        else -> DownloadVerdict.Ok
    }

    /** How an install attempt ended. */
    enum class InstallOutcome {
        Installed,
        /** The user dismissed the system dialog. An answer, not a fault. */
        Cancelled,
        /** The user did not grant permission to install packages. Also an answer. */
        PermissionDenied,
        /** Something actually went wrong. */
        Failed,
    }

    /**
     * True when the outcome is one the user chose, and so must not be reported as an error.
     *
     * Kept as a function rather than left to each call site, because the two declining outcomes
     * are easy to lump in with Failed and the result of doing so is an app that argues with
     * someone who just said no.
     */
    fun isUserDecision(outcome: InstallOutcome): Boolean =
        outcome == InstallOutcome.Cancelled || outcome == InstallOutcome.PermissionDenied

    /**
     * Maps a PackageInstaller status to an outcome.
     *
     * `STATUS_FAILURE_ABORTED` (3) is what a dismissed dialog reports, and it is the one that must
     * not become an error.
     */
    fun installOutcomeFromStatus(status: Int): InstallOutcome = when (status) {
        0 -> InstallOutcome.Installed        // STATUS_SUCCESS
        3 -> InstallOutcome.Cancelled        // STATUS_FAILURE_ABORTED
        else -> InstallOutcome.Failed
    }

    /**
     * Whether a failed install is the DEVICE refusing a programmatic self-update, rather than the
     * user declining one.
     *
     * Samsung blocks a sideloaded app from installing itself through PackageInstaller, reporting
     * INSTALL_FAILED_ABORTED with "Self update is blocked by unknown source package". Seen on an
     * SM-S948N: the check, the download and the verification all passed and only the commit was
     * refused. Handing the same APK to the system's own install screen works, so that is the
     * fallback -- but only for this case.
     *
     * The distinction is the whole reason this is a function with tests. A user who dismisses the
     * confirmation dialog is ALSO reported as aborted, with no message. Falling back on every abort
     * would re-open an install screen for someone who just said no, which is arguing with a
     * decision they made. So a blank message is never a block: the vendor refusal always carries
     * text, and a dismissal does not.
     */
    fun isSelfUpdateBlocked(status: Int, message: String): Boolean {
        if (status == 0) return false  // STATUS_SUCCESS is not something to fall back from
        val text = message.lowercase()
        if (text.isBlank()) return false
        return text.contains("self update is blocked") ||
            (text.contains("unknown source") && text.contains("block"))
    }
}
