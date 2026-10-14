# Development test installation and blue-screen recovery

**Read this entire guide and keep an offline copy before changing the Phoebus PC. This is an untested kernel driver, not a normal-use audio driver. A signature does not establish safety or working audio.**

The separate `v0.1.0-test.1` release provides a test ZIP with a self-signed SYS and catalog, the public test certificate, review scripts and instructions, plus a separate complete source ZIP. It is NOT Microsoft signed, WHQL qualified or tested on a physical Phoebus. It can crash Windows, hang, lose audio or interrupt work/data writes. It may expose no endpoints because board routing is incomplete. There is no confirmed fix for game clicks or optical dropouts. Production signing would not fix those limitations.

The original `v0.1.0-preview.1` download remains unsigned. Never apply these instructions to a ZIP that lacks the matching catalog/certificate. No installer or script in this project enables test mode, imports trust, installs a driver or changes firmware settings automatically.

## 1. Choose a recoverable target and get its owner's agreement

Use a spare development PC with the physical card, or a spare Windows SSD with the normal system drive disconnected. Back up any important data first. A spare SSD reduces disk exposure but does not isolate shared firmware settings or guarantee protection from hardware/kernel faults. An ordinary VM without access to the real card cannot qualify its playback.

The owner must understand and approve driver replacement, certificate trust and any boot/security changes. Do not test on a managed/work PC without its administrator's authorization. Do not start without all of these:

- A tested disk image/backup and a way to restore it. A restore point alone is insufficient.
- A bootable Windows recovery/install USB, checked that it starts. Choose **Repair your computer**, not Install, for recovery.
- The correct BitLocker/device-encryption recovery key, stored off the test PC. Never post that key, passwords or recovery codes in GitHub issues.
- The known-good driver package/installer and any separate control-panel/effects installers. Exporting an INF does not preserve all vendor utilities.
- Alternate audio, an account password usable in Safe Mode, and these instructions saved on another device/USB.
- A record of the original Secure Boot, test-signing, encryption and Memory Integrity settings. Firmware security changes affect the whole machine, including later boots of the normal SSD.

Do not enable Driver Verifier for the first playback test. It can deliberately bugcheck the system and requires a separate qualified debugging plan.

## 2. Record the baseline without installing this driver

Use `winver` to record the exact Windows build. In Device Manager, open the Phoebus HDA audio function's Properties > Details > Hardware Ids. The required match is:

```text
HDAUDIO\FUNC_01&VEN_13F6&DEV_8888&SUBSYS_104384D5
```

Do not select the Realtek/Intel audio device, another Xonar, or the parent HDA controller. Stop on a mismatch. Run the read-only diagnostic collector from the original preview before replacing drivers; preserve its local report.

In an Administrator **Command Prompt**, list the current packages/devices:

```cmd
pnputil /enum-drivers /class MEDIA /files
pnputil /enum-devices /class MEDIA /connected /deviceids /drivers
```

Record the matching card's instance ID, installed provider/version, and original published INF name. To export the correct existing package, first create a backup folder, then replace `oemNN.inf` with the actual original package name:

```cmd
mkdir C:\PhoebusBackup
pnputil /export-driver oemNN.inf C:\PhoebusBackup
```

Copy the backup off the PC. Keep the ORIGINAL and NEW published INF names separately; never guess them or run wildcard deletion commands. [Microsoft PnPUtil reference](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/pnputil-command-syntax).

## 3. Verify the downloaded test package

Compare the ZIP's SHA-256 with the release's `SHA256SUMS-test.txt` using `Get-FileHash`. Extract into a new folder such as `C:\PhoebusTest`. Before generating reports inside it, verify the complete fresh extraction with Python 3.11+:

```powershell
python .\verify-release.py .
```

The unsigned manifest/checksum checks bytes, not publisher identity or safety. Review `test-signing.json`, the sources and scripts before running anything. The signing script is included for audit and optional manual regeneration; it does not need to run on the target.

For the initially published test package, the public certificate identifiers are:

```text
Subject: CN=Phoebus4eva development test only
SHA-1 thumbprint: CC78F76C149C8D4BE6D8B12535DFCA4AF1F1552D
CER SHA-256: ad56d30897d0b2d551526791c7d731f1895a8aa6e4cfdc13b6685cdffc397350
Valid until: 2027-01-12 21:41:21 UTC
```

There is no trusted timestamp. Stop if the certificate has expired or these identifiers differ for this release. A newly self-built/re-signed package has different identifiers and must be audited as a separate package; do not blindly reuse these trust commands for it.

The following optional local verification checks SYS/CAT CMS signatures against the included certificate, and independently checks the SYS SHA-256 Authenticode digest. It does not change trust or prove catalog membership, target kernel acceptance, current certificate validity or safety:

```powershell
.\scripts\Verify-PhoebusTestSignature.ps1 -PackageDirectory . -PythonPath python
```

