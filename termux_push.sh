#!/data/data/com.termux/files/usr/bin/bash
# One-command push for Termux. Run inside the project folder:  ./termux_push.sh "my message"
# It RE-CREATES the hidden .github/workflows folder (phones/file managers often lose hidden
# folders), stages everything, commits, pushes, and then proves the workflow reached the commit.
set -e
cd "$(dirname "$0")"

echo "== 1/4 Making sure the GitHub build workflow exists"
mkdir -p .github/workflows
cp -f workflow_backup/mdee.yml .github/workflows/mdee.yml
ls -la .github/workflows

echo "== 2/4 Git setup"
[ -d .git ] || git init -b main
git config user.name  >/dev/null 2>&1 || git config user.name  "mdee-robot"
git config user.email >/dev/null 2>&1 || git config user.email "mdee@example.com"
git config --global credential.helper store      # remembers your token after the first push
git branch -M main

if ! git remote get-url origin >/dev/null 2>&1; then
  if [ -n "$REMOTE_URL" ]; then
    git remote add origin "$REMOTE_URL"
  else
    printf "GitHub username: "; read GH_USER
    printf "Repository name: "; read GH_REPO
    git remote add origin "https://github.com/$GH_USER/$GH_REPO.git"
  fi
fi

echo "== 3/4 Commit"
git add -A
git add -f .github/workflows/mdee.yml          # force it, even if something ignores hidden folders
git commit -m "${1:-MDEE update}" || echo "(nothing new to commit)"

echo "== 4/4 Push  (when asked: Username = your GitHub name, Password = your TOKEN, not your password)"
git push -u origin main

echo
if git ls-tree -r HEAD --name-only | grep -qx ".github/workflows/mdee.yml"; then
  echo "OK  The workflow is in the pushed commit. Open GitHub > your repo > Actions."
else
  echo "PROBLEM  The workflow is NOT in the commit. Run:  git add -f .github/workflows/mdee.yml && git commit -m fix && git push"
fi
