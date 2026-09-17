# SDL native dialog thread completion at shutdown

Status: scoped follow-up; application-owned delivery is guarded.

The Unix Zenity backend starts a detached thread in
`external/SDL/src/dialog/unix/SDL_zenitydialog.c`. A request can finish after the
client closes or SDL quits. The client cannot infer thread completion from the
application callback returning: SDL still frees its own request/thread data
after that callback.

`loading_view` requests own copied command/location strings and a shared delivery
gate, never AppState or DOM pointers. Its callback holds the gate mutex while
checking acceptance and transferring an owning result to the SDL queue. Shutdown
closes acceptance under the same mutex, then drains all results of the registered
event type while SDL events are live. Late callbacks free their request and return
before SDL error/queue calls. Production tests cover delivered/canceled/error,
malformed requests, late callback and drain paths. This establishes application
payload lifetime; it does not establish SDL's internal thread completion.

Resolve the remaining SDK question by observing a supported native dialog during
close/quit under sanitizers, then inspecting the backend's post-callback cleanup.
Any SDK fix needs its own failing regression and a separate dependency change.
No desktop dialog was launched for the client refactor, and no vendor cleanup
guarantee is claimed. The refactor retains the native dialog backend and scopes
this limitation instead of adding an unverified wait or cancellation mechanism.
