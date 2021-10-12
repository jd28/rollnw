# libnw - Objects

The first phase of Objects is essentially NWN toolset like structures. No information hiding, no getters/setters and the like.  Future phases may include simulation, etc.  The main goal is transparent initialization of objects from Gff or [neverwinter.nim](https://github.com/niv/neverwinter.nim) JSON format.

## Profiles

NWN has three different (de)serialization profiles:

* **blueprint** - UTC, UTT, etc, etc.  BIC is included here, though not a blueprint itself.
* **instance** - Instances of blueprints stored in an area's GIT file.
* **savegame** - All game and object state.  This is outside of the scope of this library.. for now.

## Components

|   Type   | Read | Write |                        Notes                        |
| -------- | ---- | ----- | --------------------------------------------------- |
| Location | Gff  | No    | Writing will be a little tricky because some things store their heading in radians, others in vectors.  |
| LocalData | Gff | No    | Will encompasses local variables, baked sqlite3, and NWNX:EE POS.  Only local variables.. for now |
| Lock     | Gff  | No    | Encapsulate lockable objects
| Trap     | Gff  | No    | Encapsulate trapable objects
| Saves    | Gff  | No    | a struct for saves, may be moved elsewhere

## Status

|      Type      | Read | Write | Notes
| -------------- | ---- | ----- | -----
| Object         | Gff  | No    | Parent of Creature, Encounter, SituatedObject, etc.
| SituatedObject | Gff  | No    | Parent of Placeables and Doors
| Area           | No   | No    |
| Creature       | No   | No    |
| Dialog         | Gff  | No    | This is very old, doesn't include new EE stuff.. yet.
| Door           | Gff  | No    |
| Encounter      | No   | No    |
| Faction        | No   | No    |
| Item           | Gff  | No    | Probably missing new EE stuff.
| Journal        | No   | No    |
| Module         | No   | No    |
| Palette        | Gff  | No    | Surprised this wasn't ditched first thing in EE.
| Placeable      | Gff  | No    |
| Sound          | Gff  | No    |
| Store          | No   | No    |
| Trigger        | Gff  | No    |
| Waypoint       | Gff  | No    |
