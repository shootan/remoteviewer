package com.remote60.androiddirect

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/**
 * The Android update rules, without a device.
 *
 * Most of these cases are about NOT installing, and about not treating a user's refusal as a
 * fault. Both matter more than the happy path: an app that installs when it should not is worse
 * than one that never updates, and an app that shows an error after someone declines a permission
 * teaches them to distrust its messages.
 */
class UpdateDecisionTest {

    private fun apk(name: String = "GNLink-0.2.13.apk", size: Long = 1024) =
        UpdateManifest.Artifact(name, size, "a".repeat(64), "https://u.example/$name")

    private fun fields(
        versionCode: Long,
        platform: String = "android",
        artifacts: List<UpdateManifest.Artifact> = listOf(apk()),
    ) = UpdateManifest.Fields(
        schema = 2,
        releaseId = "r-0.2.13",
        platform = platform,
        arch = "arm64",
        version = "0.2.13",
        versionCode = versionCode,
        artifacts = artifacts,
    )

    // ---------------------------------------------------------------- versionCode is the authority

    @Test
    fun newerVersionCodeInstalls() {
        val e = UpdateDecision.evaluate(fields(versionCode = 12), installedVersionCode = 11)
        assertEquals(UpdateDecision.Verdict.Install, e.verdict)
        assertTrue(e.shouldInstall())
        assertEquals("GNLink-0.2.13.apk", e.artifact?.name)
    }

    @Test
    fun sameVersionCodeIsUpToDate() {
        val e = UpdateDecision.evaluate(fields(versionCode = 11), installedVersionCode = 11)
        assertEquals(UpdateDecision.Verdict.UpToDate, e.verdict)
        assertFalse(e.shouldInstall())
        assertNull(e.artifact)
    }

    @Test
    fun olderVersionCodeIsADowngradeAndSaysSo() {
        // Reported apart from UpToDate: Android would refuse it anyway, and it usually means the
        // server was published wrong. Worth a distinct line in the log.
        val e = UpdateDecision.evaluate(fields(versionCode = 10), installedVersionCode = 11)
        assertEquals(UpdateDecision.Verdict.Downgrade, e.verdict)
        assertTrue(e.detail.contains("10"))
        assertTrue(e.detail.contains("11"))
    }

    @Test
    fun aMissingVersionCodeIsNotEnoughToInstallOn() {
        // The version NAME may look newer. Android compares versionCode, so installing on the
        // strength of a name would be guessing at the one thing the system will not guess at.
        val e = UpdateDecision.evaluate(fields(versionCode = 0), installedVersionCode = 11)
        assertEquals(UpdateDecision.Verdict.NotForUs, e.verdict)
        assertTrue(e.detail.contains("versionCode"))
    }

    // ---------------------------------------------------------------- which file to install

    @Test
    fun aReleaseForAnotherPlatformIsNotOurs() {
        val e = UpdateDecision.evaluate(
            fields(versionCode = 99, platform = "windows"), installedVersionCode = 11)
        assertEquals(UpdateDecision.Verdict.NotForUs, e.verdict)
    }

    @Test
    fun noApkMeansNothingToInstall() {
        val e = UpdateDecision.evaluate(
            fields(versionCode = 99, artifacts = listOf(apk(name = "GNLinkHost.exe"))),
            installedVersionCode = 11)
        assertEquals(UpdateDecision.Verdict.NotForUs, e.verdict)
    }

    @Test
    fun twoApksGiveNoBasisForChoosing() {
        // Picking the first would be inventing a rule the signature does not express.
        val two = listOf(apk("a.apk"), apk("b.apk"))
        val e = UpdateDecision.evaluate(
            fields(versionCode = 99, artifacts = two), installedVersionCode = 11)
        assertEquals(UpdateDecision.Verdict.NotForUs, e.verdict)
        assertNull(UpdateDecision.apkArtifact(fields(99, artifacts = two)))
    }

    @Test
    fun theApkIsFoundAlongsideOtherFiles() {
        val f = fields(versionCode = 99, artifacts = listOf(
            apk(name = "changelog.txt"), apk(name = "GNLink.apk"), apk(name = "notes.md")))
        assertEquals("GNLink.apk", UpdateDecision.apkArtifact(f)?.name)
    }

    @Test
    fun theSuffixMatchIsCaseInsensitive() {
        val f = fields(versionCode = 99, artifacts = listOf(apk(name = "GNLink.APK")))
        assertEquals("GNLink.APK", UpdateDecision.apkArtifact(f)?.name)
    }

    // ---------------------------------------------------------------- what arrived

    @Test
    fun aMatchingDownloadPasses() {
        val a = apk(size = 1024)
        assertEquals(UpdateDecision.DownloadVerdict.Ok,
            UpdateDecision.verifyDownload(1024, a.sha256, a))
    }

    @Test
    fun aTruncatedDownloadIsRejectedOnSize() {
        val a = apk(size = 1024)
        assertEquals(UpdateDecision.DownloadVerdict.WrongSize,
            UpdateDecision.verifyDownload(1000, a.sha256, a))
    }

    @Test
    fun theRightLengthWithTheWrongContentIsRejectedOnHash() {
        // The case size alone cannot catch, and the reason the hash is checked at all.
        val a = apk(size = 1024)
        assertEquals(UpdateDecision.DownloadVerdict.WrongHash,
            UpdateDecision.verifyDownload(1024, "b".repeat(64), a))
    }

    @Test
    fun theDigestSpellingDoesNotDecideTheAnswer() {
        // The manifest's own hash was already required to be lowercase when it parsed, so
        // accepting either spelling of the COMPUTED digest loosens nothing.
        val a = apk(size = 1024)
        assertEquals(UpdateDecision.DownloadVerdict.Ok,
            UpdateDecision.verifyDownload(1024, a.sha256.uppercase(), a))
    }

    // ---------------------------------------------------------------- saying no is an answer

    @Test
    fun aDismissedDialogIsNotAFailure() {
        // 3 is STATUS_FAILURE_ABORTED, which is what a dismissed install dialog reports.
        assertEquals(UpdateDecision.InstallOutcome.Cancelled,
            UpdateDecision.installOutcomeFromStatus(3))
        assertTrue(UpdateDecision.isUserDecision(UpdateDecision.InstallOutcome.Cancelled))
    }

    @Test
    fun refusingThePermissionIsNotAFailureEither() {
        assertTrue(UpdateDecision.isUserDecision(UpdateDecision.InstallOutcome.PermissionDenied))
    }

    @Test
    fun anActualFailureIsOne() {
        assertEquals(UpdateDecision.InstallOutcome.Failed,
            UpdateDecision.installOutcomeFromStatus(4))
        assertFalse(UpdateDecision.isUserDecision(UpdateDecision.InstallOutcome.Failed))
    }

    @Test
    fun successIsSuccess() {
        assertEquals(UpdateDecision.InstallOutcome.Installed,
            UpdateDecision.installOutcomeFromStatus(0))
        assertFalse(UpdateDecision.isUserDecision(UpdateDecision.InstallOutcome.Installed))
    }

    @Test
    fun everyOutcomeIsEitherAUserDecisionOrNot() {
        // Stated as a property so a new outcome cannot be added without deciding which side it is
        // on. Getting that wrong is what produces an error dialog after someone declines.
        val decisions = UpdateDecision.InstallOutcome.values().count {
            UpdateDecision.isUserDecision(it)
        }
        assertEquals(2, decisions)
    }
}
