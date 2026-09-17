# Browser selection highlight after markup replacement

Observed while implementing client-main-refactor C12a: a forced project-tree
window rebuild retains selected_recent_index but drops the selected CSS class.
render_project_tree_window replaces the rows and calls set_recent_selected with
the unchanged index; the setter's equality guard skips painting the new DOM.
The same setter is called after area-list replacement.

The production headless characterization is
ClientBrowserWorkspace.RebuildRetainsTheSelectedIndexAndItsHighlight in
tests/rollnw_client_browser_workspace.cpp. It verifies the index remains zero
and the rebuilt selected row loses its highlight. This is existing behavior,
kept in the extraction checkpoint and repaired in a separate behavior commit.

Fix contract: keep the selected index and paint the current visible row batch
when this presentation function is called. Unchanged virtual windows already
skip rebuilding and painting. No extra render state or force parameter is needed.
Cost: traversal of current visible DOM rows on explicit selection presentation;
no performance improvement is claimed.

Status: repaired after the extraction checkpoint by removing the equality guard.
The desired production regression failed before the repair; final focused
normal/sanitized results are recorded in client-main-refactor-progress.md.