If Windows PowerShell blocks this optional script under its execution policy, review the helper and Python source first. An explicit one-process invocation is shown below; it does not change the saved user/machine execution policy or kernel signing enforcement. Do not use it to run unreviewed scripts or override a managed policy:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\Verify-PhoebusTestSignature.ps1 -PackageDirectory . -PythonPath python
```

Without deliberately trusting the test certificate, Windows/SignTool reporting an untrusted root is expected. It is not a normal Windows trust verification pass. Do not suppress other signing/digest failures.

## 4. Prepare ONLY the dedicated test Windows installation

Trusting a code-signing certificate permits code signed with its private key; it is not scoped to sound cards. The private key used for this uploaded package was temporary and its PFX was deleted after signing. The download contains only the public CER, never a private key or password.

After the owner's agreement and the checks above, from an Administrator Command Prompt in the extracted package directory:

```cmd
certutil -addstore Root Phoebus4eva-Test.cer
certutil -addstore TrustedPublisher Phoebus4eva-Test.cer
bcdedit /set testsigning on
```

Restart after successfully enabling test mode. These commands deliberately alter machine trust and boot policy; the project does not run them for you. If Secure Boot policy blocks BCDEdit, **stop** and assess the test-machine firmware change with its owner. Disabling Secure Boot can trigger BitLocker recovery. Secure the recovery key and use Microsoft's encryption/firmware guidance before changing anything; restore the original protections after testing. Do not turn off Defender or globally disable integrity checks as troubleshooting.

Memory Integrity/HVCI still requires a signed SYS and compatible driver code. This image has an embedded test signature, but HVCI runtime compatibility is unqualified. Keep the original setting for the first attempt; if Windows blocks it, record the error and stop for diagnosis instead of assuming every security feature must be disabled. [Microsoft test-signing requirements](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/the-testsigning-boot-configuration-option).

After the intentional certificate import, the development package should pass its intended Authenticode checks. If you have the WDK SignTool, run `signtool verify /pa /v PhoebusW11.sys`, `signtool verify /pa /v phoebus.cat`, and catalog membership checks `signtool verify /pa /v /c phoebus.cat PhoebusW11.sys` and `signtool verify /pa /v /c phoebus.cat phoebus.inf`. Stop on failures. `/pa` is Authenticode policy, not proof that this Windows build's kernel will load it. [Microsoft SignTool reference](https://learn.microsoft.com/en-us/windows/win32/seccrypto/signtool).

## 5. Install and confirm the actual binding

Close applications and save work. Start with speakers/headphones disconnected or an external hardware volume control at minimum; software volume/board controls are unqualified. In an Administrator Command Prompt in the signed package folder:

```cmd
pnputil /add-driver .\phoebus.inf /install
```

This can immediately load the driver, so recovery must be ready before running it. Record the NEW published `oemNN.inf` and all installation output on another device. PnPUtil can stage the package without replacing a higher-ranked installed driver. Check Device Manager's Driver Details for `PhoebusW11.sys`, the intended device/provider, service `PhoebusW11`, device status and endpoint appearance. Do not claim a playback pass while the old driver remains bound.

If the old driver is still selected, an experienced tester can use the exact matching Phoebus device's Update driver > Browse my computer > Let me pick > Have Disk and select this INF. Do not override a device mismatch/signature rejection or create a fake root-enumerated device. Stop if you cannot positively identify the target and replacement.

Record Code 10/39/52 or other failures; do not repeatedly reinstall. Preserve `%windir%\inf\setupapi.dev.log` and relevant Event Viewer errors locally. Reboot only with recovery available.

## 6. Run small tests before game/stress testing

First check that Windows remains responsive at idle. Then, at low external volume, try a short stereo signal at 48 kHz/16-bit on one output. Check left/right identity, silence/stop and closing the application. Gradually try speakers, headphones and optical **stereo PCM**, supported rates, shared/exclusive mode, and application switching. The implementation does not supply capture, full multichannel or Dolby/DTS encoding/passthrough.

Only after basic playback succeeds, repeat the same Worms Armageddon replay or World of Warcraft scene used for the baseline. Record actual before/after clicks, dropouts and receiver lock; a game launching is not a pass. Test reboot and sleep/resume later with no unsaved work. Stop on a crash, hang, abnormal noise/output level, missing endpoints, or persistent distortion. Do not keep reconnecting headphones at high volume to chase a routing issue.

## 7. If Windows still starts: normal rollback

Record the failure first. Try Device Manager > the exact Phoebus device > Driver > Roll Back Driver, if available. To remove this test package, verify the recorded NEW published name still identifies `phoebus.inf`, this project's provider and version, then use that exact name (example placeholder below):

```cmd
pnputil /delete-driver oemNN.inf /uninstall
```

Do not use `/force`, wildcards or the ORIGINAL driver INF. Restart if Windows requests it. Reinstall the known-good package/installer and confirm actual playback. Removing a package does not guarantee the preferred old driver will automatically bind.

## 8. If it blue-screens or loops before login

1. Photograph the stop code and any named module; avoid repeated test boots. If available later, preserve `%windir%\Minidump` or `MEMORY.DMP` locally. Dumps can contain private memory; do not upload them publicly without review.
2. Enter Windows Recovery Environment through Automatic Repair, or boot the prepared Windows USB and choose **Repair your computer**. If encryption prompts, use the saved recovery key locally.
3. Choose Troubleshoot > Advanced options > Startup Settings > Restart > **4/F4 Safe Mode**. Use the account password if the PIN is unavailable. If it starts, follow the normal rollback above. Safe Mode is a recovery attempt, not a guarantee. [Microsoft startup settings](https://support.microsoft.com/en-us/windows/experience/startup-boot/windows-startup-settings).
4. If Safe Mode fails, return to WinRE > Advanced options > Command Prompt and remove ONLY the identified test package from the offline Windows installation as described next. System Restore, if a suitable point exists, is another recovery option; a verified disk image/spare SSD is the dependable fallback.

### Offline removal from WinRE (advanced; exact image/package required)

WinRE drive letters differ. **X: is normally the recovery environment, not the installed Windows.** Use `diskpart`, `list volume`, then `exit` to inspect volumes without changing partitions. Unlock the correct encrypted Windows volume using your recovery key if needed. Do not format, clean or delete partitions. Confirm the actual Windows directory with `dir D:\Windows`; **D: below is an example, not a detection result**.

After verifying the correct Windows drive, list its third-party drivers and inspect the recorded NEW published INF:

```cmd
dism /Image:D:\ /Get-Drivers /Format:Table
dism /Image:D:\ /Get-DriverInfo /Driver:oemNN.inf
```

Confirm original INF `phoebus.inf`, project provider/version, service/file identity where shown, and the recorded package. If the INF name differs offline, positively identify the same package first. Stop if uncertain. Do not remove storage, chipset, Microsoft bus or boot-critical drivers. Then remove only the test package:

```cmd
dism /Image:D:\ /Remove-Driver /Driver:oemNN.inf
```

Close Command Prompt and restart. Restore the known-good audio package when Windows starts. If offline removal fails or you cannot identify the image/package, restore the prepared image or reconnect the normal SSD with its original firmware settings. Do not improvise by deleting `System32\drivers` files. [Microsoft offline driver servicing](https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/dism-driver-servicing-command-line-options-s14?view=windows-11).

## 9. Finish by restoring the original security state

After removing the test driver and confirming the known-good driver boots, remove ONLY this release's test certificate from the machine stores where it was deliberately added, then disable test mode. From an Administrator Command Prompt:

```cmd
certutil -delstore TrustedPublisher CC78F76C149C8D4BE6D8B12535DFCA4AF1F1552D
certutil -delstore Root CC78F76C149C8D4BE6D8B12535DFCA4AF1F1552D
bcdedit /set testsigning off
```

Restart and restore the original Secure Boot/encryption/Memory Integrity configuration if it changed, following the owner's recovery plan. Check that the original protections are active. Do not remove unrelated certificates or change an existing developer test configuration without understanding its other users. If a newly generated certificate was used, remove that certificate's actual recorded thumbprint instead.

## Audit and self signing

The unsigned build instructions remain in `docs/build-driver.md`. `scripts/New-PhoebusTestPackage.ps1` signs a separate copy of the exact published preview binary using a fresh 3072-bit RSA/SHA-256 code-signing certificate valid for 90 days. It writes a temporary password-protected PFX in a user-restricted directory, invokes Microsoft SignTool, creates a fresh catalog with Inf2Cat, exports only the public CER and deletes the temporary PFX in `finally`. The password is passed to the local SignTool process, so a privileged process observer could observe it during signing. This is a local disposable development key, not a production signing service or hardware-backed key. Check for leftovers after interruption; never publish PFX/key/password files.

The helper is pinned to the preview binary SHA-256 so its source provenance is explicit. For a new self-built binary, review/update that guard and the source-commit metadata before signing; reproducible build steps do not imply byte-identical output. Each helper run creates a different certificate/key, so a self-signed rebuild needs its own trust/rollback identifiers. The package includes reviewable helper/verifier sources; none of them automatically trust the certificate, install a driver, enable test mode or modify recovery settings.

Signatures and the independent image-digest check passed locally; normal SignTool trust verification intentionally reports an untrusted root on the signing PC. Catalog generation and matching of SYS/INF to the catalog were checked, but no installation, boot, physical audio, HVCI, power/removal or blue-screen recovery exercise has been performed. The pinned kit checks 21H2/22H2/24H2 identifiers; it has no separate 26H2 identifier. These instructions are a recovery plan, not a tested guarantee.
