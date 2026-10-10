# Roadmap

This round adds per-file encoding, visible end-of-line changes, repository groups, a file history window, Linux file-manager menus, undo for recent Git operations, a visual interactive rebase, and a command palette.

The items below were considered against Fork, Tower, and TortoiseGit and are intentionally left for a later change.

## Reflog view

Tower and TortoiseGit can browse the reflog and restore a previous HEAD from it. The undo added here snapshots branch tips and file bytes for operations started in Gittyup. It is not a reflog browser.

## Partial stash

TortoiseGit and Fork can stash a chosen subset of files. Gittyup still stashes the whole working tree.

## Conflict wizard

Fork walks merge conflicts one file at a time with a guided resolution. Gittyup already shows conflict hunks and can take ours or theirs, but it does not guide the user through every remaining conflict.

## Side-by-side diff

Fork and Tower show the old and new file next to each other. The diff here stays in one column, with a file header that stays visible and hunk separators that are quieter than the file header.

## Bisect wizard

TortoiseGit and Fork wrap `git bisect` in a step-by-step window. Gittyup does not start or mark a bisect.

## Worktree management

Tower and Fork can add, open, and remove linked worktrees from the sidebar. Gittyup opens one working tree per repository.

## Commit statistics

Fork and Tower summarize additions, deletions, and authors for a commit or a range. Gittyup does not add that summary.

## File-manager status icons

TortoiseGit and RabbitVCS paint version-control state onto file icons. Those overlays need a shell or file-manager extension and are slow on large trees. The Linux integration only adds context-menu actions that open history, blame, status, or a diff. It does not install an icon overlay.

## Not planned

These came up while comparing clients and are out of scope for Gittyup:

- Pull-request management inside the client
- Generated commit messages
- TortoiseGit's revision graph
