# libnw - Objects

The first phase of Objects is essentially NWN toolset like structures. No information hiding, no getters/setters and the like.  Future phases may include simulation, etc.  The main goal is transparent initialization of objects from Gff or libnw's JSON format.

## Profiles

NWN has three different (de)serialization profiles:

* **blueprint** - UTC, UTT, etc, etc.  BIC is included here, though not a blueprint itself.
* **instance** - Instances of blueprints stored in an area's GIT file.
* **savegame** - All game and object state.  This is outside of the scope of this library.. for now.

## ObjectID, ObjectType, and ObjectHandle

libnw expands how NWN(:EE) identifies objects.  In addition to a unsigned 32bit integer ID, an `ObjectHandle` contains 16bits for object type and 16bits for version.  This allows ID to be a simple index into an array that can be safely reused 2^16 times while still being able to be backward compatible if (big if) loading save games is ever supported.

## Components

|   Type    |   Read   |  Write   |                        Notes                        |
| --------- | -------- | -------- | --------------------------------------------------- |
| Common    | Gff      | No       | Things common to all objects, writing handled by parent object.
| Location  | Gff/JSON | Gff/JSON | Writing will be a little tricky because some things store their heading in radians, others in vectors.  |
| LocalData | Gff/JSON | Gff/JSON | Will encompasses local variables, baked sqlite3, and NWNX:EE POS.  Only local variables.. for now |
| Lock     | Gff/JSON | Gff/JSON | Encapsulate lockable objects
| Saves    | Gff/JSON | Gff/JSON | a struct for saves, may be moved elsewhere
| Trap     | Gff/JSON | Gff/JSON | Encapsulate trapable objects

## Status

|      Type      |   Read   |   Write  | Notes
| -------------- | -------- | -------- | -----------------------------------------------|
| ObjectBase     |    Gff   |    No    | Base class for objects.
| Area           | Gff/JSON |  JSON    | ARE and GIT only
| Creature       | Gff/JSON | Gff/JSON |
| Dialog         |    Gff   |    No    | This is very old, doesn't include new EE stuff.. yet.
| Door           | Gff/JSON | Gff/JSON |
| Encounter      | Gff/JSON | Gff/JSON |
| Faction        | Gff/JSON | Gff/JSON |
| Item           | Gff/JSON | Gff/JSON | Probably missing new EE stuff.
| Journal        | Gff/JSON | Gff/JSON |
| Module         | Gff/JSON | Gff/JSON |
| Palette        |    Gff   |   JSON   | Surprised this wasn't ditched first thing in EE.
| Placeable      | Gff/JSON | Gff/JSON |
| Sound          | Gff/JSON | Gff/JSON |
| Store          | Gff/JSON | Gff/JSON |
| Trigger        | Gff/JSON | Gff/JSON |
| Waypoint       | Gff/JSON | Gff/JSON |
