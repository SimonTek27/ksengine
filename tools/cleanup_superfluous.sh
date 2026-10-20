#!/usr/bin/env bash
# Finish repo cleanup: promote SecureConnect body, delete milliken_extract.
# Run from repository root:
#   bash tools/cleanup_superfluous.sh
set -euo pipefail
cd "$(dirname "$0")/.."

echo "== git pull =="
git pull --ff-only

if [[ -f milliken_extract/ksnet.cpp ]]; then
  echo "== promote SecureConnect body -> src/engine/network/ksnet/ksnet.cpp =="
  cp -f milliken_extract/ksnet.cpp src/engine/network/ksnet/ksnet.cpp
  git add src/engine/network/ksnet/ksnet.cpp
fi

if [[ -d milliken_extract ]]; then
  echo "== remove milliken_extract/ =="
  git rm -rf milliken_extract
fi

shopt -s nullglob
for f in *.vcxproj *.vcxproj.filters; do
  echo "== remove $f =="
  git rm -f --ignore-unmatch "$f" || true
done
[[ -d backups ]] && git rm -rf backups || true

echo "== status =="
git status --short

if git diff --cached --quiet; then
  echo "Nothing to commit."
  exit 0
fi

git commit -m "$(cat <<'EOF'
chore: finish cleanup — drop milliken_extract, ship SecureConnect body

Promotes milliken_extract/ksnet.cpp into src/engine/network/ksnet/ksnet.cpp
(SecureConnect + SetPrivateKey XOR path) then removes the staging tree.
EOF
)"

echo "== push =="
git push origin HEAD
echo "Done."
