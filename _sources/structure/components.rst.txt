objects & components
====================

The System
----------

ObjectID
   Unlike NWN an ObjectID does not provide a one-to-one mapping to an object.  Rather,
   it's an index into a structure containing objects.

ObjectHandle
   An object handle maps to a specific object it consists of an ObjectID, the objects type,
   and an unsigned 16 bit integer indicating the object's version.  To be valid, an object
   handle must match what is in the object array.

ObjectBase
   The base class of all objects
