# Project memory (portable copy)

Curated Claude Code project memory for practice-engine, committed so it travels
with the repo. This is a **copy for portability** — the live memory Claude reads
lives outside the repo, under the user's home:

```
~/.claude/projects/-<abs-path-with-slashes-as-dashes>-practice-engine/memory/
```

## Restore on another machine

After cloning the repo, copy these files into that live location so the next
Claude session picks them up (adjust the path segment to the clone's absolute
path — Claude derives it from where the repo sits):

```sh
DEST=~/.claude/projects/-home-<user>-Workspace-practice-engine/memory
mkdir -p "$DEST"
cp .claude/memory/MEMORY.md .claude/memory/feedback_language.md "$DEST"/
```

Only non-sensitive curated memory is committed here. Credentials, session
transcripts, and cross-project history are intentionally excluded.
