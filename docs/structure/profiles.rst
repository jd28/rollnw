profiles
========

Profiles are a way of decoupling different rulesets from the rule system
itself. The current supported interface is below and will beging by
implementing a rules system similar to Neverwinter Nights 1 (long way to
go on this!).

.. code:: cpp

   struct GameProfile {
       virtual ~GameProfile() = default;

       virtual bool load_constants() const = 0;
       virtual bool load_compontents() const = 0;
       virtual bool load_rules() const = 0;
   };
