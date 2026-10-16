#!/bin/bash
# inteilidOS post-merge setup
# This project is a bare-metal C/ASM OS. There are no runtime dependencies
# to install (no npm, pip, etc.) and no migrations to run.
# The script intentionally exits 0 so the platform merge flow succeeds.
set -e
echo "Post-merge: inteilidOS source-only project — nothing to install."
exit 0
