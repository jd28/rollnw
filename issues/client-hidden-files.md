# Project browser hidden files and directories

Status: open

The project browser currently omits `.git` directories and worktree pointer files.
This is a listing filter, not a resource-access restriction.

Follow-up:

- Define hidden-file and hidden-directory behavior across supported platforms.
- Add a user-facing Show hidden option, including search behavior.
- Decide how editor-owned `.rollnw` metadata participates in that option.
- Test files, directories, nested entries, and persisted visibility preferences.

Do not delete or rewrite filesystem entries when changing visibility.
