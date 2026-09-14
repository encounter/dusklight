# Save manager

Open **Settings > Prelaunch > Save Files > Open Save Manager** from the main menu. A configured GameCube disc is required.
Each entry represents a registered game mode's save file, including all three slots. Modes with the same save filename
share an entry and mod data.

## Import and export

- **Dusklight archive (`.dusksave`)**: a GCI save, mod sidecar files, and version metadata. Importing an archive replaces
  both the game save and its mod data.
- **GCI (`.gci`)**: a single game save without mod data. Imports clear existing mod data by default; enable **Keep existing
  mod data** only when that data belongs to the imported save. The game and region must match the configured disc.
- **Raw card image (`.raw`)**: importing into GCI storage extracts saves for registered modes. Use **Import all modes** to
  import every matching save. Importing into raw storage replaces the entire card, including unrelated game saves.

Save files can also be dropped onto the main menu. Dusklight mod packages continue to use the package installer.

## Backups

Imports, restores, save deletion, and mod-data deletion back up an existing game save and its mod data before changing
it. The manager keeps five recovery copies per save file. Deleting mod data without an existing game save does not create
an archive.

Backups are stored in a `backups` directory inside the GCI folder or beside the raw card image. Raw card replacement backs
up registered mode saves; it does not back up unrelated games on the card. Export the full card image to preserve those.
