# GODify – Games on Demand converter for RGH/JTAG Xbox 360

Convert games and apps you already have on your HDD or USB into dashboard packages, right on the console.
Pick any `.xex` (`default.xex`, `Aurora.xex`, …) or an original Xbox `default.xbe`. Before converting a `.xex`,
you choose what it is, so nothing is guessed:

| You choose | What it makes | Saved as |
|---|---|---|
| **Game** | Full conversion: the whole folder goes into the package | `<TitleID>\00007000\<MediaID>` (same as iso2god) |
| **App or Demo** | **Shortcut:** a tiny package that starts the program from its folder | `<TitleID>\00007000\<Name>_<tag>` |
| *(any `.xex` not named `default.xex`)* | Always a shortcut: its folder may hold unrelated files, so it isn't scanned | `<TitleID>\00007000\<Name>_<tag>` |
| *(any `default.xbe`)* | Full conversion as Xbox Original | `<TitleID>\00005000\<TitleID>` |

All of them show up in the dashboard's Games list.

**Why shortcuts for apps:** a full package is read-only, so apps that save into their own folder (FSD, Aurora
and similar dashboards) hang or lose their settings. A shortcut contains only GODify's small launcher plus a
`launch.txt` with the program's path (e.g. `Hdd:\--- apps ---\FSD3\default.xex`). The launcher mounts just
that drive (as `tmp:`), tidies the file cache and starts the program, the same steps QuickBoot takes, so Xbox
LIVE stays connected. The app runs from its real,
writable folder, and the package is under 1 MB. **Don't move or delete the app's folder.** If you do, the
shortcut shows a message and returns to the dashboard; convert the app again to fix it. (Shortcuts to USB
drives depend on the stick keeping the same slot.) Each shortcut uses the app's own title ID (or a made-up
one if the `.xex` has none), so "View details" shows the right name. The 8-character tag stays the same when
you convert the same app again, so the shortcut gets replaced.

They then appear under **Games & Apps** on the stock dashboard. You choose what gets converted. Nothing is
converted automatically.

## Using it

1. Copy `GODify.xex` (with its folder) to your console and launch it from Aurora, FSD, XexMenu or similar.
2. **A: Browse Games**, then pick a drive (`Hdd:`, `Usb0:`, …) and go into folders.
3. **A always opens a folder.** Executables are listed after the folders as `>> default.xex`,
   `>> default.xbe`, `>> Aurora.xex` and so on. Inside a game folder the cursor starts on `default.xex`.
   Press **A** on one to convert it.
4. To convert several, press **X** on each game/app folder (or on an executable) to queue it (`[QUEUED]`),
   then press **Y: Convert Queue**. If a folder has more than one `.xex`, open it and press X on the right one.
   * GODify refuses a folder that contains other game folders (for example your whole `Games` folder with a
     stray `default.xex` in it), so it can never bundle your library into one package by mistake.
5. **One item:** the next screen shows the name and your choices: **A: Game** (full conversion),
   **X: App or Demo** (shortcut) and **Y: Rename**. Below them it shows the space needed and what's free on the HDD.
   **A queue:** you get a list of everything queued. Press **A** on an item to switch it between Game and
   App or Demo, **Y** to rename it, then **X: Convert All**.
6. A progress screen shows the percentage, MB/s and time left. **B** cancels and removes the partial output.

