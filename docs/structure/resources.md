# libnw - Containers

The main goals of the library are almost satisfied: the ability to read all NWN(:EE) containers.  The ability to add custom is limited in utility due to [NWNX:EE](https://github.com/nwnxee/unified)'s lack of a ResMan plugin.

| Type    |  Read      | Write  | In Situ Modifications
| ------- | ---------- | ------ | ----------------------|
| Key/Bif | Yes | Never | Never |
| ERF | Yes | Maybe | Maybe |
| NWSync | Yes | Never | Never |
| NWSync Server Directory | Not Yet | Never | Never |
| Directories | Yes | Yes (file format dependant) | N/A |
