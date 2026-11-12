# PDF Pro 1.3.5 clean Windows acceptance

Owner-run on disposable Windows 10 or 11 x64 VM snapshots with no DriveVault or Qt present. Use synthetic PDF files. This is separate from the package's import audit.

1. Compare the setup SHA-256 with the accompanying file. Record Windows version, installer hash and test date.
2. From a fresh snapshot, choose **Just for me**. Verify both PDF Pro and Master Pro install, Master Pro shows the red PDF Pro card as installed, and the card opens it. Check the registered paths in HKCU and that the guide opens from the Start menu.
3. Check that **Open with** lists PDF Pro and that the existing PDF default still opens normally. Open a sample with Thai text and two pages. Search, export JPEG and verify a ZIP opens. Check a redacted copy using a second reader.
4. Fill an AcroForm, protect and unlock a copy, check metadata and attachments, and verify batch output and repair on test files. With the network disconnected, OCR a Thai image-only scan, then reopen and search its copy. Replace a supported horizontal text run and verify the new text in a second reader.
5. Check that Master Pro refuses removal while PDF Pro is installed. Uninstall PDF Pro and confirm source PDFs, exported files and settings remain. Then uninstall Master Pro.
6. Restore the VM and repeat as **All users**, checking HKLM, the expected Program Files paths and launch from a second account. Test upgrade with a previously installed Master Pro version and confirm setup reports failure if the hub prerequisite cannot be installed.

Record the outcome for each item before approving publication. These checks change installed programs and Windows registration, so run them only on the designated test system.
