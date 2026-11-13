# GODify

Games on Demand converter for RGH/JTAG Xbox 360. It turns games and apps on your HDD or USB into
packages that show up on the dashboard. No PC needed.

| Pick | Result |
|---|---|
| **Game** (`default.xex`) | Full GOD conversion of the folder |
| **App or Demo** (any `.xex`) | Small shortcut that launches the app from its folder |
| Original Xbox (`default.xbe`) | Full conversion as Xbox Original |

## Usage

1. Copy `GODify.xex` to your console and launch it.
2. **Browse Games**, go into a game folder and press **A** on its `.xex`/`.xbe`, or press **X** on several to
   queue them and **Y** to convert the queue.
3. Choose **Game** or **App or Demo**, rename it if you like, and convert.

Output goes to `Hdd:\Content\0000000000000000\` and appears in the dashboard's Games list right away.

**Notes**
- Your original folders are never touched. Delete them yourself once the converted version works.
- Don't move or delete an app's folder after making a shortcut, because the shortcut points to it.
- Output is HDD only. Games stored on USB can still be converted.
- Multi-disc games: convert each disc separately.

## Building

Visual Studio 2010 + Xbox 360 XDK. Open `GODify.sln`, select `Release_LTCG | Xbox 360` and build.
Output: `GODify\Release_LTCG\GODify.xex` (the launcher is embedded in it).

`tests/` has a PC build of the converter core, which was checked byte-for-byte against iso2god-rs.

## Credits

- [iso2god-rs](https://github.com/iliazeus/iso2god-rs): GOD format reference and title name list (MIT)
- [extract-xiso](https://github.com/XboxDev/extract-xiso): XDVDFS layout rules

Provided as is, without warranty.