Output goes under `Hdd:\Content\0000000000000000\` (see the table above), as a header file plus a
`<name>.data\Data0000…` folder.

The browser lists folders first, then files. Names starting with symbols (like `--- Games ---`) come first,
then numbers, then letters, the way XeXMenu sorts them. **Exit** is available everywhere except while converting. The dashboard picks it up automatically. In Aurora/FSD, make sure
`Hdd:\Content` is a scan path and rescan.

### Good to know
* **Dashboard list refresh:** the dashboard keeps its list of installed content in
  `Hdd:\Content\0000000000000000\FFFE07DF\00040000\ContentCache.pkg`, and normally only Settings > System >
  Storage rebuilds it. After converting, GODify renames it to `ContentCache.pkg.old` so the system rescans the
  drive, and the new package shows up straight away. To undo it, rename the `.old` file back over FTP.
* **The original folder is not touched.** Delete it yourself once you've checked that the GOD version boots.
* **Free space:** you need roughly the game's size free on the HDD, because no temporary ISO is written.
  The app checks this before starting.
* **Output is HDD only.** The dashboard does not read GOD content from FAT32 USB sticks, though you can still
  convert games that are stored on USB.
* **Multi-disc games:** convert each disc folder separately. Each disc gets its own Media ID file.
* **Homebrew quirks handled automatically:** "disc 0 of 0" (DashLaunch) is written as disc 1 of 1, which the
  dashboard needs to list it. A `.xex` with no title ID at all (FreestyleDash) gets a made-up but stable ID,
  so converting it again replaces the same package.
* **Apps converted as Game** (full conversion): the console only boots `default.xex` from a package. When you pick `Aurora.xex`, the image also
  gets a `default.xex` entry that points at the same data. Nothing is copied, and the original name is kept.
  If the folder already had a different `default.xex`, that file is left out of the package.
  **GOD packages are read-only.** Apps that save settings, databases or downloads into their own folder (Aurora,
  FSD and similar dashboards) can't write there when run from a GOD package. Use this for simple homebrew,
  emulators and tools. Keep full dashboards as normal folders.
* **Original Xbox:** the game must boot from `default.xbe`. The name comes from the XBE itself. To run, the
  console needs Xbox backwards compatibility installed, and the title has to work with the 360's emulator.
* **`$SystemUpdate`** folders in a game are skipped, since they aren't needed on an RGH.
* **Re-converting** the same disc replaces the old GOD package. The confirm screen warns you first.
* Names come from a built-in list of about 4,900 titles (from iso2god-rs), then from the XBE for original Xbox
  games, then from the app's file name or the folder name. You can rename on the confirm screen. No tile icon is embedded, so the dashboard shows its default tile.
* Files over 4 GB can't go in a disc image. FATX can't hold them either, so this never comes up in practice.

## Building

Requirements: Visual Studio 2010 with the Xbox 360 XDK (same setup as SonusGUI).

The solution has two programs. **GODifyLauncher** is the small launcher used inside shortcut packages, and it
builds first. GODify then embeds its `.xex` as the `LNCH` section (see `GODify\AppConf.xml`), so you still only
copy `GODify.xex` to the console.

1. The XUI resources (`Media\XD_res.xzp`) are packaged automatically before each build. To do it by hand, run
   `GODify\Media\Build_XZP.bat`. It creates `Media\XD_res.xzp` from
   `COMPLETED_RESOURCES` and needs `%XEDK%`. The old SonusGUI `.xzp` was removed because the scene changed:
   the list is now `FileList`, there's a new `textPath` line, and the buttons were moved.
2. Open `GODify.sln`, select `Release_LTCG | Xbox 360`, and build. The output is `GODify\Release_LTCG\GODify.xex`.

## How the conversion works

`GODify/God/GodCore.cpp` holds all of the conversion logic and is platform independent:

1. It reads the execution ID (Title ID, Media ID, disc number) from the `default.xex` header. This works on
   encrypted retail XEX files because that header isn't encrypted.
2. It scans the folder and computes an XDVDFS (XISO) image layout in memory, using extract-xiso's layout rules:
   the root directory table sits at sector 0x108, directory entries are stored as binary search trees, padding is 0xFF,
   and the image is rounded up to a 64 KB boundary.
3. It streams that virtual image directly into GOD data parts. Each part has a master hash block followed by
   203 × (sub-hash block + 204 × 4 KB blocks). The master hash blocks are chained from one part to the next, and
   an unsigned `LIVE` header (content type 0x7000) is written last. RGH/JTAG consoles don't check the signature.

`GodPlatform_Xbox.cpp` is the only Xbox-specific part (file I/O through the XDK, SHA-1 through `XeCryptSha`).

## Verification (tests/)

`tests/` contains a POSIX version of the platform layer plus a command-line driver, so the same core runs on a PC.
It was tested against a synthetic 433 MB Xbox 360 game, an app (`Aurora.xex` plus a decoy `default.xex`)
and an original Xbox game. For all three, iso2god-rs produces byte-identical data parts from the same image,
with the same folder names and content types. The main test used the 433 MB game. That game had a real-format XEX2 header, 410 files, a 400-file
folder whose directory table spans several sectors, an empty folder, a zero-byte file, deep nesting and mixed-case
names. The results:

* The generated image extracts byte-identically with **extract-xiso**.
* Every file is found through a case-insensitive binary-tree lookup, the same way the console looks files up.
* Running **iso2god-rs** on the same image gives **byte-identical `Data0000`–`Data0002`**, including all hash
  tables and the hash chain. The header is identical apart from one leftover template description string that
  iso2god carries, and the header hash that covers it.
* Cancelling partway removes all partial output.

```
cd tests
g++ -O2 -o godtool godtool.cpp platform_posix.cpp ../GODify/God/GodCore.cpp -lcrypto
./godtool /path/to/game/default.xex out_dir                 # convert (also App.xex or default.xbe)
./godtool /path/to/game/default.xex out_dir --dump-iso x.iso # write the virtual XISO for inspection
```

## Credits & licences
* UI base: [SonusGUI](https://github.com/Byrom90/SonusGUI) by Byrom (ATG framework and xkelib come with it)
* GOD format, header fields and title name list: [iso2god-rs](https://github.com/iliazeus/iso2god-rs) by
  Ilia Pozdnyakov (MIT licence, see `licenses/iso2god-rs-LICENSE.txt`)
* XDVDFS layout rules: [extract-xiso](https://github.com/XboxDev/extract-xiso) (see `licenses/extract-xiso-LICENSE.txt`)

This software is provided as is, without warranty. Keep your original game folders until you've confirmed the
converted version works.
